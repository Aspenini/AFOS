use core::{
    alloc::Layout,
    ptr::NonNull,
    sync::atomic::{AtomicU64, Ordering},
};
use virtio_drivers::{BufferDirection, Hal, PhysAddr};

static HHDM_OFFSET: AtomicU64 = AtomicU64::new(0);
static EXECUTABLE_PHYSICAL: AtomicU64 = AtomicU64::new(0);
static EXECUTABLE_VIRTUAL: AtomicU64 = AtomicU64::new(0);

pub struct KernelHal;

pub fn initialize(hhdm: u64, executable_address: Option<(u64, u64)>) {
    HHDM_OFFSET.store(hhdm, Ordering::Relaxed);
    if let Some((physical, virtual_)) = executable_address {
        EXECUTABLE_PHYSICAL.store(physical, Ordering::Relaxed);
        EXECUTABLE_VIRTUAL.store(virtual_, Ordering::Relaxed);
    }
}

pub fn phys_to_virt(address: u64) -> Option<NonNull<u8>> {
    let virtual_address = address.checked_add(HHDM_OFFSET.load(Ordering::Relaxed))?;
    NonNull::new(usize::try_from(virtual_address).ok()? as *mut u8)
}

fn virt_to_phys(address: usize) -> Option<u64> {
    let address = address as u64;
    let executable_virtual = EXECUTABLE_VIRTUAL.load(Ordering::Relaxed);
    let executable_physical = EXECUTABLE_PHYSICAL.load(Ordering::Relaxed);
    if executable_virtual != 0 && address >= executable_virtual {
        return address
            .checked_sub(executable_virtual)?
            .checked_add(executable_physical);
    }
    let hhdm = HHDM_OFFSET.load(Ordering::Relaxed);
    address.checked_sub(hhdm)
}

// SAFETY: AFOS uses a physically contiguous HHDM-backed heap. The implementation
// translates only HHDM and kernel-image virtual addresses supplied by Limine.
unsafe impl Hal for KernelHal {
    fn dma_alloc(pages: usize, _direction: BufferDirection) -> (PhysAddr, NonNull<u8>) {
        let Some(size) = pages.checked_mul(virtio_drivers::PAGE_SIZE) else {
            return (0, NonNull::dangling());
        };
        let Ok(layout) = Layout::from_size_align(size, virtio_drivers::PAGE_SIZE) else {
            return (0, NonNull::dangling());
        };
        // SAFETY: `layout` is non-zero and valid. The global allocator is initialized
        // before any VirtIO device is discovered.
        let pointer = unsafe { alloc::alloc::alloc_zeroed(layout) };
        let Some(pointer) = NonNull::new(pointer) else {
            return (0, NonNull::dangling());
        };
        let Some(physical) = virt_to_phys(pointer.as_ptr() as usize) else {
            // SAFETY: `pointer` was allocated with this exact layout above.
            unsafe { alloc::alloc::dealloc(pointer.as_ptr(), layout) };
            return (0, NonNull::dangling());
        };
        (physical, pointer)
    }

    unsafe fn dma_dealloc(_paddr: PhysAddr, vaddr: NonNull<u8>, pages: usize) -> i32 {
        let Some(size) = pages.checked_mul(virtio_drivers::PAGE_SIZE) else {
            return -1;
        };
        let Ok(layout) = Layout::from_size_align(size, virtio_drivers::PAGE_SIZE) else {
            return -1;
        };
        // SAFETY: The VirtIO driver returns the pointer and page count from `dma_alloc`.
        unsafe { alloc::alloc::dealloc(vaddr.as_ptr(), layout) };
        0
    }

    unsafe fn mmio_phys_to_virt(paddr: PhysAddr, _size: usize) -> NonNull<u8> {
        phys_to_virt(paddr).expect("VirtIO MMIO address is outside the HHDM")
    }

    unsafe fn share(buffer: NonNull<[u8]>, direction: BufferDirection) -> PhysAddr {
        let length = buffer.len();
        let layout =
            Layout::from_size_align(length, 16).expect("valid VirtIO bounce-buffer layout");
        // SAFETY: VirtIO never shares an empty buffer.
        let bounce = unsafe { alloc::alloc::alloc_zeroed(layout) };
        let Some(bounce) = NonNull::new(bounce) else {
            alloc::alloc::handle_alloc_error(layout);
        };
        if matches!(
            direction,
            BufferDirection::DriverToDevice | BufferDirection::Both
        ) {
            // SAFETY: Both buffers are valid for `length` bytes and do not overlap.
            unsafe {
                core::ptr::copy_nonoverlapping(
                    buffer.as_ptr().cast::<u8>(),
                    bounce.as_ptr(),
                    length,
                );
            }
        }
        virt_to_phys(bounce.as_ptr() as usize)
            .expect("VirtIO bounce buffer is outside the HHDM heap")
    }

    unsafe fn unshare(paddr: PhysAddr, buffer: NonNull<[u8]>, direction: BufferDirection) {
        let length = buffer.len();
        let bounce = phys_to_virt(paddr).expect("VirtIO returned an invalid bounce-buffer address");
        if matches!(
            direction,
            BufferDirection::DeviceToDriver | BufferDirection::Both
        ) {
            // SAFETY: Both buffers are valid for `length` bytes and do not overlap.
            unsafe {
                core::ptr::copy_nonoverlapping(
                    bounce.as_ptr(),
                    buffer.as_ptr().cast::<u8>(),
                    length,
                );
            }
        }
        let layout =
            Layout::from_size_align(length, 16).expect("valid VirtIO bounce-buffer layout");
        // SAFETY: `bounce` was allocated by `share` with this exact layout.
        unsafe { alloc::alloc::dealloc(bounce.as_ptr(), layout) };
    }
}
