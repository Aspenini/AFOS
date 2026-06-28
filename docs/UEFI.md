---
title: UEFI builds
description: Build, package, boot, and test AFOS on x86_64 and AArch64 UEFI.
permalink: /uefi/
---

{% include nav.md %}

# UEFI target

AFOS builds standard PE32+ UEFI applications for:

- `x86_64-unknown-uefi` → `EFI/BOOT/BOOTX64.EFI`
- `aarch64-unknown-uefi` → `EFI/BOOT/BOOTAA64.EFI`

AFOS remains inside Boot Services. It uses Simple Text Input/Output, Simple
File System, Runtime Time, allocation, and RNG protocols. Persistent data is
stored under `\AFOS` on the volume that loaded the executable.

UEFI is a platform backend, not a second AFOS implementation. Both
architectures use the shared shell, VFS, security policy, and Rhai runtime.

## Host requirements

- QEMU with `qemu-system-x86_64` and/or `qemu-system-aarch64`
- Matching EDK2/OVMF code and variable firmware images
- mtools (`mformat`, `mcopy`, and `mmd`)
- Rust targets installed through `rust-toolchain.toml`

## Build and package

```sh
cargo xtask package x86_64
cargo xtask package aarch64
```

Outputs are placed under `dist/uefi-<architecture>/EFI/BOOT`.

Build desktop and both EFI targets together:

```sh
cargo xtask build all
```

To install on removable media, format a FAT32 volume and copy the matching
`EFI` directory to its root. Unsigned local builds require Secure Boot to be
disabled unless the resulting EFI binary is signed with a trusted key.

## QEMU

Install QEMU, EDK2 firmware, and mtools, then run:

```sh
cargo xtask run x86_64
cargo xtask run aarch64
```

Homebrew QEMU firmware is detected under `/opt/homebrew/share/qemu`.
Other installations can set:

- `AFOS_UEFI_X86_CODE`
- `AFOS_UEFI_X86_VARS`
- `AFOS_UEFI_AARCH64_CODE`
- `AFOS_UEFI_AARCH64_VARS`

Set `AFOS_QEMU_SHARE` when all firmware files use QEMU's standard filenames
under a non-Homebrew share directory.

Automated boot and restart-persistence checks are available through:

```sh
cargo xtask smoke x86_64
cargo xtask smoke aarch64
```

The smoke harness writes commands into the FAT image, boots AFOS headlessly,
extracts its result file, then boots the same image again to verify persisted
data.

## Real hardware

1. Format a USB drive or EFI System Partition as FAT32.
2. Copy the generated architecture-specific `EFI` directory to its root.
3. Boot the device's removable-media entry.
4. Disable Secure Boot for unsigned development builds, or sign the EFI
   application with a key trusted by the firmware.

AFOS creates `\AFOS\apps` and `\AFOS\user` on that volume. Back up the
`\AFOS` directory to preserve installed applications and user data.

Firmware protocol availability varies. Setting a master password requires the
UEFI RNG protocol.
