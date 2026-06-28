use std::{
    env, fs,
    path::{Path, PathBuf},
    process::{Command, ExitStatus},
    thread,
    time::{Duration, Instant},
};

type Result<T> = std::result::Result<T, String>;

#[derive(Clone, Copy)]
enum UefiArch {
    X86_64,
    Aarch64,
}

impl UefiArch {
    const fn target(self) -> &'static str {
        match self {
            Self::X86_64 => "x86_64-unknown-uefi",
            Self::Aarch64 => "aarch64-unknown-uefi",
        }
    }

    const fn boot_name(self) -> &'static str {
        match self {
            Self::X86_64 => "BOOTX64.EFI",
            Self::Aarch64 => "BOOTAA64.EFI",
        }
    }

    const fn label(self) -> &'static str {
        match self {
            Self::X86_64 => "x86_64",
            Self::Aarch64 => "aarch64",
        }
    }
}

fn main() {
    if let Err(error) = run() {
        eprintln!("xtask: {error}");
        std::process::exit(1);
    }
}

fn run() -> Result<()> {
    let mut args = env::args().skip(1);
    match args.next().as_deref() {
        Some("build") => match args.next().as_deref() {
            None | Some("all") => {
                cargo(&["build", "--release", "-p", "afos-desktop"])?;
                package(UefiArch::X86_64)?;
                package(UefiArch::Aarch64)
            }
            Some("desktop") => cargo(&["build", "--release", "-p", "afos-desktop"]),
            Some("uefi-x86_64") => package(UefiArch::X86_64),
            Some("uefi-aarch64") => package(UefiArch::Aarch64),
            Some(target) => Err(format!("unknown build target: {target}")),
        },
        Some("package") => {
            let arch = parse_arch(args.next().as_deref())?;
            package(arch)
        }
        Some("run") => {
            let arch = parse_arch(args.next().as_deref())?;
            run_qemu(arch)
        }
        Some("smoke") => {
            let arch = parse_arch(args.next().as_deref())?;
            smoke(arch)
        }
        Some("check") => check_all(),
        Some("help") | None => {
            println!(
                "AFOS build utility\n\n\
                 cargo xtask build [all|desktop|uefi-x86_64|uefi-aarch64]\n\
                 cargo xtask package <x86_64|aarch64>\n\
                 cargo xtask run <x86_64|aarch64>\n\
                 cargo xtask smoke <x86_64|aarch64>\n\
                 cargo xtask check"
            );
            Ok(())
        }
        Some(command) => Err(format!("unknown command: {command}")),
    }
}

fn parse_arch(value: Option<&str>) -> Result<UefiArch> {
    match value {
        Some("x86_64" | "uefi-x86_64") => Ok(UefiArch::X86_64),
        Some("aarch64" | "uefi-aarch64") => Ok(UefiArch::Aarch64),
        _ => Err(String::from("expected architecture x86_64 or aarch64")),
    }
}

fn check_all() -> Result<()> {
    cargo(&["fmt", "--all", "--", "--check"])?;
    cargo(&["test", "--workspace"])?;
    cargo(&[
        "clippy",
        "--workspace",
        "--all-targets",
        "--",
        "-D",
        "warnings",
    ])?;
    for target in ["x86_64-unknown-uefi", "aarch64-unknown-uefi"] {
        cargo(&["check", "-p", "afos-uefi", "--target", target])?;
    }
    cargo(&[
        "check",
        "-p",
        "afos-api",
        "-p",
        "afos-core",
        "-p",
        "afos-runtime-rhai",
        "--target",
        "wasm32-unknown-unknown",
    ])
}

fn package(arch: UefiArch) -> Result<()> {
    cargo(&[
        "build",
        "--release",
        "-p",
        "afos-uefi",
        "--target",
        arch.target(),
    ])?;

    let root = workspace_root();
    let source = root
        .join("target")
        .join(arch.target())
        .join("release")
        .join("afos-uefi.efi");
    let destination = root
        .join("dist")
        .join(format!("uefi-{}", arch.label()))
        .join("EFI")
        .join("BOOT")
        .join(arch.boot_name());
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent).map_err(display_error)?;
    }
    fs::copy(&source, &destination).map_err(|error| {
        format!(
            "failed to copy {} to {}: {error}",
            source.display(),
            destination.display()
        )
    })?;
    println!("packaged {}", destination.display());
    Ok(())
}

fn run_qemu(arch: UefiArch) -> Result<()> {
    package(arch)?;
    let root = workspace_root();
    let dist = root.join("dist").join(format!("uefi-{}", arch.label()));
    let image = root
        .join("target")
        .join(format!("afos-{}.img", arch.label()));
    create_fat_image(&image, &dist)?;

    run_qemu_image(arch, &image, false)
}

fn run_qemu_image(arch: UefiArch, image: &Path, headless: bool) -> Result<()> {
    let root = workspace_root();
    let (executable, code, vars) = firmware(arch)?;
    let vars_copy = root
        .join("target")
        .join(format!("afos-{}-vars.fd", arch.label()));
    fs::copy(vars, &vars_copy).map_err(display_error)?;

    let mut command = Command::new(executable);
    match arch {
        UefiArch::X86_64 => {
            command.args(["-machine", "q35", "-m", "256M"]);
        }
        UefiArch::Aarch64 => {
            command.args(["-machine", "virt", "-cpu", "cortex-a72", "-m", "512M"]);
        }
    }
    command
        .arg("-drive")
        .arg(format!(
            "if=pflash,format=raw,readonly=on,file={}",
            code.display()
        ))
        .arg("-drive")
        .arg(format!("if=pflash,format=raw,file={}", vars_copy.display()))
        .arg("-drive")
        .arg(format!("format=raw,file={}", image.display()));
    if headless {
        command.args(["-nographic", "-no-reboot"]);
        run_command_with_timeout(&mut command, Duration::from_secs(90))
    } else {
        run_command(&mut command)
    }
}

fn create_fat_image(image: &Path, source: &Path) -> Result<()> {
    if image.exists() {
        fs::remove_file(image).map_err(display_error)?;
    }
    let file = fs::File::create(image).map_err(display_error)?;
    file.set_len(64 * 1024 * 1024).map_err(display_error)?;

    run_command(
        Command::new("mformat")
            .args(["-i"])
            .arg(image)
            .args(["-F", "-v", "AFOS", "::"]),
    )?;
    run_command(
        Command::new("mcopy")
            .args(["-i"])
            .arg(image)
            .args(["-s"])
            .arg(source.join("EFI"))
            .arg("::/"),
    )
}

fn smoke(arch: UefiArch) -> Result<()> {
    package(arch)?;
    let root = workspace_root();
    let dist = root.join("dist").join(format!("uefi-{}", arch.label()));
    let image = root
        .join("target")
        .join(format!("afos-{}-smoke.img", arch.label()));
    create_fat_image(&image, &dist)?;
    create_smoke_dirs(&image)?;

    inject_commands(
        &image,
        "mkdir /user/saves/smoke\ntouch /user/saves/smoke/note.txt\nhello\n",
    )?;
    run_qemu_image(arch, &image, true)?;
    verify_smoke_result(&image, arch, "first boot")?;

    inject_commands(
        &image,
        "cat /user/saves/smoke/note.txt\nls /user/saves/smoke\n",
    )?;
    run_qemu_image(arch, &image, true)?;
    verify_smoke_result(&image, arch, "persistence boot")?;
    println!("{} UEFI smoke test passed", arch.label());
    Ok(())
}

fn create_smoke_dirs(image: &Path) -> Result<()> {
    for directory in [
        "::/AFOS",
        "::/AFOS/user",
        "::/AFOS/user/config",
        "::/AFOS/user/saves",
    ] {
        run_command(Command::new("mmd").args(["-i"]).arg(image).arg(directory))?;
    }
    Ok(())
}

fn inject_commands(image: &Path, commands: &str) -> Result<()> {
    let host_file = workspace_root()
        .join("target")
        .join("afos-test-commands.txt");
    fs::write(&host_file, commands).map_err(display_error)?;
    run_command(
        Command::new("mcopy")
            .args(["-o", "-i"])
            .arg(image)
            .arg(host_file)
            .arg("::/AFOS/user/config/test-commands.txt"),
    )
}

fn verify_smoke_result(image: &Path, arch: UefiArch, phase: &str) -> Result<()> {
    let result_file = workspace_root()
        .join("target")
        .join(format!("afos-{}-test-result.txt", arch.label()));
    if result_file.exists() {
        fs::remove_file(&result_file).map_err(display_error)?;
    }
    run_command(
        Command::new("mcopy")
            .args(["-o", "-i"])
            .arg(image)
            .arg("::/AFOS/user/config/test-result.txt")
            .arg(&result_file),
    )?;
    let result = fs::read_to_string(result_file).map_err(display_error)?;
    if result.lines().last() == Some("PASS") {
        Ok(())
    } else {
        Err(format!("{} {phase} failed:\n{result}", arch.label()))
    }
}

fn firmware(arch: UefiArch) -> Result<(PathBuf, PathBuf, PathBuf)> {
    let qemu_share = env::var_os("AFOS_QEMU_SHARE")
        .map_or_else(|| PathBuf::from("/opt/homebrew/share/qemu"), PathBuf::from);
    match arch {
        UefiArch::X86_64 => Ok((
            executable("qemu-system-x86_64")?,
            env_path("AFOS_UEFI_X86_CODE")
                .unwrap_or_else(|| qemu_share.join("edk2-x86_64-code.fd")),
            env_path("AFOS_UEFI_X86_VARS").unwrap_or_else(|| qemu_share.join("edk2-i386-vars.fd")),
        )),
        UefiArch::Aarch64 => Ok((
            executable("qemu-system-aarch64")?,
            env_path("AFOS_UEFI_AARCH64_CODE")
                .unwrap_or_else(|| qemu_share.join("edk2-aarch64-code.fd")),
            env_path("AFOS_UEFI_AARCH64_VARS")
                .unwrap_or_else(|| qemu_share.join("edk2-arm-vars.fd")),
        )),
    }
}

fn env_path(name: &str) -> Option<PathBuf> {
    env::var_os(name).map(PathBuf::from)
}

fn executable(name: &str) -> Result<PathBuf> {
    let status = Command::new(name).arg("--version").status();
    if status.is_ok_and(|status| status.success()) {
        Ok(PathBuf::from(name))
    } else {
        Err(format!("{name} is not installed or not on PATH"))
    }
}

fn cargo(args: &[&str]) -> Result<()> {
    run_command(Command::new("cargo").args(args))
}

fn run_command(command: &mut Command) -> Result<()> {
    let program = command.get_program().to_string_lossy().into_owned();
    let status: ExitStatus = command
        .status()
        .map_err(|error| format!("failed to run {program}: {error}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("{program} exited with {status}"))
    }
}

fn run_command_with_timeout(command: &mut Command, timeout: Duration) -> Result<()> {
    let program = command.get_program().to_string_lossy().into_owned();
    let mut child = command
        .spawn()
        .map_err(|error| format!("failed to run {program}: {error}"))?;
    let started = Instant::now();
    loop {
        if let Some(status) = child
            .try_wait()
            .map_err(|error| format!("failed waiting for {program}: {error}"))?
        {
            return if status.success() {
                Ok(())
            } else {
                Err(format!("{program} exited with {status}"))
            };
        }
        if started.elapsed() >= timeout {
            let _ = child.kill();
            let _ = child.wait();
            return Err(format!("{program} exceeded {} seconds", timeout.as_secs()));
        }
        thread::sleep(Duration::from_millis(100));
    }
}

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("xtask must be inside the workspace")
        .to_path_buf()
}

fn display_error(error: impl std::fmt::Display) -> String {
    error.to_string()
}
