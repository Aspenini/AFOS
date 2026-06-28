use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
    time::SystemTime,
};

fn temp_root() -> PathBuf {
    std::env::temp_dir().join(format!(
        "afos-cli-test-{}-{}",
        std::process::id(),
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ))
}

fn afos(root: &Path, command: &str) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_afos"))
        .args(["--data-dir", root.to_str().unwrap(), "--command", command])
        .output()
        .unwrap()
}

#[test]
fn executes_bundled_apps_and_persists_files() {
    let root = temp_root();
    let hello = afos(&root, "hello");
    assert!(hello.status.success());
    assert!(String::from_utf8_lossy(&hello.stdout).contains("portable AFOS Rhai"));

    fs::write(
        root.join("apps").join("hello.rhai"),
        "print(\"shadowed\"); 0",
    )
    .unwrap();
    let protected_resolution = afos(&root, "hello");
    assert!(protected_resolution.status.success());
    let protected_output = String::from_utf8_lossy(&protected_resolution.stdout);
    assert!(protected_output.contains("portable AFOS Rhai"));
    assert!(!protected_output.contains("shadowed"));

    assert!(afos(&root, "mkdir /user/saves/project").status.success());
    assert!(
        afos(&root, "touch /user/saves/project/note.txt")
            .status
            .success()
    );
    let listing = afos(&root, "ls /user/saves/project");
    assert!(listing.status.success());
    assert!(String::from_utf8_lossy(&listing.stdout).contains("note.txt"));

    fs::remove_dir_all(root).unwrap();
}
