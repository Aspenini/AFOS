mod platform;

use afos_api::{Error, Result};
use afos_core::{Afos, CommandOutcome, EmbeddedFile, ShellConfig};
use afos_runtime_rhai::RhaiRuntime;
use platform::{DesktopPlatform, bundled_filesystem_dir, default_data_dir};
use std::{
    fs,
    path::{Path, PathBuf},
};

#[derive(Default)]
struct Cli {
    data_dir: Option<PathBuf>,
    system_dir: Option<PathBuf>,
    ephemeral: bool,
    command: Option<String>,
}

fn main() {
    match run() {
        Ok(code) => std::process::exit(code),
        Err(error) => {
            eprintln!("afos: {error}");
            std::process::exit(1);
        }
    }
}

fn run() -> Result<i32> {
    let Some(cli) = parse_args()? else {
        return Ok(0);
    };
    let (root, remove_on_drop) = if cli.ephemeral {
        let root = std::env::temp_dir().join(format!(
            "afos-ephemeral-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_or(0, |duration| duration.as_nanos())
        ));
        (root, true)
    } else {
        (cli.data_dir.unwrap_or_else(default_data_dir), false)
    };

    let system_files = load_system_files(&cli.system_dir.unwrap_or_else(default_system_dir))?;
    let platform = DesktopPlatform::new(root, remove_on_drop)?;
    let mut afos = Afos::with_system_files(platform, system_files);
    afos.register_runtime(Box::new(RhaiRuntime::new()))?;

    if let Some(command) = cli.command {
        afos.initialize(false)?;
        return match afos.run_command(&command)? {
            CommandOutcome::Continue(code) | CommandOutcome::Exit(code) => Ok(code),
        };
    }

    afos.run_interactive(&ShellConfig::default())
}

fn parse_args() -> Result<Option<Cli>> {
    let mut cli = Cli::default();
    let mut args = std::env::args().skip(1);
    while let Some(argument) = args.next() {
        match argument.as_str() {
            "--data-dir" => {
                cli.data_dir = Some(PathBuf::from(args.next().ok_or_else(|| {
                    Error::InvalidInput(String::from("--data-dir requires a path"))
                })?));
            }
            "--system-dir" => {
                cli.system_dir = Some(PathBuf::from(args.next().ok_or_else(|| {
                    Error::InvalidInput(String::from("--system-dir requires a path"))
                })?));
            }
            "--ephemeral" => cli.ephemeral = true,
            "--command" | "-c" => {
                cli.command = Some(args.next().ok_or_else(|| {
                    Error::InvalidInput(String::from("--command requires a command line"))
                })?);
            }
            "--version" | "-V" => {
                println!("AFOS {}", env!("CARGO_PKG_VERSION"));
                return Ok(None);
            }
            "--help" | "-h" => {
                println!(
                    "AFOS portable environment\n\n\
                     Usage: afos [OPTIONS]\n\n\
                     Options:\n\
                       --data-dir PATH    persistent data directory\n\
                       --system-dir PATH  read-only AFOS system files\n\
                       --ephemeral        use temporary storage\n\
                       -c, --command CMD  execute one command and exit\n\
                       -V, --version      print version\n\
                       -h, --help         print help"
                );
                return Ok(None);
            }
            unknown => {
                return Err(Error::InvalidInput(format!("unknown argument: {unknown}")));
            }
        }
    }
    if cli.ephemeral && cli.data_dir.is_some() {
        return Err(Error::InvalidInput(String::from(
            "--ephemeral conflicts with --data-dir",
        )));
    }
    Ok(Some(cli))
}

fn default_system_dir() -> PathBuf {
    if let Some(path) = std::env::var_os("AFOS_SYSTEM_DIR") {
        return PathBuf::from(path);
    }
    if let Some(filesystem) = bundled_filesystem_dir() {
        return filesystem.join("sys");
    }
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("fs")
        .join("sys")
}

fn load_system_files(root: &Path) -> Result<&'static [EmbeddedFile]> {
    let root = root.canonicalize().map_err(|error| {
        Error::Io(format!(
            "cannot open system directory {}: {error}",
            root.display()
        ))
    })?;
    let mut paths = Vec::new();
    collect_files(&root, &root, &mut paths)?;
    paths.sort();

    let mut files = Vec::with_capacity(paths.len());
    for path in paths {
        let relative = path
            .strip_prefix(&root)
            .map_err(|_| Error::InvalidPath)?
            .to_string_lossy()
            .replace('\\', "/");
        let virtual_path = format!("/sys/{relative}");
        files.push(EmbeddedFile {
            path: Box::leak(virtual_path.into_boxed_str()),
            data: Box::leak(fs::read(&path).map_err(io_error)?.into_boxed_slice()),
        });
    }
    if files.is_empty() {
        return Err(Error::InvalidInput(String::from(
            "system directory contains no files",
        )));
    }
    Ok(Box::leak(files.into_boxed_slice()))
}

fn collect_files(root: &Path, directory: &Path, output: &mut Vec<PathBuf>) -> Result<()> {
    for entry in fs::read_dir(directory).map_err(io_error)? {
        let entry = entry.map_err(io_error)?;
        let file_type = entry.file_type().map_err(io_error)?;
        if file_type.is_symlink() {
            return Err(Error::PermissionDenied(format!(
                "system file may not be a symbolic link: {}",
                entry.path().display()
            )));
        }
        if file_type.is_dir() {
            collect_files(root, &entry.path(), output)?;
        } else if file_type.is_file() {
            let canonical = entry.path().canonicalize().map_err(io_error)?;
            if !canonical.starts_with(root) {
                return Err(Error::PermissionDenied(String::from(
                    "system file escapes its root",
                )));
            }
            output.push(canonical);
        }
    }
    Ok(())
}

fn io_error(error: std::io::Error) -> Error {
    Error::Io(error.to_string())
}
