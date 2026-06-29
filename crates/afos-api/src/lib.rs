#![no_std]

extern crate alloc;

use alloc::{boxed::Box, string::String, vec::Vec};
use core::fmt;

pub type Result<T> = core::result::Result<T, Error>;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Error {
    InvalidPath,
    NotFound(String),
    AlreadyExists(String),
    NotDirectory(String),
    IsDirectory(String),
    ReadOnly(String),
    PermissionDenied(String),
    CapabilityNotDeclared(String),
    AuthenticationFailed,
    AuthenticationRequired,
    RateLimited(u64),
    InvalidInput(String),
    Unsupported(String),
    ResourceLimit(String),
    Runtime(String),
    Io(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidPath => f.write_str("invalid path"),
            Self::NotFound(path) => write!(f, "not found: {path}"),
            Self::AlreadyExists(path) => write!(f, "already exists: {path}"),
            Self::NotDirectory(path) => write!(f, "not a directory: {path}"),
            Self::IsDirectory(path) => write!(f, "is a directory: {path}"),
            Self::ReadOnly(path) => write!(f, "read-only path: {path}"),
            Self::PermissionDenied(message) => write!(f, "permission denied: {message}"),
            Self::CapabilityNotDeclared(capability) => {
                write!(f, "capability not declared: {capability}")
            }
            Self::AuthenticationFailed => f.write_str("authentication failed"),
            Self::AuthenticationRequired => f.write_str("authentication required"),
            Self::RateLimited(milliseconds) => {
                write!(f, "authentication rate limited for {milliseconds} ms")
            }
            Self::InvalidInput(message) => write!(f, "invalid input: {message}"),
            Self::Unsupported(message) => write!(f, "unsupported: {message}"),
            Self::ResourceLimit(message) => write!(f, "resource limit: {message}"),
            Self::Runtime(message) => write!(f, "runtime error: {message}"),
            Self::Io(message) => write!(f, "I/O error: {message}"),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SystemInfo {
    pub name: String,
    pub version: String,
    pub platform: String,
    pub architecture: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct StorageEntry {
    pub name: String,
    pub is_dir: bool,
    pub len: u64,
}

/// A snapshot of the platform's network interface.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NetStatus {
    pub link_up: bool,
    pub mac: String,
    /// Local address in `address/prefix` form, when one has been assigned.
    pub address: Option<String>,
    pub gateway: Option<String>,
}

pub trait Platform {
    fn console_write(&mut self, text: &str) -> Result<()>;
    fn console_read_line(&mut self, prompt: &str, secret: bool) -> Result<String>;
    fn console_clear(&mut self) -> Result<()>;

    /// Storage paths are normalized and relative to the platform's AFOS-owned root.
    fn storage_read(&mut self, path: &str) -> Result<Vec<u8>>;
    fn storage_write(&mut self, path: &str, data: &[u8]) -> Result<()>;
    fn storage_list(&mut self, path: &str) -> Result<Vec<StorageEntry>>;
    fn storage_create_dir_all(&mut self, path: &str) -> Result<()>;
    fn storage_remove(&mut self, path: &str) -> Result<()>;
    fn storage_rename(&mut self, from: &str, to: &str) -> Result<()>;
    fn storage_exists(&mut self, path: &str) -> Result<bool>;

    fn monotonic_millis(&self) -> u64;
    fn fill_random(&mut self, output: &mut [u8]) -> Result<()>;
    fn system_info(&self) -> SystemInfo;
    fn poll_cancel(&mut self) -> bool {
        false
    }

    /// Reports the network interface state. Backends without networking keep the
    /// default and report it as unavailable.
    fn net_status(&mut self) -> Result<NetStatus> {
        Err(net_unavailable())
    }

    /// Opens a TCP connection to `host` (an IPv4 literal or resolvable name) on
    /// `port`, returning an opaque connection handle.
    fn net_connect(&mut self, _host: &str, _port: u16) -> Result<u64> {
        Err(net_unavailable())
    }

    /// Writes bytes to an open connection, returning the number accepted.
    fn net_send(&mut self, _handle: u64, _data: &[u8]) -> Result<usize> {
        Err(net_unavailable())
    }

    /// Reads up to `out.len()` bytes from an open connection. A return of `0`
    /// means the peer has closed the connection.
    fn net_recv(&mut self, _handle: u64, _out: &mut [u8]) -> Result<usize> {
        Err(net_unavailable())
    }

    /// Closes an open connection. Closing an unknown handle is not an error.
    fn net_close(&mut self, _handle: u64) -> Result<()> {
        Err(net_unavailable())
    }
}

fn net_unavailable() -> Error {
    Error::Unsupported(String::from("networking is unavailable on this platform"))
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Capability {
    FsRead(String),
    FsWrite(String),
    Clock,
    SystemInfo,
    /// Outbound network access to a host pattern (`*` matches any host).
    Network(String),
}

impl fmt::Display for Capability {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::FsRead(path) => write!(f, "fs.read:{path}"),
            Self::FsWrite(path) => write!(f, "fs.write:{path}"),
            Self::Clock => f.write_str("clock"),
            Self::SystemInfo => f.write_str("system.info"),
            Self::Network(host) => write!(f, "net:{host}"),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AppIdentity {
    pub id: String,
    pub source_path: String,
    pub trusted: bool,
    pub api_version: u16,
    pub capabilities: Vec<Capability>,
}

pub trait SystemApi {
    fn app(&self) -> &AppIdentity;
    fn args(&self) -> &[String];
    fn cwd(&self) -> &str;
    fn console_write(&mut self, text: &str) -> Result<()>;
    fn console_read_line(&mut self, prompt: &str) -> Result<String>;
    fn read_text(&mut self, path: &str) -> Result<String>;
    fn write_text(&mut self, path: &str, text: &str) -> Result<()>;
    fn list(&mut self, path: &str) -> Result<Vec<StorageEntry>>;
    fn create_dir_all(&mut self, path: &str) -> Result<()>;
    fn remove(&mut self, path: &str) -> Result<()>;
    fn rename(&mut self, from: &str, to: &str) -> Result<()>;
    fn appdata_read(&mut self, path: &str) -> Result<String>;
    fn appdata_write(&mut self, path: &str, text: &str) -> Result<()>;
    fn monotonic_millis(&mut self) -> Result<u64>;
    fn system_info(&mut self) -> Result<SystemInfo>;
    fn net_status(&mut self) -> Result<NetStatus>;
    fn net_connect(&mut self, host: &str, port: u16) -> Result<u64>;
    fn net_send(&mut self, handle: u64, data: &[u8]) -> Result<usize>;
    fn net_recv(&mut self, handle: u64, max: usize) -> Result<Vec<u8>>;
    fn net_close(&mut self, handle: u64) -> Result<()>;
    fn cancelled(&mut self) -> bool;
}

pub trait AppRuntime {
    fn id(&self) -> &'static str;
    fn extension(&self) -> &'static str;
    fn execute(&self, source: &str, api: &mut dyn SystemApi) -> Result<i32>;
}

pub type BoxedRuntime = Box<dyn AppRuntime>;
