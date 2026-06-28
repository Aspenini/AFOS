use afos_api::{AppIdentity, BoxedRuntime, Capability, Error, Result, SystemApi};
use alloc::{string::String, vec::Vec};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AppMetadata {
    pub identity: AppIdentity,
}

pub fn parse_metadata(source_path: &str, source: &str, trusted: bool) -> Result<AppMetadata> {
    let filename = source_path
        .rsplit('/')
        .next()
        .and_then(|name| {
            name.rsplit_once('.')
                .map_or(Some(name), |(stem, _)| Some(stem))
        })
        .filter(|name| !name.is_empty())
        .ok_or_else(|| Error::InvalidInput(String::from("application has no file name")))?;

    let mut id = String::from(filename);
    let mut api_version = 1;
    let mut capabilities = Vec::new();

    for line in source.lines() {
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        let Some(directive) = trimmed.strip_prefix("// afos:") else {
            if trimmed.starts_with("//") {
                continue;
            }
            break;
        };
        let Some((key, value)) = directive.split_once('=') else {
            return Err(Error::InvalidInput(String::from(
                "AFOS directive must use key=value",
            )));
        };
        match key.trim() {
            "api" => {
                api_version = value
                    .trim()
                    .parse()
                    .map_err(|_| Error::InvalidInput(String::from("invalid API version")))?;
                if api_version != 1 {
                    return Err(Error::Unsupported(alloc::format!(
                        "application API version {api_version}"
                    )));
                }
            }
            "id" => {
                let candidate = value.trim();
                if candidate.is_empty()
                    || !candidate.bytes().all(|byte| {
                        byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_')
                    })
                {
                    return Err(Error::InvalidInput(String::from("invalid application ID")));
                }
                id = String::from(candidate);
            }
            "capabilities" => {
                for item in value
                    .split(',')
                    .map(str::trim)
                    .filter(|item| !item.is_empty())
                {
                    capabilities.push(parse_capability(item)?);
                }
            }
            other => {
                return Err(Error::InvalidInput(alloc::format!(
                    "unknown AFOS directive: {other}"
                )));
            }
        }
    }

    Ok(AppMetadata {
        identity: AppIdentity {
            id,
            source_path: String::from(source_path),
            trusted,
            api_version,
            capabilities,
        },
    })
}

fn parse_capability(value: &str) -> Result<Capability> {
    if value == "clock" {
        return Ok(Capability::Clock);
    }
    if value == "system.info" {
        return Ok(Capability::SystemInfo);
    }
    if let Some(path) = value.strip_prefix("fs.read:") {
        return Ok(Capability::FsRead(String::from(path)));
    }
    if let Some(path) = value.strip_prefix("fs.write:") {
        return Ok(Capability::FsWrite(String::from(path)));
    }
    Err(Error::InvalidInput(alloc::format!(
        "unknown capability: {value}"
    )))
}

#[derive(Default)]
pub struct RuntimeRegistry {
    runtimes: Vec<BoxedRuntime>,
}

impl RuntimeRegistry {
    #[must_use]
    pub const fn new() -> Self {
        Self {
            runtimes: Vec::new(),
        }
    }

    pub fn register(&mut self, runtime: BoxedRuntime) -> Result<()> {
        if self
            .runtimes
            .iter()
            .any(|registered| registered.extension() == runtime.extension())
        {
            return Err(Error::AlreadyExists(String::from(runtime.extension())));
        }
        self.runtimes.push(runtime);
        Ok(())
    }

    pub fn execute(&self, path: &str, source: &str, api: &mut dyn SystemApi) -> Result<i32> {
        let extension = path
            .rsplit_once('.')
            .map(|(_, extension)| extension)
            .ok_or_else(|| Error::Unsupported(String::from("application has no extension")))?;
        let runtime = self
            .runtimes
            .iter()
            .find(|runtime| runtime.extension() == extension)
            .ok_or_else(|| Error::Unsupported(alloc::format!("*.{extension} applications")))?;
        runtime.execute(source, api)
    }

    #[must_use]
    pub fn supports(&self, path: &str) -> bool {
        path.rsplit_once('.').is_some_and(|(_, extension)| {
            self.runtimes
                .iter()
                .any(|runtime| runtime.extension() == extension)
        })
    }
}

#[cfg(test)]
mod tests {
    use super::parse_metadata;
    use afos_api::Capability;
    use alloc::string::String;

    #[test]
    fn parses_single_file_metadata() {
        let source = "// afos:api=1\n// afos:id=com.example.notes\n// afos:capabilities=clock,fs.read:/user/saves\nprint(\"ok\");";
        let metadata = parse_metadata("/apps/notes.rhai", source, false).unwrap();
        assert_eq!(metadata.identity.id, "com.example.notes");
        assert_eq!(
            metadata.identity.capabilities,
            [
                Capability::Clock,
                Capability::FsRead(String::from("/user/saves"))
            ]
        );
    }
}
