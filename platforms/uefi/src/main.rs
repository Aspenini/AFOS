#![cfg_attr(target_os = "uefi", no_main)]
#![cfg_attr(target_os = "uefi", no_std)]

#[cfg(target_os = "uefi")]
extern crate alloc;

#[cfg(target_os = "uefi")]
mod platform;

#[cfg(target_os = "uefi")]
use afos_api::{Error, Result};
#[cfg(target_os = "uefi")]
use afos_core::{Afos, CommandOutcome, ShellConfig};
#[cfg(target_os = "uefi")]
use afos_runtime_rhai::RhaiRuntime;
#[cfg(target_os = "uefi")]
use alloc::boxed::Box;
#[cfg(target_os = "uefi")]
use alloc::string::ToString;
#[cfg(target_os = "uefi")]
use platform::UefiPlatform;
#[cfg(target_os = "uefi")]
use uefi::prelude::*;

#[cfg(target_os = "uefi")]
#[entry]
fn main() -> Status {
    if let Err(error) = uefi::helpers::init() {
        uefi::println!("AFOS: failed to initialize UEFI helpers: {error}");
        return error.status();
    }

    let platform = match UefiPlatform::new() {
        Ok(platform) => platform,
        Err(error) => {
            uefi::println!("AFOS: {error}");
            return Status::DEVICE_ERROR;
        }
    };
    let mut afos = Afos::new(platform);
    if let Err(error) = afos.register_runtime(Box::new(RhaiRuntime::new())) {
        uefi::println!("AFOS: {error}");
        return Status::ABORTED;
    }
    if let Err(error) = afos.initialize(false) {
        uefi::println!("AFOS: {error}");
        return Status::ABORTED;
    }
    if afos
        .system_mut()
        .vfs_mut()
        .exists("/user/config/test-commands.txt")
        .unwrap_or(false)
    {
        let status = match run_test_mode(&mut afos) {
            Ok(()) => Status::SUCCESS,
            Err(error) => {
                let _ = afos
                    .system_mut()
                    .vfs_mut()
                    .write("/user/config/test-result.txt", error.to_string().as_bytes());
                Status::ABORTED
            }
        };
        uefi::runtime::reset(uefi::runtime::ResetType::SHUTDOWN, status, None);
    }
    match afos.run_interactive(&ShellConfig::default()) {
        Ok(_) => Status::SUCCESS,
        Err(error) => {
            uefi::println!("AFOS: {error}");
            Status::ABORTED
        }
    }
}

#[cfg(target_os = "uefi")]
fn run_test_mode(afos: &mut Afos<UefiPlatform>) -> Result<()> {
    let commands = afos
        .system_mut()
        .vfs_mut()
        .read_text("/user/config/test-commands.txt")?;
    let mut report = alloc::string::String::from("AFOS-SMOKE-1\n");
    let mut passed = true;
    for command in commands
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
    {
        match afos.run_command(command) {
            Ok(CommandOutcome::Continue(0) | CommandOutcome::Exit(0)) => {
                report.push_str("OK ");
                report.push_str(command);
                report.push('\n');
            }
            Ok(CommandOutcome::Continue(code) | CommandOutcome::Exit(code)) => {
                passed = false;
                report.push_str(&alloc::format!("FAIL {code} {command}\n"));
            }
            Err(error) => {
                passed = false;
                report.push_str(&alloc::format!("ERROR {command}: {error}\n"));
            }
        }
    }
    report.push_str(if passed { "PASS\n" } else { "FAIL\n" });
    afos.system_mut()
        .vfs_mut()
        .write("/user/config/test-result.txt", report.as_bytes())?;
    afos.system_mut()
        .vfs_mut()
        .remove("/user/config/test-commands.txt")?;
    if passed {
        Ok(())
    } else {
        Err(Error::Runtime(alloc::string::String::from(
            "UEFI smoke commands failed",
        )))
    }
}

#[cfg(not(target_os = "uefi"))]
fn main() {
    eprintln!("afos-uefi must be built for a *-unknown-uefi target");
}
