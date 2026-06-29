use afos_api::{
    AppIdentity, AppRuntime, Error, NetStatus, Result, StorageEntry, SystemApi, SystemInfo,
};
use afos_runtime_rhai::RhaiRuntime;
use std::sync::{Mutex, MutexGuard};

/// The Rhai runtime installs the active API into a single global slot and
/// rejects nested execution, so tests that evaluate scripts must not overlap.
static RUNTIME_LOCK: Mutex<()> = Mutex::new(());

fn serialized() -> MutexGuard<'static, ()> {
    RUNTIME_LOCK
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}

struct FakeApi {
    app: AppIdentity,
    args: Vec<String>,
    output: String,
    file: String,
    cancelled: bool,
    net_sent: Vec<u8>,
    net_response: Vec<u8>,
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
            net_sent: Vec::new(),
            net_response: Vec::new(),
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
    fn net_status(&mut self) -> Result<NetStatus> {
        Ok(NetStatus {
            link_up: true,
            mac: String::from("52:54:00:12:34:56"),
            address: Some(String::from("10.0.0.2/24")),
            gateway: Some(String::from("10.0.0.1")),
        })
    }
    fn net_connect(&mut self, _host: &str, _port: u16) -> Result<u64> {
        Ok(1)
    }
    fn net_send(&mut self, _handle: u64, data: &[u8]) -> Result<usize> {
        self.net_sent.extend_from_slice(data);
        Ok(data.len())
    }
    fn net_recv(&mut self, _handle: u64, max: usize) -> Result<Vec<u8>> {
        let count = max.min(self.net_response.len());
        Ok(self.net_response.drain(..count).collect())
    }
    fn net_close(&mut self, _handle: u64) -> Result<()> {
        Ok(())
    }
    fn cancelled(&mut self) -> bool {
        self.cancelled
    }
}

#[test]
fn executes_scripts_and_maps_host_errors() {
    let _guard = serialized();
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

#[test]
fn exposes_networking_to_scripts() {
    let _guard = serialized();
    let runtime = RhaiRuntime::new();
    let mut api = FakeApi::new();
    api.net_response = b"HTTP/1.1 200 OK".to_vec();
    let status = runtime
        .execute(
            r#"
                let info = net_status();
                print(info.address);
                let socket = net_connect("example.com", 80);
                net_send(socket, "GET / HTTP/1.1\r\n");
                let reply = net_recv(socket, 64);
                net_close(socket);
                if reply.contains("200 OK") { 0 } else { 1 }
            "#,
            &mut api,
        )
        .unwrap();
    assert_eq!(status, 0);
    assert_eq!(api.output, "10.0.0.2/24\n");
    assert_eq!(api.net_sent, b"GET / HTTP/1.1\r\n");
}
