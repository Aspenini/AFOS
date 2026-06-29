#![cfg_attr(target_os = "none", no_std)]
#![cfg_attr(target_os = "none", no_main)]
#![cfg_attr(target_os = "none", feature(alloc_error_handler))]

#[cfg(target_os = "none")]
extern crate alloc;

#[cfg(target_os = "none")]
mod arch;
#[cfg(target_os = "none")]
mod console;
#[cfg(target_os = "none")]
mod devices;
#[cfg(target_os = "none")]
mod memory;
#[cfg(target_os = "none")]
mod net;
#[cfg(target_os = "none")]
mod platform;

#[cfg(target_os = "none")]
use afos_core::{Afos, EmbeddedFile, ShellConfig, normalize_path};
#[cfg(target_os = "none")]
use afos_runtime_rhai::RhaiRuntime;
#[cfg(target_os = "none")]
use limine::{
    BaseRevision, RequestsEndMarker, RequestsStartMarker,
    request::{
        ExecutableAddressRequest, FramebufferRequest, HhdmRequest, MemmapRequest, ModulesRequest,
        StackSizeRequest, TscFrequencyRequest,
    },
};
#[cfg(target_os = "none")]
use linked_list_allocator::LockedHeap;
#[cfg(target_os = "none")]
use platform::{BareMetalPlatform, InitialFile};

#[cfg(target_os = "none")]
const MAX_HEAP_SIZE: u64 = 128 * 1024 * 1024;
#[cfg(target_os = "none")]
const MIN_HEAP_SIZE: u64 = 8 * 1024 * 1024;
#[cfg(target_os = "none")]
const PAGE_SIZE: u64 = 4096;

#[cfg(target_os = "none")]
#[global_allocator]
static ALLOCATOR: LockedHeap = LockedHeap::empty();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests_start")]
static REQUESTS_START: RequestsStartMarker = RequestsStartMarker::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static BASE_REVISION: BaseRevision = BaseRevision::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static FRAMEBUFFER_REQUEST: FramebufferRequest = FramebufferRequest::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static MODULES_REQUEST: ModulesRequest = ModulesRequest::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static HHDM_REQUEST: HhdmRequest = HhdmRequest::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static MEMMAP_REQUEST: MemmapRequest = MemmapRequest::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static EXECUTABLE_ADDRESS_REQUEST: ExecutableAddressRequest = ExecutableAddressRequest::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static TSC_FREQUENCY_REQUEST: TscFrequencyRequest = TscFrequencyRequest::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static STACK_SIZE_REQUEST: StackSizeRequest = StackSizeRequest::new(1024 * 1024);

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests_end")]
static REQUESTS_END: RequestsEndMarker = RequestsEndMarker::new();

#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
extern "C" fn _start() -> ! {
    // Limine is not required to zero the kernel's `.bss`, and static spinlocks
    // (such as the global allocator's) must start cleared, so do it first before
    // any static is touched.
    zero_bss();
    arch::initialize();

    if !BASE_REVISION.is_supported() {
        fail("unsupported Limine boot protocol revision");
    }
    let (hhdm, executable_address) = initialize_heap();

    let Some(modules) = MODULES_REQUEST.response() else {
        fail("Limine did not provide the AFOS filesystem");
    };
    let Some(boot_files) = load_boot_files(modules.modules()) else {
        fail("Limine filesystem modules are invalid");
    };

    let framebuffer = FRAMEBUFFER_REQUEST
        .response()
        .and_then(|response| response.framebuffers().first().copied());
    let counter_frequency = TSC_FREQUENCY_REQUEST
        .response()
        .map_or(1_000_000, |response| response.frequency.max(1));
    let devices = devices::DeviceSet::discover();

    let platform = match BareMetalPlatform::new(
        framebuffer,
        hhdm,
        executable_address,
        counter_frequency,
        boot_files.mutable,
        devices.block,
        devices.entropy,
        devices.net,
    ) {
        Ok(platform) => platform,
        Err(error) => fail(&alloc::format!("{error}")),
    };
    let mut afos = Afos::with_system_files(platform, boot_files.system);
    if afos
        .register_runtime(alloc::boxed::Box::new(RhaiRuntime::new()))
        .is_err()
    {
        fail("failed to register Rhai");
    }
    if afos
        .run_interactive(&ShellConfig {
            interactive_setup: false,
            banner: true,
        })
        .is_err()
    {
        fail("AFOS stopped after a platform error");
    }
    arch::halt()
}

#[cfg(target_os = "none")]
fn zero_bss() {
    unsafe extern "C" {
        static mut __bss_start: u8;
        static mut __bss_end: u8;
    }
    let start = core::ptr::addr_of_mut!(__bss_start);
    let end = core::ptr::addr_of_mut!(__bss_end);
    // SAFETY: The linker places `__bss_start`/`__bss_end` around the contiguous,
    // writable `.bss` segment that Limine maps for the kernel image.
    let length = unsafe { end.offset_from(start) };
    let length = usize::try_from(length).unwrap_or(0);
    if length > 0 {
        // SAFETY: `start` is valid for `length` writable bytes within `.bss`.
        unsafe {
            core::ptr::write_bytes(start, 0, length);
        }
    }
}

#[cfg(target_os = "none")]
fn initialize_heap() -> (u64, Option<(u64, u64)>) {
    let Some(hhdm) = HHDM_REQUEST.response().map(|response| response.offset) else {
        fail("Limine did not provide a higher-half direct map");
    };
    let executable_address = EXECUTABLE_ADDRESS_REQUEST
        .response()
        .map(|response| (response.physical_base, response.virtual_base));
    memory::initialize(hhdm, executable_address);

    let Some(memory_map) = MEMMAP_REQUEST.response() else {
        fail("Limine did not provide a memory map");
    };
    let Some((physical, length)) = memory_map
        .entries()
        .iter()
        .filter(|entry| entry.type_ == limine::memmap::MEMMAP_USABLE)
        .filter_map(|entry| {
            let aligned = entry.base.checked_add(PAGE_SIZE - 1)? & !(PAGE_SIZE - 1);
            let skipped = aligned.checked_sub(entry.base)?;
            let available = entry.length.checked_sub(skipped)? & !(PAGE_SIZE - 1);
            (available >= MIN_HEAP_SIZE).then_some((aligned, available.min(MAX_HEAP_SIZE)))
        })
        .max_by_key(|(_, length)| *length)
    else {
        fail("no suitable usable memory region exists for the heap");
    };
    let Some(pointer) = memory::phys_to_virt(physical) else {
        fail("heap address is outside the higher-half direct map");
    };
    let Ok(length) = usize::try_from(length) else {
        fail("heap is too large for this architecture");
    };
    // SAFETY: Limine marks this page-aligned physical range as usable, and the
    // HHDM provides an exclusive contiguous virtual mapping of the same range.
    unsafe {
        ALLOCATOR.lock().init(pointer.as_ptr(), length);
    }
    (hhdm, executable_address)
}

#[cfg(target_os = "none")]
struct BootFiles {
    system: &'static [EmbeddedFile],
    mutable: alloc::vec::Vec<InitialFile>,
}

#[cfg(target_os = "none")]
fn load_boot_files(modules: &'static [&'static limine::file::File]) -> Option<BootFiles> {
    use alloc::{boxed::Box, string::String, vec::Vec};

    let mut system = Vec::new();
    let mut mutable = Vec::new();
    for module in modules {
        let Some(path) = module.cmdline().strip_prefix("afos-file:") else {
            continue;
        };
        if !matches!(
            path,
            value if value == "/sys"
                || value.starts_with("/sys/")
                || value == "/apps"
                || value.starts_with("/apps/")
                || value == "/user"
                || value.starts_with("/user/")
        ) || normalize_path("/", path).ok().as_deref() != Some(path)
        {
            return None;
        }
        let path: &'static str = Box::leak(String::from(path).into_boxed_str());
        if path == "/sys" || path.starts_with("/sys/") {
            system.push(EmbeddedFile {
                path,
                data: module.data(),
            });
        } else {
            mutable.push(InitialFile {
                path: path.strip_prefix('/')?,
                data: module.data(),
            });
        }
    }
    if system.is_empty() {
        return None;
    }
    Some(BootFiles {
        system: Box::leak(system.into_boxed_slice()),
        mutable,
    })
}

#[cfg(target_os = "none")]
fn fail(message: &str) -> ! {
    arch::emergency_write("\nAFOS kernel: ");
    arch::emergency_write(message);
    arch::emergency_write("\n");
    arch::halt()
}

#[cfg(target_os = "none")]
#[panic_handler]
fn panic(info: &core::panic::PanicInfo<'_>) -> ! {
    use core::fmt::Write;
    struct Emergency;
    impl Write for Emergency {
        fn write_str(&mut self, text: &str) -> core::fmt::Result {
            arch::emergency_write(text);
            Ok(())
        }
    }
    let _ = writeln!(Emergency, "\nAFOS kernel panic: {info}");
    arch::halt()
}

#[cfg(target_os = "none")]
#[alloc_error_handler]
fn allocation_error(layout: core::alloc::Layout) -> ! {
    use core::fmt::Write;
    struct Emergency;
    impl Write for Emergency {
        fn write_str(&mut self, text: &str) -> core::fmt::Result {
            arch::emergency_write(text);
            Ok(())
        }
    }
    let _ = writeln!(Emergency, "AFOS kernel: allocation failed: {layout:?}");
    arch::halt()
}

#[cfg(not(target_os = "none"))]
fn main() {
    eprintln!("afos-kernel must be built for a bare-metal *-unknown-none target");
}
