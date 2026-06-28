use afos_api::{Error, Platform, Result, StorageEntry, SystemInfo};
use alloc::{
    collections::{BTreeMap, BTreeSet},
    format,
    string::String,
    vec::Vec,
};
use limine::framebuffer::Framebuffer;

use crate::{arch, console::Console};

pub struct BareMetalPlatform {
    console: Console,
    files: BTreeMap<String, Vec<u8>>,
    directories: BTreeSet<String>,
    started: u64,
    counter_frequency: u64,
}

impl BareMetalPlatform {
    pub fn new(
        framebuffer: Option<&'static Framebuffer>,
        hhdm: u64,
        executable_address: Option<(u64, u64)>,
        counter_frequency: u64,
    ) -> Self {
        Self {
            console: Console::new(framebuffer, hhdm, executable_address),
            files: BTreeMap::new(),
            directories: BTreeSet::new(),
            started: arch::counter(),
            counter_frequency,
        }
    }

    fn parent(path: &str) -> &str {
        path.rsplit_once('/').map_or("", |(parent, _)| parent)
    }

    fn add_parents(&mut self, path: &str) -> Result<()> {
        let mut current = String::new();
        for component in path.split('/').filter(|part| !part.is_empty()) {
            if !current.is_empty() {
                current.push('/');
            }
            current.push_str(component);
            if self.files.contains_key(&current) {
                return Err(Error::NotDirectory(current));
            }
            self.directories.insert(current.clone());
        }
        Ok(())
    }

    fn exists(&self, path: &str) -> bool {
        self.files.contains_key(path) || self.directories.contains(path)
    }
}

impl Platform for BareMetalPlatform {
    fn console_write(&mut self, text: &str) -> Result<()> {
        self.console.write(text);
        Ok(())
    }

    fn console_read_line(&mut self, prompt: &str, secret: bool) -> Result<String> {
        Ok(self.console.read_line(prompt, secret))
    }

    fn console_clear(&mut self) -> Result<()> {
        self.console.clear();
        Ok(())
    }

    fn storage_read(&mut self, path: &str) -> Result<Vec<u8>> {
        if self.directories.contains(path) {
            return Err(Error::IsDirectory(String::from(path)));
        }
        self.files
            .get(path)
            .cloned()
            .ok_or_else(|| Error::NotFound(String::from(path)))
    }

    fn storage_write(&mut self, path: &str, data: &[u8]) -> Result<()> {
        if self.directories.contains(path) {
            return Err(Error::IsDirectory(String::from(path)));
        }
        self.add_parents(Self::parent(path))?;
        self.files.insert(String::from(path), data.to_vec());
        Ok(())
    }

    fn storage_list(&mut self, path: &str) -> Result<Vec<StorageEntry>> {
        if !self.directories.contains(path) {
            return if self.files.contains_key(path) {
                Err(Error::NotDirectory(String::from(path)))
            } else {
                Err(Error::NotFound(String::from(path)))
            };
        }
        let prefix = if path.is_empty() {
            String::new()
        } else {
            format!("{path}/")
        };
        let mut entries = BTreeMap::new();
        for directory in &self.directories {
            if let Some(name) = direct_child(directory, &prefix) {
                entries.insert(
                    String::from(name),
                    StorageEntry {
                        name: String::from(name),
                        is_dir: true,
                        len: 0,
                    },
                );
            }
        }
        for (file, data) in &self.files {
            if let Some(name) = direct_child(file, &prefix) {
                entries.insert(
                    String::from(name),
                    StorageEntry {
                        name: String::from(name),
                        is_dir: false,
                        len: u64::try_from(data.len()).unwrap_or(u64::MAX),
                    },
                );
            }
        }
        Ok(entries.into_values().collect())
    }

    fn storage_create_dir_all(&mut self, path: &str) -> Result<()> {
        self.add_parents(path)
    }

    fn storage_remove(&mut self, path: &str) -> Result<()> {
        if self.files.remove(path).is_some() {
            return Ok(());
        }
        if !self.directories.contains(path) {
            return Err(Error::NotFound(String::from(path)));
        }
        let prefix = format!("{path}/");
        self.files.retain(|name, _| !name.starts_with(&prefix));
        self.directories
            .retain(|name| name != path && !name.starts_with(&prefix));
        Ok(())
    }

    fn storage_rename(&mut self, from: &str, to: &str) -> Result<()> {
        if from == to {
            return Ok(());
        }
        if self.exists(to) {
            return Err(Error::AlreadyExists(String::from(to)));
        }
        if to
            .strip_prefix(from)
            .is_some_and(|suffix| suffix.starts_with('/'))
        {
            return Err(Error::InvalidPath);
        }
        self.add_parents(Self::parent(to))?;
        if let Some(data) = self.files.remove(from) {
            self.files.insert(String::from(to), data);
            return Ok(());
        }
        if !self.directories.contains(from) {
            return Err(Error::NotFound(String::from(from)));
        }

        let prefix = format!("{from}/");
        let moved_directories: Vec<String> = self
            .directories
            .iter()
            .filter(|path| *path == from || path.starts_with(&prefix))
            .cloned()
            .collect();
        let moved_files: Vec<(String, Vec<u8>)> = self
            .files
            .iter()
            .filter(|(path, _)| path.starts_with(&prefix))
            .map(|(path, data)| (path.clone(), data.clone()))
            .collect();
        for path in moved_directories {
            self.directories.remove(&path);
            self.directories.insert(rebased(&path, from, to));
        }
        for (path, data) in moved_files {
            self.files.remove(&path);
            self.files.insert(rebased(&path, from, to), data);
        }
        Ok(())
    }

    fn storage_exists(&mut self, path: &str) -> Result<bool> {
        Ok(self.exists(path))
    }

    fn monotonic_millis(&self) -> u64 {
        arch::counter()
            .saturating_sub(self.started)
            .saturating_mul(1_000)
            / self.counter_frequency
    }

    fn fill_random(&mut self, _output: &mut [u8]) -> Result<()> {
        Err(Error::Unsupported(String::from(
            "no hardware entropy driver is available on this bare-metal backend",
        )))
    }

    fn system_info(&self) -> SystemInfo {
        SystemInfo {
            name: String::from("AFOS"),
            version: String::from(env!("CARGO_PKG_VERSION")),
            platform: String::from("Limine bare metal"),
            architecture: String::from(ARCHITECTURE),
        }
    }

    fn poll_cancel(&mut self) -> bool {
        self.console.poll_cancel()
    }
}

fn direct_child<'a>(path: &'a str, prefix: &str) -> Option<&'a str> {
    let remainder = path.strip_prefix(prefix)?;
    if remainder.is_empty() || remainder.contains('/') {
        None
    } else {
        Some(remainder)
    }
}

fn rebased(path: &str, from: &str, to: &str) -> String {
    let suffix = path.strip_prefix(from).unwrap_or("");
    format!("{to}{suffix}")
}

#[cfg(target_arch = "x86_64")]
const ARCHITECTURE: &str = "x86_64";
#[cfg(target_arch = "aarch64")]
const ARCHITECTURE: &str = "aarch64";
