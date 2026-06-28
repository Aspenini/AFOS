#![no_std]

extern crate alloc;

mod app;
mod security;
mod shell;
mod system;
mod vfs;

pub use app::{AppMetadata, RuntimeRegistry, parse_metadata};
pub use security::SecurityManager;
pub use shell::{Afos, CommandOutcome, ShellConfig, split_command_line};
pub use system::{AppSession, System};
pub use vfs::{EmbeddedFile, Vfs, normalize_path};
