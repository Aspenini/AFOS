use afos_api::{BoxedRuntime, Error, Platform, Result, SystemApi};
use alloc::{string::String, vec::Vec};

use crate::{
    AppSession, EmbeddedFile, RuntimeRegistry, System, Vfs, normalize_path, parse_metadata,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CommandOutcome {
    Continue(i32),
    Exit(i32),
}

#[derive(Clone, Debug)]
pub struct ShellConfig {
    pub interactive_setup: bool,
    pub banner: bool,
}

impl Default for ShellConfig {
    fn default() -> Self {
        Self {
            interactive_setup: true,
            banner: true,
        }
    }
}

pub struct Afos<P> {
    system: System<P>,
    runtimes: RuntimeRegistry,
    cwd: String,
}

impl<P: Platform> Afos<P> {
    #[must_use]
    pub fn new(platform: P) -> Self {
        Self {
            system: System::new(platform),
            runtimes: RuntimeRegistry::new(),
            cwd: String::from("/user"),
        }
    }

    #[must_use]
    pub fn with_system_files(platform: P, files: &'static [EmbeddedFile]) -> Self {
        Self {
            system: System::with_vfs(Vfs::with_embedded(platform, files)),
            runtimes: RuntimeRegistry::new(),
            cwd: String::from("/user"),
        }
    }

    pub fn register_runtime(&mut self, runtime: BoxedRuntime) -> Result<()> {
        self.runtimes.register(runtime)
    }

    pub fn initialize(&mut self, interactive_setup: bool) -> Result<()> {
        self.system.initialize(interactive_setup)
    }

    pub fn run_interactive(&mut self, config: &ShellConfig) -> Result<i32> {
        self.initialize(config.interactive_setup)?;
        if config.banner {
            self.system.vfs_mut().platform_mut().console_write(
                "AFOS 0.2.0 — portable application environment\nType `help` for commands.\n\n",
            )?;
        }

        loop {
            let prompt = alloc::format!("AFOS:{}$ ", self.cwd);
            let line = self
                .system
                .vfs_mut()
                .platform_mut()
                .console_read_line(&prompt, false)?;
            match self.run_command(&line)? {
                CommandOutcome::Continue(_) => {}
                CommandOutcome::Exit(code) => return Ok(code),
            }
        }
    }

    pub fn run_command(&mut self, line: &str) -> Result<CommandOutcome> {
        let mut args = split_command_line(line)?;
        if args.is_empty() {
            return Ok(CommandOutcome::Continue(0));
        }
        let command = args.remove(0);

        match command.as_str() {
            "cd" => {
                let target = args.first().map_or("/user", String::as_str);
                let target = normalize_path(&self.cwd, target)?;
                self.system.vfs_mut().list(&target)?;
                self.cwd = target;
                Ok(CommandOutcome::Continue(0))
            }
            "exit" => {
                let code = args
                    .first()
                    .map(|value| {
                        value.parse::<i32>().map_err(|_| {
                            Error::InvalidInput(String::from("exit status must be an integer"))
                        })
                    })
                    .transpose()?
                    .unwrap_or(0);
                Ok(CommandOutcome::Exit(code))
            }
            "clear" => {
                self.system.vfs_mut().platform_mut().console_clear()?;
                Ok(CommandOutcome::Continue(0))
            }
            "passwd" | "setup" => {
                self.system.change_password()?;
                self.system
                    .vfs_mut()
                    .platform_mut()
                    .console_write("Master password updated.\n")?;
                Ok(CommandOutcome::Continue(0))
            }
            "run" => {
                let Some(path) = args.first().cloned() else {
                    return Err(Error::InvalidInput(String::from(
                        "usage: run <app> [args...]",
                    )));
                };
                let app_args = args.into_iter().skip(1).collect();
                let code = self.execute_app(&path, app_args)?;
                Ok(CommandOutcome::Continue(code))
            }
            _ => {
                let code = self.execute_app(&command, args)?;
                Ok(CommandOutcome::Continue(code))
            }
        }
    }

    fn execute_app(&mut self, command: &str, args: Vec<String>) -> Result<i32> {
        let path = self.resolve_command(command)?;
        let source = self.system.vfs_mut().read_text(&path)?;
        if source.len() > 1024 * 1024 {
            return Err(Error::ResourceLimit(String::from(
                "application source exceeds 1 MiB",
            )));
        }
        let trusted = path
            .strip_prefix("/sys/apps/")
            .is_some_and(|suffix| !suffix.contains('/'));
        let metadata = parse_metadata(&path, &source, trusted)?;
        let mut full_args = Vec::with_capacity(args.len() + 1);
        full_args.push(metadata.identity.id.clone());
        full_args.extend(args);
        let mut session = AppSession::new(
            &mut self.system,
            metadata.identity,
            full_args,
            self.cwd.clone(),
        );
        let code = self.runtimes.execute(&path, &source, &mut session)?;
        if code != 0 {
            session.console_write(&alloc::format!(
                "{} exited with status {code}\n",
                session.app().id
            ))?;
        }
        Ok(code)
    }

    fn resolve_command(&mut self, command: &str) -> Result<String> {
        if command.contains('/') {
            let path = normalize_path(&self.cwd, command)?;
            if self.system.vfs_mut().exists(&path)? && self.runtimes.supports(&path) {
                return Ok(path);
            }
            if !path
                .rsplit('/')
                .next()
                .is_some_and(|name| name.contains('.'))
            {
                let rhai_path = alloc::format!("{path}.rhai");
                if self.system.vfs_mut().exists(&rhai_path)? {
                    return Ok(rhai_path);
                }
            }
            return Err(Error::NotFound(path));
        }

        for root in ["/sys/apps", "/apps"] {
            let path = if command.contains('.') {
                alloc::format!("{root}/{command}")
            } else {
                alloc::format!("{root}/{command}.rhai")
            };
            if self.system.vfs_mut().exists(&path)? && self.runtimes.supports(&path) {
                return Ok(path);
            }
        }
        Err(Error::NotFound(String::from(command)))
    }

    #[must_use]
    pub fn system(&self) -> &System<P> {
        &self.system
    }

    #[must_use]
    pub fn system_mut(&mut self) -> &mut System<P> {
        &mut self.system
    }

    pub fn into_platform(self) -> P {
        self.system.into_platform()
    }
}

pub fn split_command_line(input: &str) -> Result<Vec<String>> {
    let mut output = Vec::new();
    let mut current = String::new();
    let mut quote = None;
    let mut escaped = false;

    for character in input.chars() {
        if escaped {
            current.push(character);
            escaped = false;
            continue;
        }
        if character == '\\' {
            escaped = true;
            continue;
        }
        if let Some(active_quote) = quote {
            if character == active_quote {
                quote = None;
            } else {
                current.push(character);
            }
            continue;
        }
        if matches!(character, '\'' | '"') {
            quote = Some(character);
        } else if character.is_whitespace() {
            if !current.is_empty() {
                output.push(core::mem::take(&mut current));
            }
        } else {
            current.push(character);
        }
    }
    if escaped {
        current.push('\\');
    }
    if quote.is_some() {
        return Err(Error::InvalidInput(String::from("unterminated quote")));
    }
    if !current.is_empty() {
        output.push(current);
    }
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::split_command_line;

    #[test]
    fn parses_quotes_and_escapes() {
        assert_eq!(
            split_command_line(r#"echo "hello world" one\ two"#).unwrap(),
            ["echo", "hello world", "one two"]
        );
    }

    #[test]
    fn rejects_open_quote() {
        assert!(split_command_line("echo 'no").is_err());
    }
}
