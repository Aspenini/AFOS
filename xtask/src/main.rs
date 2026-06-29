use sha2::{Digest, Sha256};
use std::{
    collections::BTreeSet,
    env,
    fmt::Write as _,
    fs,
    io::{Read, Write},
    path::{Path, PathBuf},
    process::{Child, Command, ExitStatus, Stdio},
    thread,
    time::{Duration, Instant},
};

type Result<T> = std::result::Result<T, String>;

const LIMINE_VERSION: &str = "12.3.3";
const LIMINE_SHA256: &str = "205f98218bb0d5a8ccabf5f903dba9d935f7b0aa66f4262a99b0f5a8e668ec6d";
const DATA_DISK_SIZE: u64 = 32 * 1024 * 1024;

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
        &package.join("boot").join(executable_name),
    )?;
    prepare_bundle_filesystem(&package)?;
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
    let package = root.join("dist").join(arch.label());
    fs::create_dir_all(&package).map_err(display_error)?;
    recreate_directory(&package.join("boot"))?;
    recreate_directory(&package.join("fs"))?;
    let source = root
        .join("target")
        .join(arch.target())
        .join("release")
        .join("afos-kernel");
    let kernel = package.join("boot").join("afos.elf");
    copy(&source, &kernel)?;
    prepare_bundle_filesystem(&package)?;

    let staging = root
        .join("target")
        .join(format!("afos-{}-iso", arch.label()));
    recreate_directory(&staging)?;
    copy_directory(&package.join("boot"), &staging.join("boot"))?;
    copy_directory(&package.join("fs"), &staging.join("fs"))?;
    let boot = staging.join("boot");
    let limine_boot = boot.join("limine");
    fs::create_dir_all(&limine_boot).map_err(display_error)?;

    let filesystem_files = bundle_files(&staging.join("fs"))?;
    fs::write(
        staging.join("limine.conf"),
        limine_config(&filesystem_files),
    )
    .map_err(display_error)?;

    let iso = package.join("afos.iso");
    if iso.exists() {
        fs::remove_file(&iso).map_err(display_error)?;
    }
    executable("mformat")?;
    executable("mmd")?;
    executable("mcopy")?;
    let (bootloader_name, boot_image_name) = match arch {
        BareMetalArch::X86_64 => ("BOOTX64.EFI", "limine-x86_64-cd.bin"),
        BareMetalArch::Aarch64 => ("BOOTAA64.EFI", "limine-aarch64-cd.bin"),
    };
    let bootloader = limine.join(bootloader_name);
    let boot_image = limine_boot.join(boot_image_name);
    create_uefi_boot_image(&boot_image, &bootloader, bootloader_name)?;
    copy(
        &bootloader,
        &staging.join("EFI").join("BOOT").join(bootloader_name),
    )?;

    let mut xorriso = Command::new("xorriso");
    xorriso
        .args(["-as", "mkisofs", "-R", "-r", "-J", "--efi-boot"])
        .arg(format!("boot/limine/{boot_image_name}"));
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
    let data_disk = package.join("afos-data.img");
    ensure_data_disk(&data_disk)?;

    println!("bundle  {}", package.display());
    println!("kernel  {}", kernel.display());
    println!("iso     {}", iso.display());
    println!("data    {}", data_disk.display());
    Ok(())
}

fn ensure_data_disk(path: &Path) -> Result<()> {
    if path.is_file() {
        return Ok(());
    }
    let file = fs::File::create(path).map_err(display_error)?;
    file.set_len(DATA_DISK_SIZE).map_err(display_error)
}

fn create_uefi_boot_image(image: &Path, bootloader: &Path, boot_name: &str) -> Result<()> {
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
            .arg(format!("::/EFI/BOOT/{boot_name}")),
    )
}

fn limine_config(files: &[String]) -> String {
    let mut config = String::from(
        "timeout: 0\n\
         quiet: yes\n\
         serial: yes\n\
         interface_branding: AFOS\n\
         \n\
         /AFOS\n\
         protocol: limine\n\
         path: boot():/boot/afos.elf\n",
    );
    for file in files {
        let virtual_path = file.strip_prefix("fs").unwrap_or(file);
        writeln!(&mut config, "module_path: boot():/{file}").expect("writing to String");
        writeln!(&mut config, "module_string: afos-file:{virtual_path}")
            .expect("writing to String");
    }
    config
}

fn ensure_limine() -> Result<PathBuf> {
    let root = workspace_root();
    let cache = root.join("target").join(format!("limine-{LIMINE_VERSION}"));
    if cache.join("BOOTX64.EFI").is_file() && cache.join("BOOTAA64.EFI").is_file() {
        return Ok(cache);
    }

    executable("curl")?;
    executable("tar")?;
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
    if !cache.join("BOOTX64.EFI").is_file() || !cache.join("BOOTAA64.EFI").is_file() {
        return Err(String::from("Limine UEFI bootloaders were not extracted"));
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

fn bundle_files(root: &Path) -> Result<Vec<String>> {
    let mut files = Vec::new();
    collect_files(root, &mut files)?;
    files.sort();
    let mut names = Vec::with_capacity(files.len());
    for path in files {
        let relative = path.strip_prefix(root).map_err(display_error)?;
        let relative = relative
            .to_str()
            .ok_or_else(|| format!("non-UTF-8 filesystem path: {}", path.display()))?
            .replace('\\', "/");
        if relative.contains(['\n', '\r', '\0']) {
            return Err(format!("unsupported filesystem path: {}", path.display()));
        }
        names.push(format!("fs/{relative}"));
    }
    Ok(names)
}

fn prepare_bundle_filesystem(package: &Path) -> Result<()> {
    let filesystem = package.join("fs");
    copy_directory(&workspace_root().join("fs"), &filesystem)?;
    for directory in [
        filesystem.join("apps"),
        filesystem.join("user").join("config"),
        filesystem.join("user").join("saves"),
        filesystem.join("user").join("appdata"),
    ] {
        fs::create_dir_all(directory).map_err(display_error)?;
    }
    Ok(())
}

fn collect_files(directory: &Path, files: &mut Vec<PathBuf>) -> Result<()> {
    for entry in fs::read_dir(directory).map_err(display_error)? {
        let entry = entry.map_err(display_error)?;
        if entry.file_name() == ".gitkeep" {
            continue;
        }
        let file_type = entry.file_type().map_err(display_error)?;
        if file_type.is_symlink() {
            return Err(format!(
                "filesystem source may not contain symlinks: {}",
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
        if entry.file_name() == ".gitkeep" {
            continue;
        }
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
    smoke_boot(
        arch,
        &log,
        "hello\n",
        &["Hello from a portable AFOS Rhai application!"],
    )?;
    println!("{} Limine boot and shell-command test passed", arch.label());
    Ok(())
}

fn smoke_boot(arch: BareMetalArch, log: &Path, input: &str, expected: &[&str]) -> Result<()> {
    if log.exists() {
        fs::remove_file(log).map_err(display_error)?;
    }
    let output = fs::File::create(log).map_err(display_error)?;
    let errors = output.try_clone().map_err(display_error)?;
    let mut command = qemu_command(arch, true)?;
    command
        .args(["-serial", "stdio"])
        .stdin(Stdio::piped())
        .stdout(Stdio::from(output))
        .stderr(Stdio::from(errors));
    let mut child = command.spawn().map_err(display_error)?;
    let result = (|| {
        wait_for_log(&mut child, log, &["AFOS:/user$ "], Duration::from_secs(60))?;
        child
            .stdin
            .as_mut()
            .ok_or_else(|| String::from("QEMU stdin pipe is unavailable"))?
            .write_all(input.as_bytes())
            .map_err(display_error)?;
        wait_for_log(&mut child, log, expected, Duration::from_secs(120))
    })();
    terminate(&mut child);
    result
}

fn wait_for_log(child: &mut Child, log: &Path, needles: &[&str], timeout: Duration) -> Result<()> {
    let started = Instant::now();
    loop {
        if let Ok(contents) = fs::read_to_string(log)
            && needles.iter().all(|needle| contents.contains(needle))
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
    let iso = root.join("dist").join(arch.label()).join("afos.iso");
    let mut command = Command::new(executable(arch.qemu())?);
    match arch {
        BareMetalArch::X86_64 => {
            let share = env::var_os("AFOS_QEMU_SHARE")
                .map_or_else(|| PathBuf::from("/opt/homebrew/share/qemu"), PathBuf::from);
            let code =
                env_path("AFOS_X86_64_CODE").unwrap_or_else(|| share.join("edk2-x86_64-code.fd"));
            let vars =
                env_path("AFOS_X86_64_VARS").unwrap_or_else(|| share.join("edk2-i386-vars.fd"));
            let vars_copy = root.join("target").join("afos-x86_64-vars.fd");
            copy(&vars, &vars_copy)?;
            let data_disk = root.join("dist").join("x86_64").join("afos-data.img");
            command
                .args(["-machine", "q35", "-m", "256M", "-cdrom"])
                .arg(iso)
                .arg("-drive")
                .arg(format!(
                    "if=pflash,format=raw,readonly=on,file={}",
                    code.display()
                ))
                .arg("-drive")
                .arg(format!("if=pflash,format=raw,file={}", vars_copy.display()))
                // AFOS persistent data and NIC. The kernel speaks the transitional
                // (legacy I/O BAR) VirtIO interface, so force that on q35. Legacy
                // virtqueues are fixed-size, so the device ring must match the
                // kernel's 16-entry layout (see VIRTIO_QUEUE_SIZE).
                .arg("-drive")
                .arg(format!(
                    "if=none,id=afos-data,format=raw,file={}",
                    data_disk.display()
                ))
                .args([
                    "-device",
                    "virtio-blk-pci,drive=afos-data,disable-modern=on,disable-legacy=off,queue-size=16",
                    "-netdev",
                    "user,id=afos-net",
                    "-device",
                    "virtio-net-pci,netdev=afos-net,disable-modern=on,disable-legacy=off",
                ]);
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
            let data_disk = root.join("dist").join("aarch64").join("afos-data.img");
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
                ])
                // AFOS persistent data and NIC on the virt machine's VirtIO-MMIO
                // transports, which the kernel scans at 0x0a000000.
                .arg("-drive")
                .arg(format!(
                    "if=none,id=afos-data,format=raw,file={}",
                    data_disk.display()
                ))
                .args([
                    "-device",
                    "virtio-blk-device,drive=afos-data,queue-size=16",
                    "-netdev",
                    "user,id=afos-net",
                    "-device",
                    "virtio-net-device,netdev=afos-net",
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
            "--all-features",
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
        "-p",
        "afos-storage",
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
