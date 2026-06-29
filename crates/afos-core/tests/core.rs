use afos_api::{
    AppIdentity, Capability, Error, NetStatus, Platform, Result, StorageEntry, SystemApi,
    SystemInfo,
};
use afos_core::{AppSession, EmbeddedFile, SecurityManager, System, Vfs};
use std::collections::{BTreeMap, BTreeSet, VecDeque};

#[derive(Default)]
struct MockPlatform {
    files: BTreeMap<String, Vec<u8>>,
    dirs: BTreeSet<String>,
    input: VecDeque<String>,
    output: String,
    now: u64,
    connected: Vec<String>,
}

impl MockPlatform {
    fn parent(path: &str) -> &str {
        path.rsplit_once('/').map_or("", |(parent, _)| parent)
    }

    fn add_parents(&mut self, path: &str) {
        let mut current = String::new();
        for component in path.split('/').filter(|part| !part.is_empty()) {
            if !current.is_empty() {
                current.push('/');
            }
            current.push_str(component);
            self.dirs.insert(current.clone());
        }
    }
}

impl Platform for MockPlatform {
    fn console_write(&mut self, text: &str) -> Result<()> {
        self.output.push_str(text);
        Ok(())
    }

    fn console_read_line(&mut self, _prompt: &str, _secret: bool) -> Result<String> {
        self.input
            .pop_front()
            .ok_or_else(|| Error::Io(String::from("no mock input")))
    }

    fn console_clear(&mut self) -> Result<()> {
        self.output.clear();
        Ok(())
    }

    fn storage_read(&mut self, path: &str) -> Result<Vec<u8>> {
        self.files
            .get(path)
            .cloned()
            .ok_or_else(|| Error::NotFound(String::from(path)))
    }

    fn storage_write(&mut self, path: &str, data: &[u8]) -> Result<()> {
        self.add_parents(Self::parent(path));
        self.files.insert(String::from(path), data.to_vec());
        Ok(())
    }

    fn storage_list(&mut self, path: &str) -> Result<Vec<StorageEntry>> {
        if !self.dirs.contains(path) {
            return Err(Error::NotDirectory(String::from(path)));
        }
        let prefix = if path.is_empty() {
            String::new()
        } else {
            format!("{path}/")
        };
        let mut entries = BTreeMap::new();
        for directory in &self.dirs {
            if let Some(remainder) = directory.strip_prefix(&prefix)
                && !remainder.is_empty()
                && !remainder.contains('/')
            {
                entries.insert(
                    remainder.to_owned(),
                    StorageEntry {
                        name: remainder.to_owned(),
                        is_dir: true,
                        len: 0,
                    },
                );
            }
        }
        for (file, data) in &self.files {
            if let Some(remainder) = file.strip_prefix(&prefix)
                && !remainder.is_empty()
                && !remainder.contains('/')
            {
                entries.insert(
                    remainder.to_owned(),
                    StorageEntry {
                        name: remainder.to_owned(),
                        is_dir: false,
                        len: data.len() as u64,
                    },
                );
            }
        }
        Ok(entries.into_values().collect())
    }

    fn storage_create_dir_all(&mut self, path: &str) -> Result<()> {
        self.add_parents(path);
        Ok(())
    }

    fn storage_remove(&mut self, path: &str) -> Result<()> {
        self.files.remove(path);
        let prefix = format!("{path}/");
        self.files.retain(|name, _| !name.starts_with(&prefix));
        self.dirs
            .retain(|name| name != path && !name.starts_with(&prefix));
        Ok(())
    }

    fn storage_rename(&mut self, from: &str, to: &str) -> Result<()> {
        let data = self
            .files
            .remove(from)
            .ok_or_else(|| Error::NotFound(String::from(from)))?;
        self.storage_write(to, &data)
    }

    fn storage_exists(&mut self, path: &str) -> Result<bool> {
        Ok(self.files.contains_key(path) || self.dirs.contains(path))
    }

    fn monotonic_millis(&self) -> u64 {
        self.now
    }

    fn fill_random(&mut self, output: &mut [u8]) -> Result<()> {
        for (index, byte) in output.iter_mut().enumerate() {
            *byte = u8::try_from(index).unwrap() ^ 0xa5;
        }
        Ok(())
    }

    fn system_info(&self) -> SystemInfo {
        SystemInfo {
            name: String::from("AFOS"),
            version: String::from("test"),
            platform: String::from("mock"),
            architecture: String::from("test"),
        }
    }

    fn net_status(&mut self) -> Result<NetStatus> {
        Ok(NetStatus {
            link_up: true,
            mac: String::from("mock"),
            address: Some(String::from("198.51.100.2/24")),
            gateway: Some(String::from("198.51.100.1")),
        })
    }

    fn net_connect(&mut self, host: &str, port: u16) -> Result<u64> {
        self.connected.push(format!("{host}:{port}"));
        Ok(self.connected.len() as u64)
    }

    fn net_send(&mut self, _handle: u64, data: &[u8]) -> Result<usize> {
        Ok(data.len())
    }

    fn net_recv(&mut self, _handle: u64, _out: &mut [u8]) -> Result<usize> {
        Ok(0)
    }

    fn net_close(&mut self, _handle: u64) -> Result<()> {
        Ok(())
    }
}

fn identity(trusted: bool, capabilities: Vec<Capability>) -> AppIdentity {
    AppIdentity {
        id: String::from("com.example.test"),
        source_path: String::from(if trusted {
            "/sys/apps/test.rhai"
        } else {
            "/apps/test.rhai"
        }),
        trusted,
        api_version: 1,
        capabilities,
    }
}

static TEST_SYSTEM_FILES: &[EmbeddedFile] = &[EmbeddedFile::text(
    "/sys/README.txt",
    "read-only test system",
)];

#[test]
fn vfs_routes_mounts_and_keeps_sys_read_only() {
    let mut vfs = Vfs::with_system_files(MockPlatform::default(), TEST_SYSTEM_FILES);
    vfs.initialize_layout().unwrap();
    assert!(
        vfs.read_text("/sys/README.txt")
            .unwrap()
            .contains("read-only")
    );
    assert!(matches!(
        vfs.write("/sys/README.txt", b"changed"),
        Err(Error::ReadOnly(_))
    ));
    vfs.write("/user/saves/note.txt", b"persistent").unwrap();
    assert_eq!(vfs.read_text("/user/saves/note.txt").unwrap(), "persistent");
    assert_eq!(
        vfs.list("/")
            .unwrap()
            .iter()
            .map(|item| item.name.as_str())
            .collect::<Vec<_>>(),
        ["sys", "apps", "user"]
    );
}

#[test]
fn password_verifier_authenticates_without_storing_password() {
    let mut vfs = Vfs::new(MockPlatform::default());
    vfs.initialize_layout().unwrap();
    let mut security = SecurityManager::new();
    security.set_password(&mut vfs, "correct horse").unwrap();
    let encoded = vfs.read_text("/user/config/security").unwrap();
    assert!(!encoded.contains("correct horse"));
    assert!(matches!(
        security.verify(&mut vfs, "wrong"),
        Err(Error::AuthenticationFailed)
    ));
    security.verify(&mut vfs, "correct horse").unwrap();

    for _ in 0..3 {
        let _ = security.verify(&mut vfs, "wrong");
    }
    assert!(matches!(
        security.verify(&mut vfs, "correct horse"),
        Err(Error::RateLimited(_))
    ));
}

#[test]
fn installed_apps_reauthorize_each_protected_operation() {
    let mut system = System::new(MockPlatform::default());
    system.initialize(false).unwrap();
    system
        .vfs_mut()
        .write("/user/saves/note.txt", b"hello")
        .unwrap();
    system
        .vfs_mut()
        .platform_mut()
        .input
        .extend([String::from("yes"), String::from("yes")]);

    {
        let mut session = AppSession::new(
            &mut system,
            identity(false, vec![Capability::FsRead(String::from("/user/saves"))]),
            Vec::new(),
            String::from("/"),
        );
        assert_eq!(session.read_text("/user/saves/note.txt").unwrap(), "hello");
        assert_eq!(session.read_text("/user/saves/note.txt").unwrap(), "hello");
    }

    assert_eq!(
        system
            .vfs()
            .platform()
            .output
            .matches("Permission request")
            .count(),
        2
    );
}

#[test]
fn appdata_is_private_and_security_config_is_never_exposed() {
    let mut system = System::new(MockPlatform::default());
    system.initialize(false).unwrap();
    {
        let mut session = AppSession::new(
            &mut system,
            identity(true, vec![Capability::FsRead(String::from("/"))]),
            Vec::new(),
            String::from("/"),
        );
        session.appdata_write("state.txt", "saved").unwrap();
        assert_eq!(session.appdata_read("state.txt").unwrap(), "saved");
        assert!(matches!(
            session.appdata_read("../../config/initialized"),
            Err(Error::PermissionDenied(_))
        ));
        assert!(matches!(
            session.read_text("/user/config/security"),
            Err(Error::PermissionDenied(_))
        ));
    }
}

#[test]
fn network_access_honors_declared_host_capabilities() {
    let mut system = System::new(MockPlatform::default());
    system.initialize(false).unwrap();

    {
        // A trusted app with a wildcard capability connects without prompting.
        let mut session = AppSession::new(
            &mut system,
            identity(true, vec![Capability::Network(String::from("*"))]),
            Vec::new(),
            String::from("/"),
        );
        let handle = session.net_connect("example.com", 80).unwrap();
        assert_eq!(session.net_send(handle, b"ping").unwrap(), 4);
        session.net_close(handle).unwrap();
        // A closed handle can no longer be used.
        assert!(matches!(
            session.net_send(handle, b"again"),
            Err(Error::NotFound(_))
        ));
    }

    {
        // An app may only reach the host it declared.
        let mut session = AppSession::new(
            &mut system,
            identity(true, vec![Capability::Network(String::from("example.com"))]),
            Vec::new(),
            String::from("/"),
        );
        session.net_connect("example.com", 443).unwrap();
        assert!(matches!(
            session.net_connect("evil.test", 443),
            Err(Error::CapabilityNotDeclared(_))
        ));
    }

    // An app with no network capability cannot even read status.
    {
        let mut session = AppSession::new(
            &mut system,
            identity(true, Vec::new()),
            Vec::new(),
            String::from("/"),
        );
        assert!(matches!(
            session.net_status(),
            Err(Error::CapabilityNotDeclared(_))
        ));
    }

    assert_eq!(
        system.vfs().platform().connected,
        ["example.com:80", "example.com:443"]
    );
}

#[test]
fn undeclared_access_is_rejected_without_prompting() {
    let mut system = System::new(MockPlatform::default());
    system.initialize(false).unwrap();
    system
        .vfs_mut()
        .write("/user/saves/note.txt", b"hello")
        .unwrap();

    {
        let mut session = AppSession::new(
            &mut system,
            identity(false, Vec::new()),
            Vec::new(),
            String::from("/"),
        );
        assert!(matches!(
            session.read_text("/user/saves/note.txt"),
            Err(Error::CapabilityNotDeclared(_))
        ));
    }
    assert!(
        !system
            .vfs()
            .platform()
            .output
            .contains("Permission request")
    );
}
