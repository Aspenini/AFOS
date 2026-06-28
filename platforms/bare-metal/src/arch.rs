#[cfg(target_arch = "aarch64")]
use core::sync::atomic::{AtomicU64, Ordering};

pub struct ArchIo {
    #[cfg(target_arch = "x86_64")]
    shift: bool,
    #[cfg(target_arch = "x86_64")]
    caps_lock: bool,
}

impl ArchIo {
    pub fn new(hhdm: u64, executable_address: Option<(u64, u64)>) -> Self {
        #[cfg(target_arch = "aarch64")]
        initialize_pl011(hhdm, executable_address);
        #[cfg(not(target_arch = "aarch64"))]
        let _ = (hhdm, executable_address);
        Self {
            #[cfg(target_arch = "x86_64")]
            shift: false,
            #[cfg(target_arch = "x86_64")]
            caps_lock: false,
        }
    }

    pub fn write_byte(byte: u8) {
        write_byte(byte);
    }

    #[cfg_attr(target_arch = "aarch64", allow(clippy::unused_self))]
    pub fn poll_byte(&mut self) -> Option<u8> {
        if let Some(byte) = serial_read() {
            return Some(byte);
        }
        #[cfg(target_arch = "x86_64")]
        return self.keyboard_read();
        #[cfg(not(target_arch = "x86_64"))]
        None
    }

    #[cfg(target_arch = "x86_64")]
    fn keyboard_read(&mut self) -> Option<u8> {
        // SAFETY: The PS/2 controller command and data ports are architectural.
        if unsafe { inb(0x64) } & 1 == 0 {
            return None;
        }
        // SAFETY: Status indicated one byte is ready on the controller data port.
        let scan = unsafe { inb(0x60) };
        match scan {
            0x2a | 0x36 => {
                self.shift = true;
                None
            }
            0xaa | 0xb6 => {
                self.shift = false;
                None
            }
            0x3a => {
                self.caps_lock = !self.caps_lock;
                None
            }
            value if value & 0x80 != 0 => None,
            value => map_scan_code(value, self.shift, self.caps_lock),
        }
    }
}

pub fn initialize() {
    #[cfg(target_arch = "x86_64")]
    initialize_com1();
}

pub fn emergency_write(text: &str) {
    for byte in text.bytes() {
        write_byte(byte);
    }
}

pub fn counter() -> u64 {
    #[cfg(target_arch = "x86_64")]
    {
        let low: u32;
        let high: u32;
        // SAFETY: RDTSC is available in x86_64 long mode.
        unsafe {
            core::arch::asm!(
                "rdtsc",
                out("eax") low,
                out("edx") high,
                options(nomem, nostack, preserves_flags)
            );
        }
        (u64::from(high) << 32) | u64::from(low)
    }
    #[cfg(target_arch = "aarch64")]
    {
        let value: u64;
        // SAFETY: Limine enters the kernel at EL1 where the generic timer is available.
        unsafe {
            core::arch::asm!(
                "mrs {value}, cntpct_el0",
                value = out(reg) value,
                options(nomem, nostack, preserves_flags)
            );
        }
        value
    }
}

pub fn halt() -> ! {
    loop {
        #[cfg(target_arch = "x86_64")]
        // SAFETY: Halting until the next interrupt is valid after kernel shutdown.
        unsafe {
            core::arch::asm!("hlt", options(nomem, nostack));
        }
        #[cfg(target_arch = "aarch64")]
        // SAFETY: Waiting for an event is valid after kernel shutdown.
        unsafe {
            core::arch::asm!("wfe", options(nomem, nostack));
        }
    }
}

#[cfg(target_arch = "x86_64")]
fn initialize_com1() {
    // SAFETY: AFOS owns COM1 while it is running.
    unsafe {
        outb(0x3f8 + 1, 0x00);
        outb(0x3f8 + 3, 0x80);
        outb(0x3f8, 0x01);
        outb(0x3f8 + 1, 0x00);
        outb(0x3f8 + 3, 0x03);
        outb(0x3f8 + 2, 0xc7);
        outb(0x3f8 + 4, 0x0b);
    }
}

#[cfg(target_arch = "x86_64")]
fn write_byte(byte: u8) {
    // SAFETY: AFOS initialized and exclusively owns COM1.
    unsafe {
        while inb(0x3f8 + 5) & 0x20 == 0 {
            core::hint::spin_loop();
        }
        outb(0x3f8, byte);
    }
}

#[cfg(target_arch = "x86_64")]
fn serial_read() -> Option<u8> {
    // SAFETY: AFOS initialized and exclusively owns COM1.
    unsafe {
        if inb(0x3f8 + 5) & 1 == 0 {
            None
        } else {
            Some(inb(0x3f8))
        }
    }
}

#[cfg(target_arch = "x86_64")]
unsafe fn outb(port: u16, value: u8) {
    // SAFETY: The caller guarantees exclusive ownership of the port.
    unsafe {
        core::arch::asm!(
            "out dx, al",
            in("dx") port,
            in("al") value,
            options(nomem, nostack, preserves_flags)
        );
    }
}

#[cfg(target_arch = "x86_64")]
unsafe fn inb(port: u16) -> u8 {
    let value: u8;
    // SAFETY: The caller guarantees the port can be read.
    unsafe {
        core::arch::asm!(
            "in al, dx",
            in("dx") port,
            out("al") value,
            options(nomem, nostack, preserves_flags)
        );
    }
    value
}

#[cfg(target_arch = "x86_64")]
fn map_scan_code(scan: u8, shift: bool, caps: bool) -> Option<u8> {
    let letter = match scan {
        0x1e => Some(b'a'),
        0x30 => Some(b'b'),
        0x2e => Some(b'c'),
        0x20 => Some(b'd'),
        0x12 => Some(b'e'),
        0x21 => Some(b'f'),
        0x22 => Some(b'g'),
        0x23 => Some(b'h'),
        0x17 => Some(b'i'),
        0x24 => Some(b'j'),
        0x25 => Some(b'k'),
        0x26 => Some(b'l'),
        0x32 => Some(b'm'),
        0x31 => Some(b'n'),
        0x18 => Some(b'o'),
        0x19 => Some(b'p'),
        0x10 => Some(b'q'),
        0x13 => Some(b'r'),
        0x1f => Some(b's'),
        0x14 => Some(b't'),
        0x16 => Some(b'u'),
        0x2f => Some(b'v'),
        0x11 => Some(b'w'),
        0x2d => Some(b'x'),
        0x15 => Some(b'y'),
        0x2c => Some(b'z'),
        _ => None,
    };
    if let Some(mut byte) = letter {
        if shift ^ caps {
            byte = byte.to_ascii_uppercase();
        }
        return Some(byte);
    }
    Some(match (scan, shift) {
        (0x02, false) => b'1',
        (0x03, false) => b'2',
        (0x04, false) => b'3',
        (0x05, false) => b'4',
        (0x06, false) => b'5',
        (0x07, false) => b'6',
        (0x08, false) => b'7',
        (0x09, false) => b'8',
        (0x0a, false) => b'9',
        (0x0b, false) => b'0',
        (0x02, true) => b'!',
        (0x03, true) => b'@',
        (0x04, true) => b'#',
        (0x05, true) => b'$',
        (0x06, true) => b'%',
        (0x07, true) => b'^',
        (0x08, true) => b'&',
        (0x09, true) => b'*',
        (0x0a, true) => b'(',
        (0x0b, true) => b')',
        (0x0c, false) => b'-',
        (0x0c, true) => b'_',
        (0x0d, false) => b'=',
        (0x0d, true) => b'+',
        (0x1a, false) => b'[',
        (0x1a, true) => b'{',
        (0x1b, false) => b']',
        (0x1b, true) => b'}',
        (0x27, false) => b';',
        (0x27, true) => b':',
        (0x28, false) => b'\'',
        (0x28, true) => b'"',
        (0x29, false) => b'`',
        (0x29, true) => b'~',
        (0x2b, false) => b'\\',
        (0x2b, true) => b'|',
        (0x33, false) => b',',
        (0x33, true) => b'<',
        (0x34, false) => b'.',
        (0x34, true) => b'>',
        (0x35, false) => b'/',
        (0x35, true) => b'?',
        (0x39, _) => b' ',
        (0x1c, _) => b'\n',
        (0x0e, _) => 8,
        (0x0f, _) => b'\t',
        _ => return None,
    })
}

#[cfg(target_arch = "aarch64")]
static UART_BASE: AtomicU64 = AtomicU64::new(0);

#[cfg(target_arch = "aarch64")]
#[repr(C, align(4096))]
struct PageTables([[u64; 512]; 3]);

#[cfg(target_arch = "aarch64")]
static mut DEVICE_PAGE_TABLES: PageTables = PageTables([[0; 512]; 3]);

#[cfg(target_arch = "aarch64")]
const UART_PHYSICAL: u64 = 0x0900_0000;
#[cfg(target_arch = "aarch64")]
const UART_VIRTUAL: u64 = 0xffff_fffd_0900_0000;

#[cfg(target_arch = "aarch64")]
fn initialize_pl011(hhdm: u64, executable_address: Option<(u64, u64)>) {
    let base = executable_address
        .and_then(|(physical, virtual_address)| {
            map_device_page(hhdm, physical, virtual_address).then_some(UART_VIRTUAL)
        })
        .unwrap_or_else(|| hhdm.saturating_add(UART_PHYSICAL));
    UART_BASE.store(base, Ordering::Release);
    // SAFETY: QEMU's `virt` machine exposes PL011 at this mapped address.
    unsafe {
        write_register(base, 0x30, 0);
        write_register(base, 0x24, 13);
        write_register(base, 0x28, 1);
        write_register(base, 0x2c, 0x70);
        write_register(base, 0x38, 0x7ff);
        write_register(base, 0x30, 0x301);
    }
}

#[cfg(target_arch = "aarch64")]
fn map_device_page(hhdm: u64, executable_physical: u64, executable_virtual: u64) -> bool {
    if hhdm == 0 {
        return false;
    }
    let tables_virtual = core::ptr::addr_of_mut!(DEVICE_PAGE_TABLES) as u64;
    let Some(tables_offset) = tables_virtual.checked_sub(executable_virtual) else {
        return false;
    };
    let reserved_tables_physical = executable_physical.saturating_add(tables_offset);

    let root_register: u64;
    let mut mair: u64;
    // SAFETY: Reading active EL1 translation registers is valid at kernel entry.
    unsafe {
        core::arch::asm!(
            "mrs {root}, ttbr1_el1",
            "mrs {mair}, mair_el1",
            root = out(reg) root_register,
            mair = out(reg) mair,
            options(nomem, nostack, preserves_flags)
        );
    }
    let address_mask = 0x0000_ffff_ffff_f000_u64;
    let mut current_table = root_register & address_mask;
    let indices = [
        ((UART_VIRTUAL >> 39) & 0x1ff) as usize,
        ((UART_VIRTUAL >> 30) & 0x1ff) as usize,
        ((UART_VIRTUAL >> 21) & 0x1ff) as usize,
        ((UART_VIRTUAL >> 12) & 0x1ff) as usize,
    ];

    for (level, index) in indices[..3].iter().copied().enumerate() {
        let table = hhdm.saturating_add(current_table) as *mut u64;
        // SAFETY: Limine maps active page tables through the HHDM.
        let entry = unsafe { table.add(index) };
        // SAFETY: `entry` points into the active translation table.
        let descriptor = unsafe { core::ptr::read_volatile(entry) };
        if descriptor & 0b11 == 0b11 {
            current_table = descriptor & address_mask;
            continue;
        }
        let next_physical = reserved_tables_physical + (level as u64 * 4096);
        // SAFETY: Each reserved table is page-aligned, unique, and initially zero.
        unsafe {
            core::ptr::write_bytes((tables_virtual as *mut u8).add(level * 4096), 0, 4096);
            core::ptr::write_volatile(entry, next_physical | 0b11);
        }
        current_table = next_physical;
    }

    let table = hhdm.saturating_add(current_table) as *mut u64;
    // AttrIdx 2 is Device-nGnRnE. The page is EL1 RW, outer-shareable,
    // accessed, and never executable.
    let descriptor =
        UART_PHYSICAL | 0b11 | (2 << 2) | (0b10 << 8) | (1 << 10) | (1 << 53) | (1 << 54);
    // SAFETY: The final table is mapped and the selected virtual page is unused.
    unsafe {
        core::ptr::write_volatile(table.add(indices[3]), descriptor);
        mair &= !(0xff << 16);
        core::arch::asm!(
            "msr mair_el1, {mair}",
            "dsb ish",
            "tlbi vmalle1is",
            "dsb ish",
            "isb",
            mair = in(reg) mair,
            options(nostack, preserves_flags)
        );
    }
    true
}

#[cfg(target_arch = "aarch64")]
fn write_byte(byte: u8) {
    let base = UART_BASE.load(Ordering::Acquire);
    if base == 0 {
        return;
    }
    // SAFETY: The initialized address is an exclusively owned PL011.
    unsafe {
        while read_register(base, 0x18) & (1 << 5) != 0 {
            core::hint::spin_loop();
        }
        write_register(base, 0, u32::from(byte));
    }
}

#[cfg(target_arch = "aarch64")]
fn serial_read() -> Option<u8> {
    let base = UART_BASE.load(Ordering::Acquire);
    if base == 0 {
        return None;
    }
    // SAFETY: The initialized address is an exclusively owned PL011.
    unsafe {
        if read_register(base, 0x18) & (1 << 4) != 0 {
            None
        } else {
            Some(read_register(base, 0).to_le_bytes()[0])
        }
    }
}

#[cfg(target_arch = "aarch64")]
unsafe fn read_register(base: u64, offset: usize) -> u32 {
    let address = usize::try_from(base).expect("AArch64 pointers are 64-bit") + offset;
    // SAFETY: Caller guarantees that base + offset names a PL011 register.
    unsafe { core::ptr::read_volatile(address as *const u32) }
}

#[cfg(target_arch = "aarch64")]
unsafe fn write_register(base: u64, offset: usize, value: u32) {
    let address = usize::try_from(base).expect("AArch64 pointers are 64-bit") + offset;
    // SAFETY: Caller guarantees that base + offset names a PL011 register.
    unsafe { core::ptr::write_volatile(address as *mut u32, value) }
}
