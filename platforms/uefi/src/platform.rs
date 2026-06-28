use afos_api::{Error, Platform, Result, StorageEntry, SystemInfo};
use alloc::{format, string::String, string::ToString, vec::Vec};
use core::{cell::Cell, fmt::Write};
use uefi::{
    CString16, boot,
    fs::FileSystem,
    proto::{
        console::text::{Key, ScanCode},
        rng::Rng,
    },
    runtime, system,
};

const MONTH_DAYS: [u64; 12] = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];

pub struct UefiPlatform {
    fs: FileSystem,
    started_at: u64,
    last_clock: Cell<u64>,
}

impl UefiPlatform {
    pub fn new() -> Result<Self> {
        let protocol = boot::get_image_file_system(boot::image_handle()).map_err(uefi_error)?;
        let mut fs = FileSystem::new(protocol);
        let root = to_uefi_path("")?;
        fs.create_dir_all(root.as_ref()).map_err(fs_error)?;
        let now = wall_clock_millis();
        Ok(Self {
            fs,
            started_at: now,
            last_clock: Cell::new(0),
        })
    }

    fn path(relative: &str) -> Result<CString16> {
        to_uefi_path(relative)
    }

    fn copy_tree(&mut self, from: &str, to: &str) -> Result<()> {
        let from_path = Self::path(from)?;
        let metadata = self.fs.metadata(from_path.as_ref()).map_err(fs_error)?;
        if metadata.is_directory() {
            self.storage_create_dir_all(to)?;
            for entry in self.storage_list(from)? {
                let child_from = format!("{from}/{}", entry.name);
                let child_to = format!("{to}/{}", entry.name);
                self.copy_tree(&child_from, &child_to)?;
            }
        } else {
            let data = self.storage_read(from)?;
            self.storage_write(to, &data)?;
        }
        Ok(())
    }

    fn read_key_blocking() -> Result<Key> {
        loop {
            let key = system::with_stdin(|input| {
                let event = input.wait_for_key_event().map_err(uefi_error)?;
                let mut events = [event];
                boot::wait_for_event(&mut events).map_err(uefi_error)?;
                input.read_key().map_err(uefi_error)
            })?;
            if let Some(key) = key {
                return Ok(key);
            }
        }
    }
}

impl Platform for UefiPlatform {
    fn console_write(&mut self, text: &str) -> Result<()> {
        system::with_stdout(|output| output.write_str(text))
            .map_err(|_| Error::Io(String::from("UEFI console output failed")))
    }

    fn console_read_line(&mut self, prompt: &str, secret: bool) -> Result<String> {
        self.console_write(prompt)?;
        let mut line = String::new();
        loop {
            match Self::read_key_blocking()? {
                Key::Printable(character) => {
                    let character: char = character.into();
                    match character {
                        '\r' | '\n' => {
                            self.console_write("\n")?;
                            return Ok(line);
                        }
                        '\u{8}' => {
                            if line.pop().is_some() {
                                self.console_write("\u{8} \u{8}")?;
                            }
                        }
                        '\0' => {}
                        value if !value.is_control() => {
                            line.push(value);
                            if secret {
                                self.console_write("*")?;
                            } else {
                                let mut encoded = [0_u8; 4];
                                self.console_write(value.encode_utf8(&mut encoded))?;
                            }
                        }
                        _ => {}
                    }
                }
                Key::Special(ScanCode::ESCAPE) => {
                    self.console_write("\n")?;
                    return Err(Error::Io(String::from("input cancelled")));
                }
                Key::Special(_) => {}
            }
        }
    }

    fn console_clear(&mut self) -> Result<()> {
        system::with_stdout(uefi::proto::console::text::Output::clear).map_err(uefi_error)
    }

    fn storage_read(&mut self, path: &str) -> Result<Vec<u8>> {
        let path = Self::path(path)?;
        self.fs.read(path.as_ref()).map_err(fs_error)
    }

    fn storage_write(&mut self, path: &str, data: &[u8]) -> Result<()> {
        if let Some((parent, _)) = path.rsplit_once('/') {
            self.storage_create_dir_all(parent)?;
        }
        let path = Self::path(path)?;
        self.fs.write(path.as_ref(), data).map_err(fs_error)
    }

    fn storage_list(&mut self, path: &str) -> Result<Vec<StorageEntry>> {
        let path = Self::path(path)?;
        let mut entries = Vec::new();
        for result in self.fs.read_dir(path.as_ref()).map_err(fs_error)? {
            let info = result.map_err(|error| Error::Io(format!("UEFI directory: {error}")))?;
            let name = info.file_name().to_string();
            if matches!(name.as_str(), "." | "..") {
                continue;
            }
            entries.push(StorageEntry {
                name,
                is_dir: info.is_directory(),
                len: info.file_size(),
            });
        }
        entries.sort_by(|left, right| left.name.cmp(&right.name));
        Ok(entries)
    }

    fn storage_create_dir_all(&mut self, path: &str) -> Result<()> {
        let path = Self::path(path)?;
        self.fs.create_dir_all(path.as_ref()).map_err(fs_error)
    }

    fn storage_remove(&mut self, path: &str) -> Result<()> {
        let path = Self::path(path)?;
        let metadata = self.fs.metadata(path.as_ref()).map_err(fs_error)?;
        if metadata.is_directory() {
            self.fs.remove_dir_all(path.as_ref()).map_err(fs_error)
        } else {
            self.fs.remove_file(path.as_ref()).map_err(fs_error)
        }
    }

    fn storage_rename(&mut self, from: &str, to: &str) -> Result<()> {
        self.copy_tree(from, to)?;
        self.storage_remove(from)
    }

    fn storage_exists(&mut self, path: &str) -> Result<bool> {
        let path = Self::path(path)?;
        self.fs.try_exists(path.as_ref()).map_err(fs_error)
    }

    fn monotonic_millis(&self) -> u64 {
        let raw = wall_clock_millis().saturating_sub(self.started_at);
        let value = raw.max(self.last_clock.get());
        self.last_clock.set(value);
        value
    }

    fn fill_random(&mut self, output: &mut [u8]) -> Result<()> {
        let handle = boot::get_handle_for_protocol::<Rng>().map_err(uefi_error)?;
        let mut rng = boot::open_protocol_exclusive::<Rng>(handle).map_err(uefi_error)?;
        rng.get_rng(None, output).map_err(uefi_error)
    }

    fn system_info(&self) -> SystemInfo {
        SystemInfo {
            name: String::from("AFOS"),
            version: String::from(env!("CARGO_PKG_VERSION")),
            platform: format!("UEFI {}", system::uefi_revision()),
            architecture: String::from(ARCHITECTURE),
        }
    }

    fn poll_cancel(&mut self) -> bool {
        system::with_stdin(|input| {
            matches!(input.read_key(), Ok(Some(Key::Special(ScanCode::ESCAPE))))
        })
    }
}

fn to_uefi_path(relative: &str) -> Result<CString16> {
    if relative.starts_with('/')
        || relative.contains('\\')
        || relative.split('/').any(|part| part == "..")
    {
        return Err(Error::InvalidPath);
    }
    // The high-level UEFI filesystem opens every path from the volume root.
    // A relative FAT path avoids its `create_dir_all` treating the leading
    // separator as an empty parent component.
    let mut path = String::from("AFOS");
    if !relative.is_empty() {
        path.push('\\');
        path.push_str(&relative.replace('/', "\\"));
    }
    CString16::try_from(path.as_str())
        .map_err(|_| Error::InvalidInput(String::from("path cannot be represented by UEFI")))
}

fn wall_clock_millis() -> u64 {
    let Ok(time) = runtime::get_time() else {
        return 0;
    };
    let year = u64::from(time.year());
    let mut days = 0_u64;
    for candidate in 1970..year {
        days += if is_leap(candidate) { 366 } else { 365 };
    }
    for month in 1..u64::from(time.month()) {
        days += MONTH_DAYS[usize::try_from(month - 1).unwrap_or(0)];
        if month == 2 && is_leap(year) {
            days += 1;
        }
    }
    days += u64::from(time.day().saturating_sub(1));
    let seconds = (((days * 24) + u64::from(time.hour())) * 60 + u64::from(time.minute())) * 60
        + u64::from(time.second());
    seconds
        .saturating_mul(1_000)
        .saturating_add(u64::from(time.nanosecond()) / 1_000_000)
}

const fn is_leap(year: u64) -> bool {
    year.is_multiple_of(4) && (!year.is_multiple_of(100) || year.is_multiple_of(400))
}

fn uefi_error<D: core::fmt::Debug>(error: uefi::Error<D>) -> Error {
    Error::Io(format!("UEFI status: {}", error.status()))
}

fn fs_error(error: uefi::fs::Error) -> Error {
    let message = format!("{error}");
    if message.contains("NOT_FOUND") {
        Error::NotFound(message)
    } else {
        Error::Io(message)
    }
}

#[cfg(target_arch = "x86_64")]
const ARCHITECTURE: &str = "x86_64";
#[cfg(target_arch = "aarch64")]
const ARCHITECTURE: &str = "aarch64";
