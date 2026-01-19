# AFOS - Aspen Feltner Operating System

A 32-bit operating system kernel written in C and Assembly, featuring a comprehensive set of subsystems including filesystems, network stack, graphics, audio, and programming language interpreters.

## Overview

AFOS is a monolithic kernel operating system designed for educational purposes and experimentation. It runs entirely in kernel space (ring 0) and provides a rich set of features including:

- **32-bit x86 architecture** - Runs on Intel/AMD 32-bit processors
- **Multiboot compliant** - Boots via GRUB bootloader
- **Monolithic kernel design** - All subsystems run in kernel space
- **In-memory and disk-based filesystem** - Hybrid storage system
- **Network stack** - Ethernet, ARP, IP, and ICMP support
- **Graphics support** - VESA/VGA graphics modes
- **Audio system** - AC97 audio with WAV playback
- **Programming languages** - BASIC and Brainfuck interpreters
- **Custom executable format** - Native binary program execution
- **Interactive shell** - Command-line interface

## Architecture

### Kernel Architecture

AFOS uses a monolithic kernel architecture where all kernel services run in privileged mode (ring 0). This simplifies development but means:

- **No process isolation** - All programs share kernel memory space
- **No user mode** - Programs run with full kernel privileges
- **Direct hardware access** - Programs can directly call kernel functions
- **No standard library** - Programs use kernel APIs directly

### Memory Layout

- Kernel starts at 1MB (standard location for multiboot kernels)
- Text, rodata, data, and BSS sections are properly aligned
- Filesystem data embedded at build time
- Executable programs loaded into dedicated memory regions

### Boot Process

1. **GRUB** loads the kernel binary (`afos.bin`) via Multiboot protocol
2. **Boot assembly** (`boot/boot.asm`) sets up Multiboot header
3. **Entry point** (`boot/entry.asm`) initializes stack and calls kernel
4. **Kernel initialization** (`kernel/kernel.c`) sets up all subsystems
5. **Shell** starts interactive command-line interface

## Core System Components

### Global Descriptor Table (GDT)

The GDT (`kernel/gdt.c`, `boot/gdt.asm`) defines memory segments for:
- Null segment
- Kernel code segment
- Kernel data segment
- User code segment (reserved for future use)
- User data segment (reserved for future use)

### Interrupt Descriptor Table (IDT)

The IDT (`kernel/idt.c`) sets up interrupt handlers for:
- Hardware interrupts (IRQs)
- Software interrupts
- Exceptions
- System calls (future expansion)

### Interrupt Service Routines (ISR)

ISRs (`kernel/isr.c`, `boot/isr.asm`) handle:
- CPU exceptions
- Hardware interrupts
- Keyboard interrupts
- Network card interrupts
- Timer interrupts

### Programmable Interrupt Controller (PIC)

PIC initialization (`kernel/idt.c`) remaps IRQ vectors to avoid conflicts with CPU exceptions and enables interrupt handling.

## Hardware Support

### Keyboard (PS/2)

- **Implementation**: `kernel/keyboard.c`
- Polling-based keyboard input
- Character buffer for shell input
- Supports printable ASCII characters, backspace, and Enter

### Timer (PIT - Programmable Interval Timer)

- **Implementation**: `kernel/pit.c`
- 1000 Hz frequency (1 millisecond resolution)
- Sleep functions for delays
- Timer interrupt handler

### VGA Text Mode

- **Implementation**: `kernel/kernel.c`
- 80x25 character terminal
- Color support (16 colors)
- Scrolling and cursor management
- Backspace support

### Graphics System

- **Implementation**: `kernel/graphics.c`, `kernel/vesa.c`
- VESA/VGA graphics mode support
- VGA mode 13h (320x200x8-bit color)
- Graphics primitives:
  - Pixel drawing
  - Lines, rectangles, circles
  - Polygons
  - Text rendering
  - Double buffering support
- Demo graphics program included

### Storage (ATA/IDE)

- **Implementation**: `kernel/ata.c`, `kernel/blockdev.c`
- ATA/IDE hard disk support
- Block device abstraction layer
- Sector-level read/write operations
- Used by FAT32 filesystem

### Network Card (RTL8139)

- **Implementation**: `kernel/rtl8139.c`
- Realtek RTL8139 Ethernet controller support
- Packet send/receive
- MAC address retrieval
- Interrupt-driven packet handling
- Polling mode for packet processing

### Audio (AC97)

- **Implementation**: `kernel/ac97.c`, `kernel/audio.c`
- Intel AC97 audio codec support
- PCM audio playback
- Tone generation
- WAV file playback support
- Alternative SB16 support available (`kernel/sb16.c`)

## Filesystem

AFOS implements a hybrid filesystem system combining in-memory and disk-based storage.

### In-Memory Filesystem

- **Implementation**: `kernel/filesystem.c`
- Hierarchical directory structure
- File and directory operations
- Current directory tracking
- Path resolution (absolute and relative paths)
- Built-in files embedded at compile time

### FAT32 Filesystem

- **Implementation**: `kernel/fat32.c`
- Full FAT32 read/write support
- Directory enumeration
- File read/write operations
- Cluster allocation and deallocation
- Disk persistence for user files

### Filesystem Integration

- **System files** (`/sys/`) - Embedded in kernel binary via `build_sysfs.py`
  - Compiled into kernel at build time
  - Read-only access
  - Suitable for small system programs

- **User files** (`/home/`) - Stored on FAT32 disk
  - Copied to disk image via `build_home.py`
  - Read/write access
  - Persistent across boots
  - Suitable for large files (WAV files, documents, etc.)

### Filesystem Operations

- Directory navigation (`cd`, `ls`, `dir`)
- File creation (`create`)
- File reading and writing
- Save to disk (`save` command)
- Load from disk (automatic on boot)

## Network Stack

AFOS implements a complete TCP/IP network stack (subset):

### Ethernet Layer

- **Implementation**: `kernel/ethernet.c`
- Ethernet frame encapsulation/decapsulation
- MAC address management
- Frame routing to upper protocols
- Support for IPv4, ARP protocols

### ARP (Address Resolution Protocol)

- **Implementation**: `kernel/arp.c`
- IP to MAC address resolution
- ARP request/reply handling
- ARP cache management
- Timeout-based resolution

### IP (Internet Protocol)

- **Implementation**: `kernel/ip.c`
- IPv4 packet handling
- IP header construction
- Checksum calculation
- Protocol routing (ICMP, TCP, UDP)
- TTL (Time To Live) support

### ICMP (Internet Control Message Protocol)

- **Implementation**: `kernel/icmp.c`
- ICMP Echo Request (ping)
- ICMP Echo Reply handling
- Ping command in shell (`ping <ip_address>`)

### Network Configuration

- IP address assignment (currently manual via code)
- MAC address from RTL8139 hardware
- Gateway support (typically 10.0.2.2 in QEMU user networking)

## Programming Languages

### BASIC Interpreter

- **Implementation**: `kernel/basic.c`
- Full BASIC language interpreter
- Variable management
- Control structures (IF, FOR, WHILE, etc.)
- Functions and subroutines
- File I/O operations
- Graphics functions
- Run `.bas` files directly from shell

### Brainfuck Interpreter

- **Implementation**: `kernel/brainfuck.c`
- Complete Brainfuck language implementation
- 30,000-cell tape
- All 8 Brainfuck operators (`,`, `.`, `+`, `-`, `<`, `>`, `[`, `]`)
- Run `.bf` files directly from shell

## Executable System

### AFOS Binary Format

- **Implementation**: `kernel/executable.c`
- Custom executable format (`.afos` extension)
- Simple header structure:
  - Magic number: `0x534F4641` ("AFOS")
  - Version number
  - Code size
  - Entry point offset
- Direct execution (no process isolation)
- Command-line argument support

### Program Execution

Programs can be executed via:
- `run <path>` command - Execute by full path
- Direct execution - Type program name if in `/sys/components/`
- Automatic interpreter selection - `.bas` and `.bf` files automatically use interpreters

## Shell

The AFOS shell (`kernel/shell.c`) provides an interactive command-line interface.

### Built-in Commands

- **`cd <dir>`** - Change current directory
- **`ls [dir]`** - List directory contents
- **`dir [dir]`** - Alias for `ls`
- **`clear`** - Clear the terminal screen
- **`run <executable>`** - Execute a program
- **`graphics-test`** - Run graphics demo
- **`audio-test`** - Test audio output (plays 440Hz tone)
- **`play <file.wav>`** - Play a WAV audio file
- **`save`** - Save in-memory filesystem to disk
- **`create <file>`** - Create a new empty file
- **`ping <ip_address>`** - Send ICMP ping packets
- **`help`** - Display help message

### Features

- Path-based prompt showing current directory
- Command history buffer
- Backspace support for editing
- Automatic program type detection
- System program path (`/sys/components/`)

## Audio System

### AC97 Audio

- **Implementation**: `kernel/ac97.c`, `kernel/audio.c`, `kernel/wav.c`
- Intel AC97 audio codec initialization
- PCM audio playback
- Tone generation (sine waves)
- WAV file parsing and playback
- Sample rate conversion
- Mono/stereo support
- 8-bit and 16-bit sample support

### Audio Features

- Stream-based playback (handles large files)
- Automatic format conversion (to 8-bit mono for AC97)
- Chunked processing for memory efficiency
- Real-time audio output

## Graphics System

### Graphics Modes

- **VGA Mode 13h**: 320x200 pixels, 8-bit color (256 colors)
- **VESA Support**: Framework for higher resolutions (when available)

### Graphics API

- Pixel-level drawing
- Geometric primitives (lines, rectangles, circles, polygons)
- Filled shapes
- Text rendering
- Color palette management
- Double buffering support

### Graphics Demo

The `graphics-test` command demonstrates:
- Color gradients
- Animated graphics
- Shape drawing
- Pattern generation

## Build System

### Makefile Targets

- **`make all`** - Build the kernel binary
- **`make iso`** - Create bootable ISO image
- **`make disk`** - Create/update FAT32 disk image
- **`make run`** - Run kernel directly with QEMU
- **`make run-iso`** - Run ISO with QEMU (includes disk and network)
- **`make go`** - Clean, build, create disk, and run ISO
- **`make clean`** - Remove build artifacts
- **`make clean-disk`** - Remove disk image
- **`make web`** - Prepare for web deployment

### Build Scripts

- **`build_sysfs.py`** - Embeds `files/sys/` directory into kernel binary
  - Converts files to C source code
  - Handles binary and text files
  - Creates filesystem structure
  - Generates `kernel/sysfs_data.c`

- **`build_home.py`** - Copies `files/home/` to FAT32 disk image
  - Uses mtools for FAT32 manipulation
  - Creates/updates disk image
  - Preserves file structure

### Build Requirements

- **Cross-compiler**: `x86_64-elf-gcc` or `i386-elf-gcc` (or system gcc with `-m32`)
- **Assembler**: `nasm`
- **Linker**: Cross-compiler linker or system ld
- **GRUB**: `grub-mkrescue` for ISO creation
- **QEMU**: For running the OS
- **mtools**: For FAT32 disk manipulation
- **dosfstools**: For creating FAT32 filesystem

### Build Process

1. **System files**: `build_sysfs.py` processes `files/sys/` → `kernel/sysfs_data.c`
2. **Compilation**: C and Assembly files compiled to object files
3. **Linking**: Object files linked into `afos.bin` kernel binary
4. **Disk image**: `build_home.py` creates/updates `afos_disk.img` FAT32 image
5. **ISO creation**: Kernel + GRUB config → `afos.iso`

## Quick Start

### Building and Running

```bash
# Build the kernel
make all

# Create ISO and run in QEMU (recommended)
make iso
make run-iso

# Or use the convenience target (clean + build + run)
make go
```

### QEMU Configuration

The `run-iso` target uses:
- CD-ROM boot (ISO image)
- IDE hard disk (FAT32 disk image)
- RTL8139 network card (user networking)
- AC97 audio device
- 128MB RAM

### Adding Files

**System files** (embedded in kernel):
```bash
cp myprogram.bas files/sys/components/
make go
# Access as: /sys/components/myprogram.bas
```

**User files** (on disk):
```bash
cp myfile.wav files/home/
make go
# Access as: /home/myfile.wav
```

## File Structure

```
AFOS/
├── boot/              # Bootloader and assembly initialization
│   ├── boot.asm       # Multiboot header
│   ├── entry.asm      # Kernel entry point
│   ├── gdt.asm        # GDT initialization
│   └── isr.asm        # ISR stubs
├── kernel/            # Kernel source code
│   ├── kernel.c       # Main kernel initialization
│   ├── idt.c          # Interrupt Descriptor Table
│   ├── gdt.c          # Global Descriptor Table
│   ├── isr.c          # Interrupt Service Routines
│   ├── keyboard.c     # Keyboard driver
│   ├── filesystem.c   # In-memory filesystem
│   ├── fat32.c        # FAT32 filesystem
│   ├── shell.c        # Shell implementation
│   ├── executable.c   # Executable loader
│   ├── basic.c        # BASIC interpreter
│   ├── brainfuck.c    # Brainfuck interpreter
│   ├── graphics.c     # Graphics subsystem
│   ├── vesa.c         # VESA/VGA graphics
│   ├── audio.c        # Audio subsystem
│   ├── ac97.c         # AC97 audio driver
│   ├── wav.c          # WAV file parser
│   ├── rtl8139.c      # RTL8139 network driver
│   ├── ethernet.c     # Ethernet layer
│   ├── arp.c          # ARP protocol
│   ├── ip.c           # IP protocol
│   ├── icmp.c         # ICMP protocol
│   ├── ata.c          # ATA disk driver
│   ├── blockdev.c     # Block device abstraction
│   ├── pit.c          # Timer (PIT)
│   └── malloc.c       # Memory allocator
├── include/           # Header files
├── files/             # Source files for filesystem
│   ├── sys/           # System files (embedded)
│   └── home/          # User files (on disk)
├── examples/          # Example programs
├── tools/             # Build utilities
├── linker.ld          # Linker script
├── grub.cfg           # GRUB configuration
└── Makefile           # Build system
```

## Development

### Writing Programs

**BASIC programs**:
```basic
PRINT "Hello, AFOS!"
FOR i = 1 TO 10
    PRINT i
NEXT i
```

**Brainfuck programs**:
```brainfuck
++++++++++[>+++++++>++++++++++>+++>+<<<<-]
>++.>+.+++++++..+++.>++.<<+++++++++++++++.
>.+++.------.--------.>+.>.
```

**C programs** (compile to AFOS binary format):
- Use custom executable format
- Link against kernel functions
- See `tools/afos-pack.c` for packing tool

### Kernel Development

- All kernel code in `kernel/` directory
- Headers in `include/` directory
- No standard library - implement everything yourself
- Use kernel functions directly
- Memory management via `malloc.c`

### Debugging

- Use QEMU's monitor: `qemu-system-i386 -monitor stdio ...`
- Add print statements via `terminal_writestring()`
- Use GDB with QEMU's GDB stub
- Check kernel logs in terminal output

## Limitations and Design Decisions

- **No process isolation** - All programs run in kernel space
- **No user mode** - Everything runs with full privileges
- **No multitasking** - Single-threaded execution
- **Limited memory management** - Basic malloc implementation
- **No standard library** - Kernel provides all functionality
- **In-memory filesystem** - System files embedded at build time
- **Simple executable format** - Custom format for simplicity

## License

This is an educational operating system project.

## Credits

Created by Aspen Feltner as an educational operating system project demonstrating low-level system programming concepts.
