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
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Capability {
    FsRead(String),
    FsWrite(String),
    Clock,
    SystemInfo,
}

impl fmt::Display for Capability {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::FsRead(path) => write!(f, "fs.read:{path}"),
            Self::FsWrite(path) => write!(f, "fs.write:{path}"),
            Self::Clock => f.write_str("clock"),
            Self::SystemInfo => f.write_str("system.info"),
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
    fn cancelled(&mut self) -> bool;
}

pub trait AppRuntime {
    fn id(&self) -> &'static str;
    fn extension(&self) -> &'static str;
    fn execute(&self, source: &str, api: &mut dyn SystemApi) -> Result<i32>;
}

pub type BoxedRuntime = Box<dyn AppRuntime>;
