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
mod platform;

#[cfg(target_os = "none")]
use afos_core::{Afos, ShellConfig, load_system_image};
#[cfg(target_os = "none")]
use afos_runtime_rhai::RhaiRuntime;
#[cfg(target_os = "none")]
use limine::{
    BaseRevision, RequestsEndMarker, RequestsStartMarker,
    request::{
        ExecutableAddressRequest, FramebufferRequest, HhdmRequest, ModulesRequest,
        TscFrequencyRequest,
    },
};
#[cfg(target_os = "none")]
use linked_list_allocator::LockedHeap;
#[cfg(target_os = "none")]
use platform::BareMetalPlatform;

#[cfg(target_os = "none")]
const HEAP_SIZE: usize = 16 * 1024 * 1024;

#[cfg(target_os = "none")]
#[repr(C, align(4096))]
struct Heap([u8; HEAP_SIZE]);

#[cfg(target_os = "none")]
#[global_allocator]
static ALLOCATOR: LockedHeap = LockedHeap::empty();

#[cfg(target_os = "none")]
static mut HEAP: Heap = Heap([0; HEAP_SIZE]);

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
static EXECUTABLE_ADDRESS_REQUEST: ExecutableAddressRequest = ExecutableAddressRequest::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests")]
static TSC_FREQUENCY_REQUEST: TscFrequencyRequest = TscFrequencyRequest::new();

#[cfg(target_os = "none")]
#[used]
#[unsafe(link_section = ".limine_requests_end")]
static REQUESTS_END: RequestsEndMarker = RequestsEndMarker::new();

#[cfg(target_os = "none")]
#[unsafe(no_mangle)]
extern "C" fn _start() -> ! {
    // SAFETY: `_start` runs once on the bootstrap processor before allocation.
    unsafe {
        ALLOCATOR
            .lock()
            .init(core::ptr::addr_of_mut!(HEAP.0).cast(), HEAP_SIZE);
    }
    arch::initialize();

    if !BASE_REVISION.is_supported() {
        fail("unsupported Limine boot protocol revision");
    }

    let Some(modules) = MODULES_REQUEST.response() else {
        fail("Limine did not provide the AFOS system image");
    };
    let Some(system_module) =
        modules.modules().iter().copied().find(|module| {
            module.cmdline() == "afos-system" || module.path().ends_with("/system.tar")
        })
    else {
        fail("system.tar was not loaded");
    };
    let Ok(system_files) = load_system_image(system_module.data()) else {
        fail("system.tar is invalid");
    };

    let framebuffer = FRAMEBUFFER_REQUEST
        .response()
        .and_then(|response| response.framebuffers().first().copied());
    let hhdm = HHDM_REQUEST
        .response()
        .map_or(0, |response| response.offset);
    let executable_address = EXECUTABLE_ADDRESS_REQUEST
        .response()
        .map(|response| (response.physical_base, response.virtual_base));
    let counter_frequency = TSC_FREQUENCY_REQUEST
        .response()
        .map_or(1_000_000, |response| response.frequency.max(1));

    let platform = BareMetalPlatform::new(framebuffer, hhdm, executable_address, counter_frequency);
    let mut afos = Afos::with_system_files(platform, system_files);
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
