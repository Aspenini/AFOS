use sha2::{Digest, Sha256};
use std::{
    collections::BTreeSet,
    env,
    fmt::Write as _,
    fs,
    io::Read,
    path::{Path, PathBuf},
    process::{Child, Command, ExitStatus, Stdio},
    thread,
    time::{Duration, Instant},
};

type Result<T> = std::result::Result<T, String>;

const LIMINE_VERSION: &str = "12.3.3";
const LIMINE_SHA256: &str = "205f98218bb0d5a8ccabf5f903dba9d935f7b0aa66f4262a99b0f5a8e668ec6d";

#[derive(Clone, Copy)]
enum BareMetalArch {
    X86_64,
    Aarch64,
}

impl BareMetalArch {
    const fn target(self) -> &'static str {
        match self {
            Self::X86_64 => "x86_64-unknown-none",
            Self::Aarch64 => "aarch64-unknown-none-softfloat",
        }
    }

    const fn label(self) -> &'static str {
        match self {
            Self::X86_64 => "x86_64",
            Self::Aarch64 => "aarch64",
        }
    }

    const fn qemu(self) -> &'static str {
        match self {
            Self::X86_64 => "qemu-system-x86_64",
            Self::Aarch64 => "qemu-system-aarch64",
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
                package_desktop()?;
                package(BareMetalArch::X86_64)?;
                package(BareMetalArch::Aarch64)
            }
            Some("desktop") => package_desktop(),
            Some("x86_64" | "bare-x86_64") => package(BareMetalArch::X86_64),
            Some("aarch64" | "arm64" | "bare-aarch64") => package(BareMetalArch::Aarch64),
            Some(target) => Err(format!("unknown build target: {target}")),
        },
        Some("package") => package(parse_arch(args.next().as_deref())?),
        Some("run") => run_qemu(parse_arch(args.next().as_deref())?),
        Some("smoke") => smoke(parse_arch(args.next().as_deref())?),
        Some("check") => check_all(),
        Some("help") | None => {
            println!(
                "AFOS build utility\n\n\
                 cargo xtask build [all|desktop|x86_64|aarch64]\n\
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

fn package_desktop() -> Result<()> {
    cargo(&["build", "--release", "-p", "afos-desktop"])?;
    let root = workspace_root();
    let package = root.join("dist").join("desktop");
    recreate_directory(&package)?;
    let executable_name = format!("afos{}", env::consts::EXE_SUFFIX);
    copy(
        &root.join("target").join("release").join(&executable_name),
        &package.join(executable_name),
    )?;
    copy_directory(
        &root.join("assets").join("sys"),
        &package.join("share").join("afos").join("sys"),
    )?;
    println!("desktop {}", package.display());
    Ok(())
}

fn parse_arch(value: Option<&str>) -> Result<BareMetalArch> {
    match value {
        Some("x86_64") => Ok(BareMetalArch::X86_64),
        Some("aarch64" | "arm64") => Ok(BareMetalArch::Aarch64),
        _ => Err(String::from("expected architecture x86_64 or aarch64")),
    }
}

fn package(arch: BareMetalArch) -> Result<()> {
    executable("xorriso")?;
    let limine = ensure_limine()?;
    cargo(&[
        "build",
        "--release",
        "-p",
        "afos-kernel",
        "--target",
        arch.target(),
    ])?;

    let root = workspace_root();
    let dist = root.join("dist");
    fs::create_dir_all(&dist).map_err(display_error)?;
    let source = root
        .join("target")
        .join(arch.target())
        .join("release")
        .join("afos-kernel");
    let kernel = dist.join(format!("afos-{}.elf", arch.label()));
    copy(&source, &kernel)?;

    let system_image = dist.join("system.tar");
    build_system_image(&system_image)?;

    let staging = root
        .join("target")
        .join(format!("afos-{}-iso", arch.label()));
    recreate_directory(&staging)?;
    let boot = staging.join("boot");
    let limine_boot = boot.join("limine");
    fs::create_dir_all(&limine_boot).map_err(display_error)?;

    copy(&kernel, &boot.join("afos.elf"))?;
    copy(&system_image, &boot.join("system.tar"))?;
    if matches!(arch, BareMetalArch::X86_64) {
        for file in [
            "limine-bios-cd.bin",
            "limine-uefi-cd.bin",
            "limine-bios.sys",
        ] {
            copy(&limine.join(file), &limine_boot.join(file))?;
        }
    }
    fs::write(staging.join("limine.conf"), limine_config()).map_err(display_error)?;

    let iso = dist.join(format!("afos-{}.iso", arch.label()));
    if iso.exists() {
        fs::remove_file(&iso).map_err(display_error)?;
    }
    let mut xorriso = Command::new("xorriso");
    xorriso.args(["-as", "mkisofs", "-R", "-r", "-J"]);
    match arch {
        BareMetalArch::X86_64 => {
            xorriso.args([
                "-b",
                "boot/limine/limine-bios-cd.bin",
                "-no-emul-boot",
                "-boot-load-size",
                "4",
                "-boot-info-table",
                "-hfsplus",
                "-apm-block-size",
                "2048",
                "--efi-boot",
                "boot/limine/limine-uefi-cd.bin",
            ]);
        }
        BareMetalArch::Aarch64 => {
            executable("mformat")?;
            executable("mmd")?;
            executable("mcopy")?;
            let boot_image = limine_boot.join("limine-aarch64-cd.bin");
            create_aarch64_boot_image(&boot_image, &limine.join("BOOTAA64.EFI"))?;
            xorriso.args(["--efi-boot", "boot/limine/limine-aarch64-cd.bin"]);
        }
    }
    xorriso
        .args([
            "-efi-boot-part",
            "--efi-boot-image",
            "--protective-msdos-label",
        ])
        .arg(&staging)
        .args(["-o"])
        .arg(&iso);
    run_command(&mut xorriso)?;
    if matches!(arch, BareMetalArch::X86_64) {
        run_command(
            Command::new(limine.join("limine"))
                .args(["bios-install"])
                .arg(&iso),
        )?;
    }

    println!("kernel  {}", kernel.display());
    println!("system  {}", system_image.display());
    println!("iso     {}", iso.display());
    Ok(())
}

fn create_aarch64_boot_image(image: &Path, bootloader: &Path) -> Result<()> {
    let file = fs::File::create(image).map_err(display_error)?;
    file.set_len(4 * 1024 * 1024).map_err(display_error)?;
    run_command(
        Command::new("mformat")
            .args(["-i"])
            .arg(image)
            .args(["-v", "AFOS_BOOT", "::"]),
    )?;
    for directory in ["::/EFI", "::/EFI/BOOT"] {
        run_command(Command::new("mmd").args(["-i"]).arg(image).arg(directory))?;
    }
    run_command(
        Command::new("mcopy")
            .args(["-i"])
            .arg(image)
            .arg(bootloader)
            .arg("::/EFI/BOOT/BOOTAA64.EFI"),
    )
}

fn limine_config() -> &'static str {
    "timeout: 0\n\
     quiet: yes\n\
     serial: yes\n\
     interface_branding: AFOS\n\
     \n\
     /AFOS\n\
     protocol: limine\n\
     path: boot():/boot/afos.elf\n\
     module_path: boot():/boot/system.tar\n\
     module_string: afos-system\n"
}

fn ensure_limine() -> Result<PathBuf> {
    let root = workspace_root();
    let cache = root.join("target").join(format!("limine-{LIMINE_VERSION}"));
    let tool = cache.join("limine");
    if tool.is_file() {
        return Ok(cache);
    }

    executable("curl")?;
    executable("tar")?;
    executable("make")?;
    recreate_directory(&cache)?;
    let archive = root
        .join("target")
        .join(format!("limine-binary-{LIMINE_VERSION}.tar.gz"));
    let url = format!(
        "https://github.com/Limine-Bootloader/Limine/releases/download/v{LIMINE_VERSION}/limine-binary.tar.gz"
    );
    run_command(
        Command::new("curl")
            .args(["-fL", "--retry", "3", "-o"])
            .arg(&archive)
            .arg(url),
    )?;
    verify_sha256(&archive, LIMINE_SHA256)?;
    run_command(
        Command::new("tar")
            .args(["-xzf"])
            .arg(&archive)
            .args(["-C"])
            .arg(&cache)
            .arg("--strip-components=1"),
    )?;
    run_command(Command::new("make").arg("-C").arg(&cache))?;
    if !tool.is_file() {
        return Err(String::from("Limine host tool was not built"));
    }
    Ok(cache)
}

fn verify_sha256(path: &Path, expected: &str) -> Result<()> {
    let mut file = fs::File::open(path).map_err(display_error)?;
    let mut hasher = Sha256::new();
    let mut buffer = vec![0_u8; 64 * 1024];
    loop {
        let count = file.read(&mut buffer).map_err(display_error)?;
        if count == 0 {
            break;
        }
        hasher.update(&buffer[..count]);
    }
    let digest = hasher.finalize();
    let mut actual = String::with_capacity(digest.len() * 2);
    for byte in digest {
        write!(&mut actual, "{byte:02x}").map_err(display_error)?;
    }
    if actual == expected {
        Ok(())
    } else {
        Err(format!(
            "Limine archive checksum mismatch: expected {expected}, got {actual}"
        ))
    }
}

fn build_system_image(destination: &Path) -> Result<()> {
    let root = workspace_root().join("assets").join("sys");
    let mut files = Vec::new();
    collect_files(&root, &mut files)?;
    files.sort();

    let output = fs::File::create(destination).map_err(display_error)?;
    let mut archive = tar::Builder::new(output);
    archive.mode(tar::HeaderMode::Deterministic);
    for path in files {
        let relative = path.strip_prefix(&root).map_err(display_error)?;
        let archive_path = Path::new("sys").join(relative);
        let data = fs::read(&path).map_err(display_error)?;
        let mut header = tar::Header::new_ustar();
        header.set_size(u64::try_from(data.len()).map_err(display_error)?);
        header.set_mode(0o444);
        header.set_uid(0);
        header.set_gid(0);
        header.set_mtime(0);
        header.set_cksum();
        archive
            .append_data(&mut header, archive_path, data.as_slice())
            .map_err(display_error)?;
    }
    archive.finish().map_err(display_error)
}

fn collect_files(directory: &Path, files: &mut Vec<PathBuf>) -> Result<()> {
    for entry in fs::read_dir(directory).map_err(display_error)? {
        let entry = entry.map_err(display_error)?;
        let file_type = entry.file_type().map_err(display_error)?;
        if file_type.is_symlink() {
            return Err(format!(
                "system image source may not contain symlinks: {}",
                entry.path().display()
            ));
        }
        if file_type.is_dir() {
            collect_files(&entry.path(), files)?;
        } else if file_type.is_file() {
            files.push(entry.path());
        }
    }
    Ok(())
}

fn copy_directory(source: &Path, destination: &Path) -> Result<()> {
    fs::create_dir_all(destination).map_err(display_error)?;
    for entry in fs::read_dir(source).map_err(display_error)? {
        let entry = entry.map_err(display_error)?;
        let file_type = entry.file_type().map_err(display_error)?;
        let target = destination.join(entry.file_name());
        if file_type.is_symlink() {
            return Err(format!(
                "package source may not contain symlinks: {}",
                entry.path().display()
            ));
        }
        if file_type.is_dir() {
            copy_directory(&entry.path(), &target)?;
        } else if file_type.is_file() {
            copy(&entry.path(), &target)?;
        }
    }
    Ok(())
}

fn run_qemu(arch: BareMetalArch) -> Result<()> {
    package(arch)?;
    let mut command = qemu_command(arch, false)?;
    run_command(&mut command)
}

fn smoke(arch: BareMetalArch) -> Result<()> {
    package(arch)?;
    let root = workspace_root();
    let log = root
        .join("target")
        .join(format!("afos-{}-smoke.log", arch.label()));
    if log.exists() {
        fs::remove_file(&log).map_err(display_error)?;
    }
    let log_file = fs::File::create(&log).map_err(display_error)?;
    let mut command = qemu_command(arch, true)?;
    command
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .args(["-serial"])
        .arg(format!("file:{}", log.display()));
    let mut child = command.spawn().map_err(display_error)?;
    let result = wait_for_log(&mut child, &log, "AFOS:/user$ ", Duration::from_secs(45));
    terminate(&mut child);
    result?;
    println!("{} Limine boot smoke test passed", arch.label());
    drop(log_file);
    Ok(())
}

fn wait_for_log(child: &mut Child, log: &Path, needle: &str, timeout: Duration) -> Result<()> {
    let started = Instant::now();
    loop {
        if let Ok(contents) = fs::read_to_string(log)
            && contents.contains(needle)
        {
            return Ok(());
        }
        if let Some(status) = child.try_wait().map_err(display_error)? {
            return Err(format!("QEMU exited before AFOS started: {status}"));
        }
        if started.elapsed() >= timeout {
            let output = fs::read_to_string(log).unwrap_or_default();
            return Err(format!(
                "AFOS did not reach its shell within {} seconds:\n{output}",
                timeout.as_secs()
            ));
        }
        thread::sleep(Duration::from_millis(100));
    }
}

fn terminate(child: &mut Child) {
    let _ = child.kill();
    let _ = child.wait();
}

fn qemu_command(arch: BareMetalArch, headless: bool) -> Result<Command> {
    let root = workspace_root();
    let iso = root.join("dist").join(format!("afos-{}.iso", arch.label()));
    let mut command = Command::new(executable(arch.qemu())?);
    match arch {
        BareMetalArch::X86_64 => {
            command
                .args(["-machine", "q35", "-m", "256M", "-cdrom"])
                .arg(iso);
        }
        BareMetalArch::Aarch64 => {
            let share = env::var_os("AFOS_QEMU_SHARE")
                .map_or_else(|| PathBuf::from("/opt/homebrew/share/qemu"), PathBuf::from);
            let code =
                env_path("AFOS_AARCH64_CODE").unwrap_or_else(|| share.join("edk2-aarch64-code.fd"));
            let vars =
                env_path("AFOS_AARCH64_VARS").unwrap_or_else(|| share.join("edk2-arm-vars.fd"));
            let vars_copy = root.join("target").join("afos-aarch64-vars.fd");
            copy(&vars, &vars_copy)?;
            command
                .args(["-machine", "virt", "-cpu", "cortex-a72", "-m", "512M"])
                .args(["-device", "ramfb"])
                .arg("-drive")
                .arg(format!(
                    "if=pflash,format=raw,readonly=on,file={}",
                    code.display()
                ))
                .arg("-drive")
                .arg(format!("if=pflash,format=raw,file={}", vars_copy.display()))
                .arg("-drive")
                .arg(format!(
                    "if=none,id=cdrom,media=cdrom,readonly=on,file={}",
                    iso.display()
                ))
                .args([
                    "-device",
                    "virtio-scsi-pci",
                    "-device",
                    "scsi-cd,drive=cdrom",
                ]);
        }
    }
    if headless {
        command.args(["-display", "none"]);
    } else {
        command.args(["-serial", "stdio"]);
    }
    command.arg("-no-reboot");
    Ok(command)
}

fn check_all() -> Result<()> {
    check_docs()?;
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
    for target in ["x86_64-unknown-none", "aarch64-unknown-none-softfloat"] {
        cargo(&[
            "clippy",
            "-p",
            "afos-kernel",
            "--target",
            target,
            "--",
            "-D",
            "warnings",
        ])?;
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

fn check_docs() -> Result<()> {
    let docs_dir = workspace_root().join("docs");
    let mut pages = vec![docs_dir.join("index.md"), docs_dir.join("404.md")];
    for entry in fs::read_dir(&docs_dir).map_err(display_error)? {
        let path = entry.map_err(display_error)?.path();
        if path.extension().is_some_and(|extension| extension == "md")
            && !matches!(
                path.file_name().and_then(|name| name.to_str()),
                Some("index.md" | "404.md")
            )
        {
            pages.push(path);
        }
    }

    let mut permalinks = BTreeSet::new();
    let mut contents = Vec::new();
    for page in pages {
        let content = fs::read_to_string(&page).map_err(display_error)?;
        let front_matter = content
            .strip_prefix("---\n")
            .and_then(|rest| rest.split_once("\n---\n").map(|(front, _)| front))
            .ok_or_else(|| format!("{} has no valid YAML front matter", page.display()))?;
        if !front_matter.lines().any(|line| line.starts_with("title:")) {
            return Err(format!("{} has no title", page.display()));
        }
        let permalink = front_matter
            .lines()
            .find_map(|line| line.strip_prefix("permalink:").map(str::trim))
            .ok_or_else(|| format!("{} has no permalink", page.display()))?;
        if !permalinks.insert(permalink.to_owned()) {
            return Err(format!("duplicate documentation permalink: {permalink}"));
        }
        if page.file_name().is_some_and(|name| name != "404.md")
            && !content.contains("{% include nav.md %}")
        {
            return Err(format!(
                "{} does not include site navigation",
                page.display()
            ));
        }
        contents.push((page, content));
    }

    for (page, content) in contents {
        let mut remainder = content.as_str();
        while let Some(start) = remainder.find("{{ '") {
            remainder = &remainder[start + 4..];
            let Some(end) = remainder.find("' | relative_url }}") else {
                return Err(format!("{} has a malformed relative_url", page.display()));
            };
            let target = &remainder[..end];
            if !permalinks.contains(target) {
                return Err(format!(
                    "{} links to undocumented permalink {target}",
                    page.display()
                ));
            }
            remainder = &remainder[end + 19..];
        }
    }
    Ok(())
}

fn recreate_directory(path: &Path) -> Result<()> {
    if path.exists() {
        fs::remove_dir_all(path).map_err(display_error)?;
    }
    fs::create_dir_all(path).map_err(display_error)
}

fn copy(source: &Path, destination: &Path) -> Result<()> {
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent).map_err(display_error)?;
    }
    fs::copy(source, destination).map(|_| ()).map_err(|error| {
        format!(
            "failed to copy {} to {}: {error}",
            source.display(),
            destination.display()
        )
    })
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

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("xtask must be inside the workspace")
        .to_path_buf()
}

fn display_error(error: impl std::fmt::Display) -> String {
    error.to_string()
}
