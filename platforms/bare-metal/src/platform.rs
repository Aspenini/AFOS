use afos_api::{Error, NetStatus, Platform, Result, StorageEntry, SystemInfo};
use afos_storage::{BlockDevice, FileTree, SnapshotStore, StorageError};
use alloc::{boxed::Box, collections::BTreeMap, format, string::String, vec::Vec};
use limine::framebuffer::Framebuffer;

use crate::{
    arch,
    console::Console,
    devices::EntropySource,
    net::{NetDevice, NetError, NetStack},
};

/// How long a single network operation may block before timing out.
const NET_TIMEOUT_MILLIS: u64 = 30_000;

pub struct InitialFile {
    pub path: &'static str,
    pub data: &'static [u8],
}

pub struct BareMetalPlatform {
    console: Console,
    tree: FileTree,
    persistence: Option<SnapshotStore<Box<dyn BlockDevice>>>,
    entropy: Option<Box<dyn EntropySource>>,
    net: Option<NetStack>,
    started: u64,
    counter_frequency: u64,
}

impl BareMetalPlatform {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        framebuffer: Option<&'static Framebuffer>,
        hhdm: u64,
        executable_address: Option<(u64, u64)>,
        counter_frequency: u64,
        initial_files: Vec<InitialFile>,
        block: Option<Box<dyn BlockDevice>>,
        entropy: Option<Box<dyn EntropySource>>,
        net: Option<Box<dyn NetDevice>>,
    ) -> Result<Self> {
        let (persistence, restored) = if let Some(device) = block {
            let (store, restored) = SnapshotStore::open(device).map_err(storage_error)?;
            (Some(store), restored)
        } else {
            (None, None)
        };
        let net = net.map(|device| NetStack::new(device, 0, arch::counter()));
        let mut platform = Self {
            console: Console::new(framebuffer, hhdm, executable_address),
            tree: restored.unwrap_or_default(),
            persistence,
            entropy,
            net,
            started: arch::counter(),
            counter_frequency,
        };
        if platform.tree.files.is_empty() && platform.tree.directories.is_empty() {
            for file in initial_files {
                platform.add_parents(Self::parent(file.path))?;
                platform
                    .tree
                    .files
                    .insert(String::from(file.path), file.data.to_vec());
            }
            platform.persist()?;
        }
        Ok(platform)
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
            if self.tree.files.contains_key(&current) {
                return Err(Error::NotDirectory(current));
            }
            self.tree.directories.insert(current.clone());
        }
        Ok(())
    }

    fn exists(&self, path: &str) -> bool {
        self.tree.files.contains_key(path) || self.tree.directories.contains(path)
    }

    fn persist(&mut self) -> Result<()> {
        if let Some(store) = &mut self.persistence {
            store.save(&self.tree).map_err(storage_error)?;
        }
        Ok(())
    }

    fn commit(&mut self, previous: FileTree) -> Result<()> {
        if let Err(error) = self.persist() {
            self.tree = previous;
            return Err(error);
        }
        Ok(())
    }

    /// Runs `operation` against the network stack with a monotonic clock and an
    /// Escape-driven cancellation probe. Borrows of the disjoint `net` and
    /// `console` fields are split so both closures stay live during the call.
    fn with_net<R>(
        &mut self,
        operation: impl FnOnce(&mut NetStack, &mut dyn FnMut() -> u64, &mut dyn FnMut() -> bool) -> R,
    ) -> Option<R> {
        let started = self.started;
        let frequency = self.counter_frequency;
        let console = &mut self.console;
        let stack = self.net.as_mut()?;
        let mut clock = || {
            arch::counter()
                .saturating_sub(started)
                .saturating_mul(1_000)
                / frequency
        };
        let mut cancel = || console.poll_cancel();
        Some(operation(stack, &mut clock, &mut cancel))
    }
}

fn net_unavailable() -> Error {
    Error::Unsupported(String::from("no network device is available"))
}

fn map_net_error(error: NetError) -> Error {
    match error {
        NetError::NoLease => Error::Io(String::from("no DHCP lease was acquired")),
        NetError::NameResolution(message) => {
            Error::Io(format!("name resolution failed: {message}"))
        }
        NetError::Timeout(message) | NetError::Socket(message) => Error::Io(message),
        NetError::Cancelled => Error::Io(String::from("network operation cancelled")),
        NetError::Closed => Error::Io(String::from("connection is closed")),
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
        if self.tree.directories.contains(path) {
            return Err(Error::IsDirectory(String::from(path)));
        }
        self.tree
            .files
            .get(path)
            .cloned()
            .ok_or_else(|| Error::NotFound(String::from(path)))
    }

    fn storage_write(&mut self, path: &str, data: &[u8]) -> Result<()> {
        if self.tree.directories.contains(path) {
            return Err(Error::IsDirectory(String::from(path)));
        }
        let previous = self.tree.clone();
        self.add_parents(Self::parent(path))?;
        self.tree.files.insert(String::from(path), data.to_vec());
        self.commit(previous)
    }

    fn storage_list(&mut self, path: &str) -> Result<Vec<StorageEntry>> {
        if !self.tree.directories.contains(path) {
            return if self.tree.files.contains_key(path) {
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
        for directory in &self.tree.directories {
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
        for (file, data) in &self.tree.files {
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
        let previous = self.tree.clone();
        self.add_parents(path)?;
        self.commit(previous)
    }

    fn storage_remove(&mut self, path: &str) -> Result<()> {
        let previous = self.tree.clone();
        if self.tree.files.remove(path).is_some() {
            return self.commit(previous);
        }
        if !self.tree.directories.contains(path) {
            return Err(Error::NotFound(String::from(path)));
        }
        let prefix = format!("{path}/");
        self.tree.files.retain(|name, _| !name.starts_with(&prefix));
        self.tree
            .directories
            .retain(|name| name != path && !name.starts_with(&prefix));
        self.commit(previous)
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
        let previous = self.tree.clone();
        self.add_parents(Self::parent(to))?;
        if let Some(data) = self.tree.files.remove(from) {
            self.tree.files.insert(String::from(to), data);
            return self.commit(previous);
        }
        if !self.tree.directories.contains(from) {
            return Err(Error::NotFound(String::from(from)));
        }

        let prefix = format!("{from}/");
        let moved_directories: Vec<String> = self
            .tree
            .directories
            .iter()
            .filter(|path| *path == from || path.starts_with(&prefix))
            .cloned()
            .collect();
        let moved_files: Vec<(String, Vec<u8>)> = self
            .tree
            .files
            .iter()
            .filter(|(path, _)| path.starts_with(&prefix))
            .map(|(path, data)| (path.clone(), data.clone()))
            .collect();
        for path in moved_directories {
            self.tree.directories.remove(&path);
            self.tree.directories.insert(rebased(&path, from, to));
        }
        for (path, data) in moved_files {
            self.tree.files.remove(&path);
            self.tree.files.insert(rebased(&path, from, to), data);
        }
        self.commit(previous)
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

    fn fill_random(&mut self, output: &mut [u8]) -> Result<()> {
        if let Some(entropy) = &mut self.entropy {
            return entropy.fill(output).map_err(Error::Io);
        }
        if arch::fill_hardware_random(output) {
            return Ok(());
        }
        Err(Error::Unsupported(String::from(
            "no cryptographically secure entropy device is available",
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

    fn net_status(&mut self) -> Result<NetStatus> {
        self.with_net(|stack, clock, cancel| {
            // Give DHCP a brief window to acquire a lease before reporting.
            let deadline = clock().saturating_add(5_000);
            while !stack.is_configured() {
                stack.poll(clock());
                if stack.is_configured() || cancel() || clock() >= deadline {
                    break;
                }
            }
            stack.status()
        })
        .ok_or_else(net_unavailable)
    }

    fn net_connect(&mut self, host: &str, port: u16) -> Result<u64> {
        self.with_net(|stack, clock, cancel| {
            stack.connect(host, port, NET_TIMEOUT_MILLIS, clock, cancel)
        })
        .ok_or_else(net_unavailable)?
        .map_err(map_net_error)
    }

    fn net_send(&mut self, handle: u64, data: &[u8]) -> Result<usize> {
        self.with_net(|stack, clock, cancel| stack.send(handle, data, clock, cancel))
            .ok_or_else(net_unavailable)?
            .map_err(map_net_error)
    }

    fn net_recv(&mut self, handle: u64, out: &mut [u8]) -> Result<usize> {
        self.with_net(|stack, clock, cancel| stack.recv(handle, out, clock, cancel))
            .ok_or_else(net_unavailable)?
            .map_err(map_net_error)
    }

    fn net_close(&mut self, handle: u64) -> Result<()> {
        self.with_net(|stack, clock, _cancel| stack.close(handle, clock))
            .ok_or_else(net_unavailable)
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

fn storage_error(error: StorageError) -> Error {
    Error::Io(format!("persistent storage: {error}"))
}

#[cfg(target_arch = "x86_64")]
const ARCHITECTURE: &str = "x86_64";
#[cfg(target_arch = "aarch64")]
const ARCHITECTURE: &str = "aarch64";
