use afos_storage::{BlockDevice, StorageError};
use alloc::{boxed::Box, format, string::String, vec::Vec};
use virtio_drivers::{
    device::{
        blk::{SECTOR_SIZE, VirtIOBlk},
        net::VirtIONet,
        rng::VirtIORng,
    },
    transport::{DeviceType, Transport},
};

use crate::net::NetDevice;

/// `VirtIO` network receive/transmit ring depth. Legacy virtqueues are
/// fixed-size, so this must match `QEMU`'s default `virtio-net` queue size of
/// 256.
const NET_QUEUE_SIZE: usize = 256;
/// Per-buffer length: a full Ethernet frame plus the `VirtIO` network header.
const NET_BUFFER_LEN: usize = 2048;

#[cfg(target_arch = "aarch64")]
use virtio_drivers::transport::mmio::{MmioTransport, VirtIOHeader};
#[cfg(target_arch = "x86_64")]
use virtio_drivers::transport::pci::{
    bus::{BarInfo, Command, ConfigurationAccess, DeviceFunction, PciRoot},
    virtio_device_type,
};
#[cfg(target_arch = "x86_64")]
use zerocopy::{FromBytes, Immutable, IntoBytes};

#[cfg(target_arch = "aarch64")]
use crate::memory;
use crate::memory::KernelHal;

pub trait EntropySource {
    fn fill(&mut self, output: &mut [u8]) -> Result<(), String>;
}

pub struct DeviceSet {
    pub block: Option<Box<dyn BlockDevice>>,
    pub entropy: Option<Box<dyn EntropySource>>,
    pub net: Option<Box<dyn NetDevice>>,
}

impl DeviceSet {
    fn empty() -> Self {
        Self {
            block: None,
            entropy: None,
            net: None,
        }
    }

    fn complete(&self) -> bool {
        self.block.is_some() && self.entropy.is_some() && self.net.is_some()
    }

    pub fn discover() -> Self {
        #[cfg(target_arch = "x86_64")]
        return discover_pci();
        // AArch64 VirtIO-MMIO discovery is gated until the kernel maps the
        // device-MMIO window as Device-nGnRnE memory and performs the DMA cache
        // maintenance that VirtIO rings require on this architecture. Until then
        // the kernel boots with the in-memory filesystem instead of faulting on
        // an unmapped probe. See docs/BARE_METAL.md.
        #[cfg(target_arch = "aarch64")]
        return if aarch64_mmio_ready() {
            discover_mmio()
        } else {
            Self::empty()
        };
    }
}

/// Whether the `AArch64` kernel has mapped the `VirtIO`-MMIO window and is ready
/// to drive DMA-capable `VirtIO` devices. This stays `false` until the
/// device-memory mapping and ring cache maintenance land; see
/// [`DeviceSet::discover`].
#[cfg(target_arch = "aarch64")]
const fn aarch64_mmio_ready() -> bool {
    false
}

struct VirtioBlock {
    driver: VirtIOBlk<KernelHal, DeviceTransport>,
}

impl BlockDevice for VirtioBlock {
    fn block_count(&self) -> u64 {
        self.driver.capacity()
    }

    fn read_blocks(&mut self, first_block: u64, output: &mut [u8]) -> Result<(), StorageError> {
        if output.is_empty() || !output.len().is_multiple_of(SECTOR_SIZE) {
            return Err(StorageError::Device(String::from(
                "unaligned VirtIO block read",
            )));
        }
        let first_block = usize::try_from(first_block)
            .map_err(|_| StorageError::Device(String::from("block address is too large")))?;
        self.driver
            .read_blocks(first_block, output)
            .map_err(|error| StorageError::Device(format!("VirtIO read: {error:?}")))
    }

    fn write_blocks(&mut self, first_block: u64, data: &[u8]) -> Result<(), StorageError> {
        if data.is_empty() || !data.len().is_multiple_of(SECTOR_SIZE) {
            return Err(StorageError::Device(String::from(
                "unaligned VirtIO block write",
            )));
        }
        let first_block = usize::try_from(first_block)
            .map_err(|_| StorageError::Device(String::from("block address is too large")))?;
        self.driver
            .write_blocks(first_block, data)
            .map_err(|error| StorageError::Device(format!("VirtIO write: {error:?}")))
    }

    fn flush(&mut self) -> Result<(), StorageError> {
        self.driver
            .flush()
            .map_err(|error| StorageError::Device(format!("VirtIO flush: {error:?}")))
    }
}

struct VirtioNet {
    driver: VirtIONet<KernelHal, DeviceTransport, NET_QUEUE_SIZE>,
}

impl NetDevice for VirtioNet {
    fn mac(&self) -> [u8; 6] {
        self.driver.mac_address()
    }

    fn can_send(&self) -> bool {
        self.driver.can_send()
    }

    fn receive(&mut self) -> Option<Vec<u8>> {
        if !self.driver.can_recv() {
            return None;
        }
        let buffer = self.driver.receive().ok()?;
        let frame = buffer.packet().to_vec();
        let _ = self.driver.recycle_rx_buffer(buffer);
        Some(frame)
    }

    fn transmit(&mut self, frame: &[u8]) {
        let mut buffer = self.driver.new_tx_buffer(frame.len());
        buffer.packet_mut().copy_from_slice(frame);
        let _ = self.driver.send(buffer);
    }
}

struct VirtioEntropy {
    driver: VirtIORng<KernelHal, DeviceTransport>,
}

impl EntropySource for VirtioEntropy {
    fn fill(&mut self, output: &mut [u8]) -> Result<(), String> {
        let mut offset = 0;
        while offset < output.len() {
            let count = self
                .driver
                .request_entropy(&mut output[offset..])
                .map_err(|error| format!("VirtIO entropy: {error:?}"))?;
            if count == 0 {
                return Err(String::from("VirtIO entropy device returned no bytes"));
            }
            offset += count.min(output.len() - offset);
        }
        Ok(())
    }
}

fn add_transport(transport: DeviceTransport, devices: &mut DeviceSet) -> Result<(), String> {
    match transport.device_type() {
        DeviceType::Block if devices.block.is_none() => {
            let driver = VirtIOBlk::<KernelHal, _>::new(transport)
                .map_err(|error| format!("VirtIO block initialization: {error:?}"))?;
            if driver.readonly() {
                return Err(String::from("AFOS VirtIO data disk is read-only"));
            }
            devices.block = Some(Box::new(VirtioBlock { driver }));
        }
        DeviceType::EntropySource if devices.entropy.is_none() => {
            let driver = VirtIORng::<KernelHal, _>::new(transport)
                .map_err(|error| format!("VirtIO entropy initialization: {error:?}"))?;
            devices.entropy = Some(Box::new(VirtioEntropy { driver }));
        }
        DeviceType::Network if devices.net.is_none() => {
            let driver = VirtIONet::<KernelHal, _, NET_QUEUE_SIZE>::new(transport, NET_BUFFER_LEN)
                .map_err(|error| format!("VirtIO network initialization: {error:?}"))?;
            devices.net = Some(Box::new(VirtioNet { driver }));
        }
        _ => {}
    }
    Ok(())
}

#[cfg(target_arch = "aarch64")]
fn discover_mmio() -> DeviceSet {
    const VIRTIO_MMIO_BASE: u64 = 0x0a00_0000;
    const VIRTIO_MMIO_STRIDE: u64 = 0x200;
    const VIRTIO_MMIO_SIZE: usize = 0x200;
    const VIRTIO_MMIO_SLOTS: u64 = 32;

    let mut devices = DeviceSet::empty();
    for slot in 0..VIRTIO_MMIO_SLOTS {
        let physical = VIRTIO_MMIO_BASE + slot * VIRTIO_MMIO_STRIDE;
        let Some(pointer) = memory::phys_to_virt(physical) else {
            continue;
        };
        let header = pointer.cast::<VirtIOHeader>();
        // SAFETY: QEMU's `virt` machine maps a 0x200-byte VirtIO MMIO transport
        // at every scanned address. Invalid/unused transports are rejected.
        let Ok(transport) = (unsafe { MmioTransport::new(header, VIRTIO_MMIO_SIZE) }) else {
            continue;
        };
        let device_type = transport.device_type();
        if matches!(
            device_type,
            DeviceType::Block | DeviceType::EntropySource | DeviceType::Network
        ) {
            let _ = add_transport(transport, &mut devices);
        }
        if devices.complete() {
            break;
        }
    }
    devices
}

#[cfg(target_arch = "aarch64")]
type DeviceTransport = MmioTransport<'static>;

#[cfg(target_arch = "x86_64")]
fn discover_pci() -> DeviceSet {
    let mut root = PciRoot::new(PciIo);
    let candidates: Vec<_> = root.enumerate_bus(0).collect();
    let mut devices = DeviceSet::empty();
    for (function, information) in candidates {
        let Some(device_type) = virtio_device_type(&information) else {
            continue;
        };
        if !matches!(
            device_type,
            DeviceType::Block | DeviceType::EntropySource | DeviceType::Network
        ) {
            continue;
        }
        let (_, command) = root.get_status_command(function);
        root.set_command(function, command | Command::IO_SPACE | Command::BUS_MASTER);
        let Ok(Some(BarInfo::IO { address, .. })) = root.bar_info(function, 0) else {
            continue;
        };
        root.set_command(function, command | Command::IO_SPACE | Command::BUS_MASTER);
        let Ok(io_base) = u16::try_from(address) else {
            continue;
        };
        let _ = add_transport(
            LegacyPciTransport {
                device_type,
                io_base,
            },
            &mut devices,
        );
        if devices.complete() {
            break;
        }
    }
    devices
}

#[cfg(target_arch = "x86_64")]
type DeviceTransport = LegacyPciTransport;

#[cfg(target_arch = "x86_64")]
struct LegacyPciTransport {
    device_type: DeviceType,
    io_base: u16,
}

#[cfg(target_arch = "x86_64")]
impl LegacyPciTransport {
    const DEVICE_FEATURES: u16 = 0x00;
    const DRIVER_FEATURES: u16 = 0x04;
    const QUEUE_PFN: u16 = 0x08;
    const QUEUE_SIZE: u16 = 0x0c;
    const QUEUE_SELECT: u16 = 0x0e;
    const QUEUE_NOTIFY: u16 = 0x10;
    const DEVICE_STATUS: u16 = 0x12;
    const ISR_STATUS: u16 = 0x13;
    const DEVICE_CONFIG: u16 = 0x14;

    fn port(&self, offset: u16) -> u16 {
        self.io_base + offset
    }

    fn select_queue(&self, queue: u16) {
        // SAFETY: The BAR was reported as an allocated PCI I/O BAR for this device.
        unsafe { out_u16(self.port(Self::QUEUE_SELECT), queue) };
    }
}

#[cfg(target_arch = "x86_64")]
impl Transport for LegacyPciTransport {
    fn device_type(&self) -> DeviceType {
        self.device_type
    }

    fn read_device_features(&mut self) -> u64 {
        // SAFETY: The BAR was reported as an allocated PCI I/O BAR for this device.
        let features = unsafe { in_u32(self.port(Self::DEVICE_FEATURES)) };
        // Keep the first implementation on direct descriptors with unconditional
        // queue notifications; these optional ring optimizations are unnecessary
        // for AFOS's synchronous single-threaded I/O.
        u64::from(features & !((1 << 28) | (1 << 29)))
    }

    fn write_driver_features(&mut self, driver_features: u64) {
        let features = u32::try_from(driver_features & u64::from(u32::MAX)).unwrap_or(0);
        // SAFETY: Legacy devices expose the low 32 feature bits through this port.
        unsafe { out_u32(self.port(Self::DRIVER_FEATURES), features) };
    }

    fn max_queue_size(&mut self, queue: u16) -> u32 {
        self.select_queue(queue);
        // SAFETY: The BAR was reported as an allocated PCI I/O BAR for this device.
        u32::from(unsafe { in_u16(self.port(Self::QUEUE_SIZE)) })
    }

    fn notify(&mut self, queue: u16) {
        // SAFETY: The BAR was reported as an allocated PCI I/O BAR for this device.
        unsafe { out_u16(self.port(Self::QUEUE_NOTIFY), queue) };
    }

    fn get_status(&self) -> virtio_drivers::transport::DeviceStatus {
        // SAFETY: The BAR was reported as an allocated PCI I/O BAR for this device.
        let status = unsafe { in_u8(self.port(Self::DEVICE_STATUS)) };
        virtio_drivers::transport::DeviceStatus::from_bits_truncate(u32::from(status))
    }

    fn set_status(&mut self, status: virtio_drivers::transport::DeviceStatus) {
        // VIRTIO_CONFIG_S_FEATURES_OK was added by the modern specification and
        // must not be written to a legacy transport.
        let status = u8::try_from(status.bits() & !0x08).unwrap_or(u8::MAX);
        // SAFETY: The BAR was reported as an allocated PCI I/O BAR for this device.
        unsafe { out_u8(self.port(Self::DEVICE_STATUS), status) };
    }

    fn set_guest_page_size(&mut self, _guest_page_size: u32) {}

    fn requires_legacy_layout(&self) -> bool {
        true
    }

    fn queue_set(
        &mut self,
        queue: u16,
        _size: u32,
        descriptors: u64,
        _driver_area: u64,
        _device_area: u64,
    ) {
        self.select_queue(queue);
        let page_size = u64::try_from(virtio_drivers::PAGE_SIZE).unwrap_or(4096);
        let page_frame = u32::try_from(descriptors / page_size).unwrap_or(0);
        // SAFETY: Legacy VirtIO queues are activated by writing the descriptor PFN.
        unsafe { out_u32(self.port(Self::QUEUE_PFN), page_frame) };
    }

    fn queue_unset(&mut self, queue: u16) {
        self.select_queue(queue);
        // SAFETY: Writing zero disables the selected legacy VirtIO queue.
        unsafe { out_u32(self.port(Self::QUEUE_PFN), 0) };
    }

    fn queue_used(&mut self, queue: u16) -> bool {
        self.select_queue(queue);
        // SAFETY: The BAR was reported as an allocated PCI I/O BAR for this device.
        unsafe { in_u32(self.port(Self::QUEUE_PFN)) != 0 }
    }

    fn ack_interrupt(&mut self) -> virtio_drivers::transport::InterruptStatus {
        // SAFETY: Reading the ISR acknowledges pending legacy VirtIO interrupts.
        let status = unsafe { in_u8(self.port(Self::ISR_STATUS)) };
        virtio_drivers::transport::InterruptStatus::from_bits_truncate(u32::from(status))
    }

    fn read_config_generation(&self) -> u32 {
        0
    }

    fn read_config_space<T: FromBytes + IntoBytes>(
        &self,
        offset: usize,
    ) -> virtio_drivers::Result<T> {
        let mut value = T::new_zeroed();
        for (index, byte) in value.as_mut_bytes().iter_mut().enumerate() {
            let port_offset =
                u16::try_from(offset + index).map_err(|_| virtio_drivers::Error::InvalidParam)?;
            // SAFETY: Device configuration bytes immediately follow the legacy header.
            *byte = unsafe { in_u8(self.port(Self::DEVICE_CONFIG + port_offset)) };
        }
        Ok(value)
    }

    fn write_config_space<T: IntoBytes + Immutable>(
        &mut self,
        offset: usize,
        value: T,
    ) -> virtio_drivers::Result<()> {
        for (index, byte) in value.as_bytes().iter().copied().enumerate() {
            let port_offset =
                u16::try_from(offset + index).map_err(|_| virtio_drivers::Error::InvalidParam)?;
            // SAFETY: Device configuration bytes immediately follow the legacy header.
            unsafe { out_u8(self.port(Self::DEVICE_CONFIG + port_offset), byte) };
        }
        Ok(())
    }
}

#[cfg(target_arch = "x86_64")]
#[derive(Clone, Copy)]
struct PciIo;

#[cfg(target_arch = "x86_64")]
impl ConfigurationAccess for PciIo {
    fn read_word(&self, function: DeviceFunction, register_offset: u8) -> u32 {
        let address = pci_address(function, register_offset);
        // SAFETY: PCI configuration mechanism 1 owns these architectural ports
        // for the duration of this synchronous access.
        unsafe {
            out_u32(0x0cf8, address);
            in_u32(0x0cfc)
        }
    }

    fn write_word(&mut self, function: DeviceFunction, register_offset: u8, data: u32) {
        let address = pci_address(function, register_offset);
        // SAFETY: PCI configuration mechanism 1 owns these architectural ports
        // for the duration of this synchronous access.
        unsafe {
            out_u32(0x0cf8, address);
            out_u32(0x0cfc, data);
        }
    }

    unsafe fn unsafe_clone(&self) -> Self {
        *self
    }
}

#[cfg(target_arch = "x86_64")]
fn pci_address(function: DeviceFunction, register_offset: u8) -> u32 {
    0x8000_0000
        | (u32::from(function.bus) << 16)
        | (u32::from(function.device) << 11)
        | (u32::from(function.function) << 8)
        | u32::from(register_offset & 0xfc)
}

#[cfg(target_arch = "x86_64")]
unsafe fn out_u8(port: u16, value: u8) {
    // SAFETY: The caller guarantees that this port supports an 8-bit write.
    unsafe {
        core::arch::asm!(
            "out dx, al",
            in("dx") port,
            in("al") value,
            options(nostack, preserves_flags)
        );
    }
}

#[cfg(target_arch = "x86_64")]
unsafe fn in_u8(port: u16) -> u8 {
    let value: u8;
    // SAFETY: The caller guarantees that this port supports an 8-bit read.
    unsafe {
        core::arch::asm!(
            "in al, dx",
            in("dx") port,
            out("al") value,
            options(nostack, preserves_flags)
        );
    }
    value
}

#[cfg(target_arch = "x86_64")]
unsafe fn out_u16(port: u16, value: u16) {
    // SAFETY: The caller guarantees that this port supports a 16-bit write.
    unsafe {
        core::arch::asm!(
            "out dx, ax",
            in("dx") port,
            in("ax") value,
            options(nostack, preserves_flags)
        );
    }
}

#[cfg(target_arch = "x86_64")]
unsafe fn in_u16(port: u16) -> u16 {
    let value: u16;
    // SAFETY: The caller guarantees that this port supports a 16-bit read.
    unsafe {
        core::arch::asm!(
            "in ax, dx",
            in("dx") port,
            out("ax") value,
            options(nostack, preserves_flags)
        );
    }
    value
}

#[cfg(target_arch = "x86_64")]
unsafe fn out_u32(port: u16, value: u32) {
    // SAFETY: The caller guarantees that this port supports a 32-bit write.
    unsafe {
        core::arch::asm!(
            "out dx, eax",
            in("dx") port,
            in("eax") value,
            options(nostack, preserves_flags)
        );
    }
}

#[cfg(target_arch = "x86_64")]
unsafe fn in_u32(port: u16) -> u32 {
    let value: u32;
    // SAFETY: The caller guarantees that this port supports a 32-bit read.
    unsafe {
        core::arch::asm!(
            "in eax, dx",
            in("dx") port,
            out("eax") value,
            options(nostack, preserves_flags)
        );
    }
    value
}
