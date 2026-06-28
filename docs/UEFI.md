# UEFI target

AFOS builds standard PE32+ UEFI applications for:

- `x86_64-unknown-uefi` → `EFI/BOOT/BOOTX64.EFI`
- `aarch64-unknown-uefi` → `EFI/BOOT/BOOTAA64.EFI`

AFOS remains inside Boot Services. It uses Simple Text Input/Output, Simple
File System, Runtime Time, allocation, and RNG protocols. Persistent data is
stored under `\AFOS` on the volume that loaded the executable.

## Build and package

```sh
cargo xtask package x86_64
cargo xtask package aarch64
```

Outputs are placed under `dist/uefi-<architecture>/EFI/BOOT`.

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

Automated boot and restart-persistence checks are available through:

```sh
cargo xtask smoke x86_64
cargo xtask smoke aarch64
```

The smoke harness writes commands into the FAT image, boots AFOS headlessly,
extracts its result file, then boots the same image again to verify persisted
data.

