#![no_std]

extern crate alloc;

use alloc::{
    boxed::Box,
    collections::{BTreeMap, BTreeSet},
    string::String,
    vec,
    vec::Vec,
};
use core::fmt;

pub const BLOCK_SIZE: usize = 512;
const MAGIC: &[u8; 8] = b"AFOSFS01";
const FORMAT_VERSION: u32 = 1;
const HEADER_SIZE: usize = BLOCK_SIZE;
const MAX_PATH_LEN: usize = 4096;
const MAX_ENTRY_COUNT: usize = 100_000;

pub trait BlockDevice {
    fn block_count(&self) -> u64;
    fn read_blocks(&mut self, first_block: u64, output: &mut [u8]) -> Result<(), StorageError>;
    fn write_blocks(&mut self, first_block: u64, data: &[u8]) -> Result<(), StorageError>;
    fn flush(&mut self) -> Result<(), StorageError>;
}

impl<T: BlockDevice + ?Sized> BlockDevice for Box<T> {
    fn block_count(&self) -> u64 {
        (**self).block_count()
    }

    fn read_blocks(&mut self, first_block: u64, output: &mut [u8]) -> Result<(), StorageError> {
        (**self).read_blocks(first_block, output)
    }

    fn write_blocks(&mut self, first_block: u64, data: &[u8]) -> Result<(), StorageError> {
        (**self).write_blocks(first_block, data)
    }

    fn flush(&mut self) -> Result<(), StorageError> {
        (**self).flush()
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct FileTree {
    pub files: BTreeMap<String, Vec<u8>>,
    pub directories: BTreeSet<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum StorageError {
    Device(String),
    DeviceTooSmall,
    SnapshotTooLarge,
    CorruptSnapshot,
    UnsupportedVersion(u32),
}

impl fmt::Display for StorageError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Device(message) => write!(formatter, "block device: {message}"),
            Self::DeviceTooSmall => formatter.write_str("block device is too small"),
            Self::SnapshotTooLarge => formatter.write_str("filesystem snapshot is too large"),
            Self::CorruptSnapshot => formatter.write_str("filesystem snapshot is corrupt"),
            Self::UnsupportedVersion(version) => {
                write!(formatter, "unsupported filesystem version {version}")
            }
        }
    }
}

pub struct SnapshotStore<D> {
    device: D,
    slot_blocks: u64,
    active_slot: Option<u64>,
    generation: u64,
}

impl<D: BlockDevice> SnapshotStore<D> {
    pub fn open(mut device: D) -> Result<(Self, Option<FileTree>), StorageError> {
        let slot_blocks = device.block_count() / 2;
        if slot_blocks < 2 {
            return Err(StorageError::DeviceTooSmall);
        }

        let first = read_slot(&mut device, 0, slot_blocks)?;
        let second = read_slot(&mut device, 1, slot_blocks)?;
        let selected = match (first, second) {
            (Some(left), Some(right)) => {
                if right.generation > left.generation {
                    Some((1, right))
                } else {
                    Some((0, left))
                }
            }
            (Some(snapshot), None) => Some((0, snapshot)),
            (None, Some(snapshot)) => Some((1, snapshot)),
            (None, None) => None,
        };
        let (active_slot, generation, tree) = selected
            .map_or((None, 0, None), |(slot, snapshot)| {
                (Some(slot), snapshot.generation, Some(snapshot.tree))
            });
        Ok((
            Self {
                device,
                slot_blocks,
                active_slot,
                generation,
            },
            tree,
        ))
    }

    pub fn save(&mut self, tree: &FileTree) -> Result<(), StorageError> {
        let payload = encode_tree(tree)?;
        let payload_blocks = payload.len().div_ceil(BLOCK_SIZE);
        let available_blocks =
            usize::try_from(self.slot_blocks - 1).map_err(|_| StorageError::SnapshotTooLarge)?;
        if payload_blocks > available_blocks {
            return Err(StorageError::SnapshotTooLarge);
        }

        let target_slot = self.active_slot.map_or(0, |slot| 1 - slot);
        let slot_start = target_slot * self.slot_blocks;
        let mut padded = vec![0_u8; payload_blocks * BLOCK_SIZE];
        padded[..payload.len()].copy_from_slice(&payload);
        if !padded.is_empty() {
            self.device.write_blocks(slot_start + 1, &padded)?;
        }
        self.device.flush()?;

        let generation = self.generation.saturating_add(1);
        let header = encode_header(generation, payload.len() as u64, checksum(&payload));
        self.device.write_blocks(slot_start, &header)?;
        self.device.flush()?;
        self.active_slot = Some(target_slot);
        self.generation = generation;
        Ok(())
    }

    #[must_use]
    pub fn into_device(self) -> D {
        self.device
    }
}

struct DecodedSnapshot {
    generation: u64,
    tree: FileTree,
}

fn read_slot<D: BlockDevice>(
    device: &mut D,
    slot: u64,
    slot_blocks: u64,
) -> Result<Option<DecodedSnapshot>, StorageError> {
    let slot_start = slot * slot_blocks;
    let mut header = [0_u8; HEADER_SIZE];
    device.read_blocks(slot_start, &mut header)?;
    if header.iter().all(|byte| *byte == 0) {
        return Ok(None);
    }
    if &header[..MAGIC.len()] != MAGIC {
        return Ok(None);
    }
    let version = read_u32(&header, 8)?;
    if version != FORMAT_VERSION {
        return Err(StorageError::UnsupportedVersion(version));
    }
    let generation = read_u64(&header, 16)?;
    let payload_len =
        usize::try_from(read_u64(&header, 24)?).map_err(|_| StorageError::CorruptSnapshot)?;
    let expected_checksum = read_u64(&header, 32)?;
    let payload_blocks = payload_len.div_ceil(BLOCK_SIZE);
    let available_blocks =
        usize::try_from(slot_blocks - 1).map_err(|_| StorageError::CorruptSnapshot)?;
    if payload_blocks > available_blocks {
        return Ok(None);
    }
    let mut padded = vec![0_u8; payload_blocks * BLOCK_SIZE];
    if !padded.is_empty() {
        device.read_blocks(slot_start + 1, &mut padded)?;
    }
    let payload = &padded[..payload_len];
    if checksum(payload) != expected_checksum {
        return Ok(None);
    }
    let tree = decode_tree(payload)?;
    Ok(Some(DecodedSnapshot { generation, tree }))
}

fn encode_header(generation: u64, payload_len: u64, payload_checksum: u64) -> [u8; HEADER_SIZE] {
    let mut header = [0_u8; HEADER_SIZE];
    header[..8].copy_from_slice(MAGIC);
    header[8..12].copy_from_slice(&FORMAT_VERSION.to_le_bytes());
    header[16..24].copy_from_slice(&generation.to_le_bytes());
    header[24..32].copy_from_slice(&payload_len.to_le_bytes());
    header[32..40].copy_from_slice(&payload_checksum.to_le_bytes());
    header
}

fn encode_tree(tree: &FileTree) -> Result<Vec<u8>, StorageError> {
    let mut output = Vec::new();
    write_count(&mut output, tree.directories.len())?;
    for path in &tree.directories {
        write_string(&mut output, path)?;
    }
    write_count(&mut output, tree.files.len())?;
    for (path, data) in &tree.files {
        write_string(&mut output, path)?;
        output.extend_from_slice(
            &u64::try_from(data.len())
                .map_err(|_| StorageError::SnapshotTooLarge)?
                .to_le_bytes(),
        );
        output.extend_from_slice(data);
    }
    Ok(output)
}

fn decode_tree(payload: &[u8]) -> Result<FileTree, StorageError> {
    let mut cursor = 0;
    let directory_count = read_count(payload, &mut cursor)?;
    let mut directories = BTreeSet::new();
    for _ in 0..directory_count {
        directories.insert(read_string(payload, &mut cursor)?);
    }
    let file_count = read_count(payload, &mut cursor)?;
    let mut files = BTreeMap::new();
    for _ in 0..file_count {
        let path = read_string(payload, &mut cursor)?;
        let length = usize::try_from(take_u64(payload, &mut cursor)?)
            .map_err(|_| StorageError::CorruptSnapshot)?;
        let data = take(payload, &mut cursor, length)?.to_vec();
        files.insert(path, data);
    }
    if cursor != payload.len() {
        return Err(StorageError::CorruptSnapshot);
    }
    Ok(FileTree { files, directories })
}

fn write_count(output: &mut Vec<u8>, count: usize) -> Result<(), StorageError> {
    if count > MAX_ENTRY_COUNT {
        return Err(StorageError::SnapshotTooLarge);
    }
    output.extend_from_slice(
        &u32::try_from(count)
            .map_err(|_| StorageError::SnapshotTooLarge)?
            .to_le_bytes(),
    );
    Ok(())
}

fn write_string(output: &mut Vec<u8>, value: &str) -> Result<(), StorageError> {
    if value.len() > MAX_PATH_LEN {
        return Err(StorageError::SnapshotTooLarge);
    }
    output.extend_from_slice(
        &u32::try_from(value.len())
            .map_err(|_| StorageError::SnapshotTooLarge)?
            .to_le_bytes(),
    );
    output.extend_from_slice(value.as_bytes());
    Ok(())
}

fn read_count(input: &[u8], cursor: &mut usize) -> Result<usize, StorageError> {
    let count =
        usize::try_from(take_u32(input, cursor)?).map_err(|_| StorageError::CorruptSnapshot)?;
    if count > MAX_ENTRY_COUNT {
        return Err(StorageError::CorruptSnapshot);
    }
    Ok(count)
}

fn read_string(input: &[u8], cursor: &mut usize) -> Result<String, StorageError> {
    let length =
        usize::try_from(take_u32(input, cursor)?).map_err(|_| StorageError::CorruptSnapshot)?;
    if length > MAX_PATH_LEN {
        return Err(StorageError::CorruptSnapshot);
    }
    let bytes = take(input, cursor, length)?;
    String::from_utf8(bytes.to_vec()).map_err(|_| StorageError::CorruptSnapshot)
}

fn take<'a>(input: &'a [u8], cursor: &mut usize, length: usize) -> Result<&'a [u8], StorageError> {
    let end = cursor
        .checked_add(length)
        .ok_or(StorageError::CorruptSnapshot)?;
    let bytes = input
        .get(*cursor..end)
        .ok_or(StorageError::CorruptSnapshot)?;
    *cursor = end;
    Ok(bytes)
}

fn take_u32(input: &[u8], cursor: &mut usize) -> Result<u32, StorageError> {
    let bytes: [u8; 4] = take(input, cursor, 4)?
        .try_into()
        .map_err(|_| StorageError::CorruptSnapshot)?;
    Ok(u32::from_le_bytes(bytes))
}

fn take_u64(input: &[u8], cursor: &mut usize) -> Result<u64, StorageError> {
    let bytes: [u8; 8] = take(input, cursor, 8)?
        .try_into()
        .map_err(|_| StorageError::CorruptSnapshot)?;
    Ok(u64::from_le_bytes(bytes))
}

fn read_u32(input: &[u8], offset: usize) -> Result<u32, StorageError> {
    let mut cursor = offset;
    take_u32(input, &mut cursor)
}

fn read_u64(input: &[u8], offset: usize) -> Result<u64, StorageError> {
    let mut cursor = offset;
    take_u64(input, &mut cursor)
}

fn checksum(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf2_9ce4_8422_2325_u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}

#[cfg(test)]
mod tests {
    use super::*;

    struct MemoryDevice {
        blocks: Vec<u8>,
    }

    impl MemoryDevice {
        fn new(blocks: usize) -> Self {
            Self {
                blocks: vec![0; blocks * BLOCK_SIZE],
            }
        }
    }

    impl BlockDevice for MemoryDevice {
        fn block_count(&self) -> u64 {
            (self.blocks.len() / BLOCK_SIZE) as u64
        }

        fn read_blocks(&mut self, first_block: u64, output: &mut [u8]) -> Result<(), StorageError> {
            let start = usize::try_from(first_block).unwrap() * BLOCK_SIZE;
            output.copy_from_slice(&self.blocks[start..start + output.len()]);
            Ok(())
        }

        fn write_blocks(&mut self, first_block: u64, data: &[u8]) -> Result<(), StorageError> {
            let start = usize::try_from(first_block).unwrap() * BLOCK_SIZE;
            self.blocks[start..start + data.len()].copy_from_slice(data);
            Ok(())
        }

        fn flush(&mut self) -> Result<(), StorageError> {
            Ok(())
        }
    }

    #[test]
    fn persists_latest_tree_across_reopen() {
        let (mut store, initial) = SnapshotStore::open(MemoryDevice::new(64)).unwrap();
        assert!(initial.is_none());
        let mut tree = FileTree::default();
        tree.directories.insert(String::from("user/saves"));
        tree.files
            .insert(String::from("user/saves/test"), b"first".to_vec());
        store.save(&tree).unwrap();
        tree.files
            .insert(String::from("user/saves/test"), b"second".to_vec());
        store.save(&tree).unwrap();

        let (_, loaded) = SnapshotStore::open(store.into_device()).unwrap();
        assert_eq!(loaded, Some(tree));
    }

    #[test]
    fn falls_back_to_previous_snapshot_when_latest_payload_is_damaged() {
        let (mut store, _) = SnapshotStore::open(MemoryDevice::new(64)).unwrap();
        let mut first = FileTree::default();
        first
            .files
            .insert(String::from("user/saves/test"), b"first".to_vec());
        store.save(&first).unwrap();
        let mut second = first.clone();
        second
            .files
            .insert(String::from("user/saves/test"), b"second".to_vec());
        store.save(&second).unwrap();
        let mut device = store.into_device();
        let second_slot_payload = 32 * BLOCK_SIZE + BLOCK_SIZE;
        device.blocks[second_slot_payload] ^= 0xff;

        let (_, loaded) = SnapshotStore::open(device).unwrap();
        assert_eq!(loaded, Some(first));
    }
}
