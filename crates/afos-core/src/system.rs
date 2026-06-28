use afos_api::{
    AppIdentity, Capability, Error, Platform, Result, StorageEntry, SystemApi, SystemInfo,
};
use alloc::{string::String, string::ToString, vec::Vec};

use crate::{SecurityManager, Vfs, normalize_path};

const INITIALIZED_PATH: &str = "/user/config/initialized";
const SECURITY_PATH: &str = "/user/config/security";

pub struct System<P> {
    vfs: Vfs<P>,
    security: SecurityManager,
}

impl<P: Platform> System<P> {
    #[must_use]
    pub fn new(platform: P) -> Self {
        Self {
            vfs: Vfs::new(platform),
            security: SecurityManager::new(),
        }
    }

    #[must_use]
    pub fn with_vfs(vfs: Vfs<P>) -> Self {
        Self {
            vfs,
            security: SecurityManager::new(),
        }
    }

    pub fn initialize(&mut self, interactive: bool) -> Result<()> {
        self.vfs.initialize_layout()?;
        if self.vfs.exists(INITIALIZED_PATH)? {
            return Ok(());
        }

        self.vfs
            .platform_mut()
            .console_write("AFOS first-run setup\n")?;
        if interactive {
            let answer = self
                .vfs
                .platform_mut()
                .console_read_line("Configure a master password? [y/N] ", false)?;
            if answer.eq_ignore_ascii_case("y") || answer.eq_ignore_ascii_case("yes") {
                self.prompt_new_password()?;
            }
        }
        self.vfs.write(INITIALIZED_PATH, b"AFOS-SETUP-1\n")
    }

    pub fn change_password(&mut self) -> Result<()> {
        if self.security.has_password(&mut self.vfs)? {
            let current = self
                .vfs
                .platform_mut()
                .console_read_line("Current master password: ", true)?;
            self.security.verify(&mut self.vfs, &current)?;
        }
        self.prompt_new_password()
    }

    fn prompt_new_password(&mut self) -> Result<()> {
        let password = self
            .vfs
            .platform_mut()
            .console_read_line("New master password (empty disables it): ", true)?;
        if !password.is_empty() {
            let confirmation = self
                .vfs
                .platform_mut()
                .console_read_line("Confirm master password: ", true)?;
            if password != confirmation {
                return Err(Error::InvalidInput(String::from(
                    "password confirmation does not match",
                )));
            }
        }
        self.security.set_password(&mut self.vfs, &password)
    }

    #[must_use]
    pub fn vfs(&self) -> &Vfs<P> {
        &self.vfs
    }

    #[must_use]
    pub fn vfs_mut(&mut self) -> &mut Vfs<P> {
        &mut self.vfs
    }

    pub fn into_platform(self) -> P {
        self.vfs.into_platform()
    }

    fn authorize(
        &mut self,
        app: &AppIdentity,
        capability: &Capability,
        resource: &str,
    ) -> Result<()> {
        if resource == SECURITY_PATH
            || resource
                .strip_prefix(SECURITY_PATH)
                .is_some_and(|suffix| suffix.starts_with('/'))
        {
            return Err(Error::PermissionDenied(String::from(
                "security configuration is not exposed to applications",
            )));
        }

        if !declares(app, capability) {
            return Err(Error::CapabilityNotDeclared(capability.to_string()));
        }
        if app.trusted {
            return Ok(());
        }

        self.vfs.platform_mut().console_write(&alloc::format!(
            "\nPermission request\n  app: {}\n  operation: {}\n  resource: {}\n",
            app.id,
            capability,
            resource
        ))?;
        let answer = self
            .vfs
            .platform_mut()
            .console_read_line("Allow this operation once? [y/N] ", false)?;
        if !answer.eq_ignore_ascii_case("y") && !answer.eq_ignore_ascii_case("yes") {
            return Err(Error::PermissionDenied(String::from("request denied")));
        }

        if self.security.has_password(&mut self.vfs)? {
            let password = self
                .vfs
                .platform_mut()
                .console_read_line("Master password: ", true)?;
            self.security.verify(&mut self.vfs, &password)?;
        }
        Ok(())
    }
}

fn path_prefix(prefix: &str, path: &str) -> bool {
    prefix == "/"
        || path == prefix
        || path
            .strip_prefix(prefix)
            .is_some_and(|suffix| suffix.starts_with('/'))
}

fn declares(app: &AppIdentity, requested: &Capability) -> bool {
    app.capabilities
        .iter()
        .any(|declared| match (declared, requested) {
            (Capability::FsRead(prefix), Capability::FsRead(path))
            | (Capability::FsWrite(prefix), Capability::FsWrite(path)) => {
                normalize_path("/", prefix).is_ok_and(|normalized| path_prefix(&normalized, path))
            }
            (Capability::Clock, Capability::Clock)
            | (Capability::SystemInfo, Capability::SystemInfo) => true,
            _ => false,
        })
}

pub struct AppSession<'a, P> {
    system: &'a mut System<P>,
    app: AppIdentity,
    args: Vec<String>,
    cwd: String,
    appdata_root: String,
}

impl<'a, P: Platform> AppSession<'a, P> {
    #[must_use]
    pub fn new(
        system: &'a mut System<P>,
        app: AppIdentity,
        args: Vec<String>,
        cwd: String,
    ) -> Self {
        let appdata_root = alloc::format!("/user/appdata/{}", app.id);
        Self {
            system,
            app,
            args,
            cwd,
            appdata_root,
        }
    }

    fn resolve(&self, path: &str) -> Result<String> {
        normalize_path(&self.cwd, path)
    }

    fn resolve_appdata(&self, path: &str) -> Result<String> {
        let resolved = normalize_path(&self.appdata_root, path)?;
        if !path_prefix(&self.appdata_root, &resolved) {
            return Err(Error::PermissionDenied(String::from(
                "path escapes application data",
            )));
        }
        Ok(resolved)
    }
}

impl<P: Platform> SystemApi for AppSession<'_, P> {
    fn app(&self) -> &AppIdentity {
        &self.app
    }

    fn args(&self) -> &[String] {
        &self.args
    }

    fn cwd(&self) -> &str {
        &self.cwd
    }

    fn console_write(&mut self, text: &str) -> Result<()> {
        self.system.vfs.platform_mut().console_write(text)
    }

    fn console_read_line(&mut self, prompt: &str) -> Result<String> {
        self.system
            .vfs
            .platform_mut()
            .console_read_line(prompt, false)
    }

    fn read_text(&mut self, path: &str) -> Result<String> {
        let path = self.resolve(path)?;
        self.system
            .authorize(&self.app, &Capability::FsRead(path.clone()), &path)?;
        self.system.vfs.read_text(&path)
    }

    fn write_text(&mut self, path: &str, text: &str) -> Result<()> {
        let path = self.resolve(path)?;
        self.system
            .authorize(&self.app, &Capability::FsWrite(path.clone()), &path)?;
        self.system.vfs.write(&path, text.as_bytes())
    }

    fn list(&mut self, path: &str) -> Result<Vec<StorageEntry>> {
        let path = self.resolve(path)?;
        self.system
            .authorize(&self.app, &Capability::FsRead(path.clone()), &path)?;
        self.system.vfs.list(&path)
    }

    fn create_dir_all(&mut self, path: &str) -> Result<()> {
        let path = self.resolve(path)?;
        self.system
            .authorize(&self.app, &Capability::FsWrite(path.clone()), &path)?;
        self.system.vfs.create_dir_all(&path)
    }

    fn remove(&mut self, path: &str) -> Result<()> {
        let path = self.resolve(path)?;
        self.system
            .authorize(&self.app, &Capability::FsWrite(path.clone()), &path)?;
        self.system.vfs.remove(&path)
    }

    fn rename(&mut self, from: &str, to: &str) -> Result<()> {
        let from = self.resolve(from)?;
        let to = self.resolve(to)?;
        self.system
            .authorize(&self.app, &Capability::FsWrite(from.clone()), &from)?;
        self.system
            .authorize(&self.app, &Capability::FsWrite(to.clone()), &to)?;
        self.system.vfs.rename(&from, &to)
    }

    fn appdata_read(&mut self, path: &str) -> Result<String> {
        let path = self.resolve_appdata(path)?;
        self.system.vfs.read_text(&path)
    }

    fn appdata_write(&mut self, path: &str, text: &str) -> Result<()> {
        let path = self.resolve_appdata(path)?;
        self.system.vfs.create_dir_all(&self.appdata_root)?;
        self.system.vfs.write(&path, text.as_bytes())
    }

    fn monotonic_millis(&mut self) -> Result<u64> {
        self.system
            .authorize(&self.app, &Capability::Clock, "monotonic clock")?;
        Ok(self.system.vfs.platform().monotonic_millis())
    }

    fn system_info(&mut self) -> Result<SystemInfo> {
        self.system
            .authorize(&self.app, &Capability::SystemInfo, "system information")?;
        Ok(self.system.vfs.platform().system_info())
    }

    fn cancelled(&mut self) -> bool {
        self.system.vfs.platform_mut().poll_cancel()
    }
}
