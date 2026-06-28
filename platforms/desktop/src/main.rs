mod platform;

use afos_api::{Error, Result};
use afos_core::{Afos, CommandOutcome, ShellConfig};
use afos_runtime_rhai::RhaiRuntime;
use platform::{DesktopPlatform, default_data_dir};
use std::path::PathBuf;

#[derive(Default)]
struct Cli {
    data_dir: Option<PathBuf>,
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

    let platform = DesktopPlatform::new(root, remove_on_drop)?;
    let mut afos = Afos::new(platform);
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
