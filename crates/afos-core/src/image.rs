use afos_api::{Error, Result};
use alloc::{boxed::Box, string::String, vec::Vec};
use tar_no_std::TarArchiveRef;

use crate::EmbeddedFile;

/// Parse a ustar system image supplied by a platform.
///
/// The backing bytes must remain valid for the lifetime of AFOS. Bootloaders
/// normally satisfy this by loading the module into reserved memory.
pub fn load_system_image(image: &'static [u8]) -> Result<&'static [EmbeddedFile]> {
    let archive = TarArchiveRef::new(image)
        .map_err(|_| Error::InvalidInput(String::from("invalid AFOS system image")))?;
    let mut files = Vec::new();
    for entry in archive.entries() {
        let filename = entry.filename();
        let name = filename
            .as_str()
            .map_err(|_| Error::InvalidInput(String::from("non-UTF-8 system image path")))?;
        let name = name.trim_start_matches("./").trim_start_matches('/');
        if name.is_empty() {
            continue;
        }
        if name.contains('\\') || name.split('/').any(|part| matches!(part, "" | "." | "..")) {
            return Err(Error::InvalidPath);
        }
        let path: &'static str = Box::leak(format_path(name).into_boxed_str());
        files.push(EmbeddedFile {
            path,
            data: entry.data(),
        });
    }
    if files.is_empty() {
        return Err(Error::InvalidInput(String::from(
            "AFOS system image contains no files",
        )));
    }
    Ok(Box::leak(files.into_boxed_slice()))
}

fn format_path(name: &str) -> String {
    let mut path = String::from("/");
    path.push_str(name);
    path
}
