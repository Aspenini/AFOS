use afos_api::{Error, Platform, Result, StorageEntry, SystemInfo};
use std::{
    fs,
    io::{self, Write},
    path::{Component, Path, PathBuf},
    time::Instant,
};

pub struct DesktopPlatform {
    root: PathBuf,
    started: Instant,
    remove_on_drop: bool,
}

impl DesktopPlatform {
    pub fn new(root: PathBuf, remove_on_drop: bool) -> Result<Self> {
        fs::create_dir_all(&root).map_err(io_error)?;
        let root = root.canonicalize().map_err(io_error)?;
        Ok(Self {
            root,
            started: Instant::now(),
            remove_on_drop,
        })
    }

    fn joined(&self, relative: &str) -> Result<PathBuf> {
        let path = Path::new(relative);
        if path.is_absolute()
            || path
                .components()
                .any(|component| !matches!(component, Component::Normal(_) | Component::CurDir))
        {
            return Err(Error::InvalidPath);
        }
        Ok(self.root.join(path))
    }

    fn existing(&self, relative: &str) -> Result<PathBuf> {
        let path = self.joined(relative)?;
        let canonical = path.canonicalize().map_err(|error| match error.kind() {
            io::ErrorKind::NotFound => Error::NotFound(String::from(relative)),
            _ => io_error(error),
        })?;
        if !canonical.starts_with(&self.root) {
            return Err(Error::PermissionDenied(String::from(
                "path escapes the AFOS data directory",
            )));
        }
        Ok(canonical)
    }

    fn writable(&self, relative: &str) -> Result<PathBuf> {
        let path = self.joined(relative)?;
        let parent = path.parent().ok_or(Error::InvalidPath)?;
        fs::create_dir_all(parent).map_err(io_error)?;
        let canonical_parent = parent.canonicalize().map_err(io_error)?;
        if !canonical_parent.starts_with(&self.root) {
            return Err(Error::PermissionDenied(String::from(
                "path escapes the AFOS data directory",
            )));
        }
        if fs::symlink_metadata(&path).is_ok_and(|metadata| metadata.file_type().is_symlink()) {
            return Err(Error::PermissionDenied(String::from(
                "symbolic-link targets are not writable",
            )));
        }
        Ok(path)
    }
}

impl Drop for DesktopPlatform {
    fn drop(&mut self) {
        if self.remove_on_drop {
            let _ = fs::remove_dir_all(&self.root);
        }
    }
}

impl Platform for DesktopPlatform {
    fn console_write(&mut self, text: &str) -> Result<()> {
        print!("{text}");
        io::stdout().flush().map_err(io_error)
    }

    fn console_read_line(&mut self, prompt: &str, secret: bool) -> Result<String> {
        if secret {
            return rpassword::prompt_password(prompt).map_err(io_error);
        }
        print!("{prompt}");
        io::stdout().flush().map_err(io_error)?;
        let mut line = String::new();
        let count = io::stdin().read_line(&mut line).map_err(io_error)?;
        if count == 0 {
            return Err(Error::Io(String::from("end of input")));
        }
        while matches!(line.chars().last(), Some('\n' | '\r')) {
            line.pop();
        }
        Ok(line)
    }

    fn console_clear(&mut self) -> Result<()> {
        self.console_write("\u{1b}[2J\u{1b}[H")
    }

    fn storage_read(&mut self, path: &str) -> Result<Vec<u8>> {
        fs::read(self.existing(path)?).map_err(io_error)
    }

    fn storage_write(&mut self, path: &str, data: &[u8]) -> Result<()> {
        fs::write(self.writable(path)?, data).map_err(io_error)
    }

    fn storage_list(&mut self, path: &str) -> Result<Vec<StorageEntry>> {
        let directory = self.existing(path)?;
        if !directory.is_dir() {
            return Err(Error::NotDirectory(String::from(path)));
        }
        let mut entries = Vec::new();
        for entry in fs::read_dir(directory).map_err(io_error)? {
            let entry = entry.map_err(io_error)?;
            let metadata = entry.metadata().map_err(io_error)?;
            entries.push(StorageEntry {
                name: entry.file_name().to_string_lossy().into_owned(),
                is_dir: metadata.is_dir(),
                len: metadata.len(),
            });
        }
        entries.sort_by(|left, right| left.name.cmp(&right.name));
        Ok(entries)
    }

    fn storage_create_dir_all(&mut self, path: &str) -> Result<()> {
        fs::create_dir_all(self.writable(path)?).map_err(io_error)
    }

    fn storage_remove(&mut self, path: &str) -> Result<()> {
        let path = self.existing(path)?;
        if path.is_dir() {
            fs::remove_dir_all(path).map_err(io_error)
        } else {
            fs::remove_file(path).map_err(io_error)
        }
    }

    fn storage_rename(&mut self, from: &str, to: &str) -> Result<()> {
        let from = self.existing(from)?;
        let to = self.writable(to)?;
        fs::rename(from, to).map_err(io_error)
    }

    fn storage_exists(&mut self, path: &str) -> Result<bool> {
        match self.existing(path) {
            Ok(_) => Ok(true),
            Err(Error::NotFound(_)) => Ok(false),
            Err(error) => Err(error),
        }
    }

    fn monotonic_millis(&self) -> u64 {
        u64::try_from(self.started.elapsed().as_millis()).unwrap_or(u64::MAX)
    }

    fn fill_random(&mut self, output: &mut [u8]) -> Result<()> {
        getrandom::fill(output)
            .map_err(|error| Error::Io(format!("random-number generator: {error}")))
    }

    fn system_info(&self) -> SystemInfo {
        SystemInfo {
            name: String::from("AFOS"),
            version: String::from(env!("CARGO_PKG_VERSION")),
            platform: String::from(std::env::consts::OS),
            architecture: String::from(std::env::consts::ARCH),
        }
    }
}

fn io_error(error: io::Error) -> Error {
    match error.kind() {
        io::ErrorKind::NotFound => Error::NotFound(error.to_string()),
        io::ErrorKind::AlreadyExists => Error::AlreadyExists(error.to_string()),
        io::ErrorKind::PermissionDenied => Error::PermissionDenied(error.to_string()),
        _ => Error::Io(error.to_string()),
    }
}

pub fn default_data_dir() -> PathBuf {
    if let Some(path) = std::env::var_os("AFOS_DATA_DIR") {
        return PathBuf::from(path);
    }
    if cfg!(target_os = "windows") {
        if let Some(path) = std::env::var_os("APPDATA") {
            return PathBuf::from(path).join("AFOS");
        }
    } else if cfg!(target_os = "macos") {
        if let Some(home) = std::env::var_os("HOME") {
            return PathBuf::from(home)
                .join("Library")
                .join("Application Support")
                .join("AFOS");
        }
    } else if let Some(path) = std::env::var_os("XDG_DATA_HOME") {
        return PathBuf::from(path).join("afos");
    } else if let Some(home) = std::env::var_os("HOME") {
        return PathBuf::from(home)
            .join(".local")
            .join("share")
            .join("afos");
    }
    PathBuf::from(".afos")
}
