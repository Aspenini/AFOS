use afos_api::{AppIdentity, AppRuntime, Error, Result, StorageEntry, SystemApi, SystemInfo};
use afos_runtime_rhai::RhaiRuntime;

struct FakeApi {
    app: AppIdentity,
    args: Vec<String>,
    output: String,
    file: String,
    cancelled: bool,
}

impl FakeApi {
    fn new() -> Self {
        Self {
            app: AppIdentity {
                id: String::from("test"),
                source_path: String::from("/apps/test.rhai"),
                trusted: false,
                api_version: 1,
                capabilities: Vec::new(),
            },
            args: vec![String::from("test"), String::from("argument")],
            output: String::new(),
            file: String::new(),
            cancelled: false,
        }
    }
}

impl SystemApi for FakeApi {
    fn app(&self) -> &AppIdentity {
        &self.app
    }
    fn args(&self) -> &[String] {
        &self.args
    }
    #[allow(clippy::unnecessary_literal_bound)]
    fn cwd(&self) -> &str {
        "/user"
    }
    fn console_write(&mut self, text: &str) -> Result<()> {
        self.output.push_str(text);
        Ok(())
    }
    fn console_read_line(&mut self, _prompt: &str) -> Result<String> {
        Ok(String::from("input"))
    }
    fn read_text(&mut self, _path: &str) -> Result<String> {
        Ok(self.file.clone())
    }
    fn write_text(&mut self, _path: &str, text: &str) -> Result<()> {
        self.file = String::from(text);
        Ok(())
    }
    fn list(&mut self, _path: &str) -> Result<Vec<StorageEntry>> {
        Ok(Vec::new())
    }
    fn create_dir_all(&mut self, _path: &str) -> Result<()> {
        Ok(())
    }
    fn remove(&mut self, _path: &str) -> Result<()> {
        Ok(())
    }
    fn rename(&mut self, _from: &str, _to: &str) -> Result<()> {
        Ok(())
    }
    fn appdata_read(&mut self, _path: &str) -> Result<String> {
        Ok(self.file.clone())
    }
    fn appdata_write(&mut self, _path: &str, text: &str) -> Result<()> {
        self.file = String::from(text);
        Ok(())
    }
    fn monotonic_millis(&mut self) -> Result<u64> {
        Ok(42)
    }
    fn system_info(&mut self) -> Result<SystemInfo> {
        Ok(SystemInfo {
            name: String::from("AFOS"),
            version: String::from("test"),
            platform: String::from("fake"),
            architecture: String::from("fake"),
        })
    }
    fn cancelled(&mut self) -> bool {
        self.cancelled
    }
}

#[test]
fn executes_scripts_and_maps_host_errors() {
    let runtime = RhaiRuntime::new();
    let mut api = FakeApi::new();
    let status = runtime
        .execute(
            r#"
                let values = args();
                print(values[1]);
                appdata_write("state", "saved");
                if appdata_read("state") == "saved" { 7 } else { 1 }
            "#,
            &mut api,
        )
        .unwrap();
    assert_eq!(status, 7);
    assert_eq!(api.output, "argument\n");
    assert_eq!(api.file, "saved");

    api.cancelled = true;
    assert!(matches!(
        runtime.execute("loop { }", &mut api),
        Err(Error::Runtime(_))
    ));
}
