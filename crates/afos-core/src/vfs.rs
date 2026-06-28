use afos_api::{Error, Platform, Result, StorageEntry};
use alloc::{collections::BTreeMap, string::String, vec, vec::Vec};

#[derive(Clone, Copy, Debug)]
pub struct EmbeddedFile {
    pub path: &'static str,
    pub data: &'static [u8],
}

impl EmbeddedFile {
    #[must_use]
    pub const fn text(path: &'static str, data: &'static str) -> Self {
        Self {
            path,
            data: data.as_bytes(),
        }
    }
}

#[must_use]
fn is_path_prefix(prefix: &str, path: &str) -> bool {
    path == prefix
        || path
            .strip_prefix(prefix)
            .is_some_and(|suffix| suffix.starts_with('/'))
}

pub fn normalize_path(cwd: &str, input: &str) -> Result<String> {
    if input.contains('\0') || input.contains('\\') {
        return Err(Error::InvalidPath);
    }

    let mut components: Vec<&str> = Vec::new();
    if !input.starts_with('/') {
        for component in cwd.split('/') {
            if !component.is_empty() {
                components.push(component);
            }
        }
    }

    for component in input.split('/') {
        match component {
            "" | "." => {}
            ".." => {
                if components.pop().is_none() {
                    return Err(Error::InvalidPath);
                }
            }
            value => components.push(value),
        }
    }

    let mut output = String::from("/");
    output.push_str(&components.join("/"));
    Ok(output)
}

pub struct Vfs<P> {
    platform: P,
    system_files: &'static [EmbeddedFile],
}

impl<P: Platform> Vfs<P> {
    #[must_use]
    pub fn new(platform: P) -> Self {
        Self {
            platform,
            system_files: &[],
        }
    }

    #[must_use]
    pub fn with_system_files(platform: P, system_files: &'static [EmbeddedFile]) -> Self {
        Self {
            platform,
            system_files,
        }
    }

    #[must_use]
    pub fn platform(&self) -> &P {
        &self.platform
    }

    #[must_use]
    pub fn platform_mut(&mut self) -> &mut P {
        &mut self.platform
    }

    #[must_use]
    pub fn into_platform(self) -> P {
        self.platform
    }

    pub fn initialize_layout(&mut self) -> Result<()> {
        for path in ["apps", "user", "user/config", "user/saves", "user/appdata"] {
            self.platform.storage_create_dir_all(path)?;
        }
        Ok(())
    }

    pub fn exists(&mut self, path: &str) -> Result<bool> {
        let path = normalize_path("/", path)?;
        if path == "/" || path == "/sys" || path == "/apps" || path == "/user" {
            return Ok(true);
        }
        if is_path_prefix("/sys", &path) {
            return Ok(self.system_file(&path).is_some() || self.system_dir_exists(&path));
        }
        self.platform.storage_exists(Self::persistent_path(&path)?)
    }

    pub fn read(&mut self, path: &str) -> Result<Vec<u8>> {
        let path = normalize_path("/", path)?;
        if is_path_prefix("/sys", &path) {
            return self
                .system_file(&path)
                .map(|file| file.data.to_vec())
                .ok_or(Error::NotFound(path));
        }
        self.platform.storage_read(Self::persistent_path(&path)?)
    }

    pub fn read_text(&mut self, path: &str) -> Result<String> {
        String::from_utf8(self.read(path)?)
            .map_err(|_| Error::InvalidInput(String::from("file is not valid UTF-8")))
    }

    pub fn write(&mut self, path: &str, data: &[u8]) -> Result<()> {
        let path = normalize_path("/", path)?;
        if is_path_prefix("/sys", &path) {
            return Err(Error::ReadOnly(path));
        }
        self.platform
            .storage_write(Self::persistent_path(&path)?, data)
    }

    pub fn list(&mut self, path: &str) -> Result<Vec<StorageEntry>> {
        let path = normalize_path("/", path)?;
        if path == "/" {
            return Ok(vec![
                StorageEntry {
                    name: String::from("sys"),
                    is_dir: true,
                    len: 0,
                },
                StorageEntry {
                    name: String::from("apps"),
                    is_dir: true,
                    len: 0,
                },
                StorageEntry {
                    name: String::from("user"),
                    is_dir: true,
                    len: 0,
                },
            ]);
        }
        if is_path_prefix("/sys", &path) {
            return self.list_system(&path);
        }
        self.platform.storage_list(Self::persistent_path(&path)?)
    }

    pub fn create_dir_all(&mut self, path: &str) -> Result<()> {
        let path = normalize_path("/", path)?;
        if is_path_prefix("/sys", &path) {
            return Err(Error::ReadOnly(path));
        }
        self.platform
            .storage_create_dir_all(Self::persistent_path(&path)?)
    }

    pub fn remove(&mut self, path: &str) -> Result<()> {
        let path = normalize_path("/", path)?;
        if is_path_prefix("/sys", &path) {
            return Err(Error::ReadOnly(path));
        }
        if matches!(
            path.as_str(),
            "/" | "/apps" | "/user" | "/user/config" | "/user/saves" | "/user/appdata"
        ) {
            return Err(Error::PermissionDenied(String::from(
                "cannot remove an AFOS root directory",
            )));
        }
        self.platform.storage_remove(Self::persistent_path(&path)?)
    }

    pub fn rename(&mut self, from: &str, to: &str) -> Result<()> {
        let from = normalize_path("/", from)?;
        let to = normalize_path("/", to)?;
        if is_path_prefix("/sys", &from) || is_path_prefix("/sys", &to) {
            return Err(Error::ReadOnly(String::from("/sys")));
        }
        self.platform
            .storage_rename(Self::persistent_path(&from)?, Self::persistent_path(&to)?)
    }

    fn persistent_path(path: &str) -> Result<&str> {
        let relative = path.strip_prefix('/').ok_or(Error::InvalidPath)?;
        if relative.is_empty()
            || !(is_path_prefix("apps", relative) || is_path_prefix("user", relative))
        {
            return Err(Error::PermissionDenied(String::from(
                "path is outside persistent mounts",
            )));
        }
        Ok(relative)
    }

    fn system_file(&self, path: &str) -> Option<&EmbeddedFile> {
        self.system_files.iter().find(|file| file.path == path)
    }

    fn system_dir_exists(&self, path: &str) -> bool {
        let mut prefix = String::from(path);
        if !prefix.ends_with('/') {
            prefix.push('/');
        }
        self.system_files
            .iter()
            .any(|file| file.path.starts_with(&prefix))
    }

    fn list_system(&self, path: &str) -> Result<Vec<StorageEntry>> {
        if path != "/sys" && !self.system_dir_exists(path) {
            return Err(Error::NotFound(String::from(path)));
        }

        let mut prefix = String::from(path);
        if !prefix.ends_with('/') {
            prefix.push('/');
        }
        let mut entries: BTreeMap<String, StorageEntry> = BTreeMap::new();

        for file in self.system_files {
            let Some(remainder) = file.path.strip_prefix(&prefix) else {
                continue;
            };
            let (name, is_dir) = remainder
                .split_once('/')
                .map_or((remainder, false), |(name, _)| (name, true));
            if name.is_empty() {
                continue;
            }
            entries.entry(String::from(name)).or_insert(StorageEntry {
                name: String::from(name),
                is_dir,
                len: if is_dir { 0 } else { file.data.len() as u64 },
            });
        }

        Ok(entries.into_values().collect())
    }
}

#[cfg(test)]
mod tests {
    use super::normalize_path;

    #[test]
    fn normalizes_paths() {
        assert_eq!(
            normalize_path("/user/saves", "../config").unwrap(),
            "/user/config"
        );
        assert_eq!(
            normalize_path("/", "/apps//hello.rhai").unwrap(),
            "/apps/hello.rhai"
        );
    }

    #[test]
    fn rejects_root_escape() {
        assert!(normalize_path("/", "../../secret").is_err());
        assert!(normalize_path("/user", "../../../secret").is_err());
    }
}
