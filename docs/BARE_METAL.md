---
title: Bare-metal builds
description: Build, inspect, boot, and update AFOS Limine kernel images.
permalink: /bare-metal/
---

{% include nav.md %}

# Bare-metal target

AFOS bare metal is a standalone Rust kernel loaded with the Limine boot
protocol. It does not run inside UEFI Boot Services. The same `afos-core` and
official Rhai runtime used by desktop execute inside the kernel.

Two architectures are packaged:

- `x86_64-unknown-none`
- `aarch64-unknown-none-softfloat`

## Disk and memory layout

All targets use the same relative layout:

```text
dist/
├── x86_64/
│   ├── boot/afos.elf
│   ├── fs/{sys,apps,user}/
│   ├── afos.iso
│   └── afos-data.img
├── aarch64/
│   └── ...
└── desktop/
    ├── boot/afos[.exe]
    └── fs/{sys,apps,user}/
```

Inside an ISO, Limine reads `/boot/afos.elf` and each individual file under
`/fs`. It loads the ELF segments and tags every file module with its AFOS path
before transferring control to the kernel. This keeps updateable policy and
applications out of the kernel:

- replace the ELF to update kernel and shared Rust logic;
- replace individual files under `fs/` to update applications or defaults;
- replace Limine files to update the bootloader.

The packaging command regenerates the ISO after any replacement. No standalone
EFI directory or FAT disk image is emitted. UEFI boot still requires Limine's
architecture-specific firmware loader inside the ISO; that is an internal
bootloader detail rather than the AFOS kernel format.

`afos-data.img` is a separate sparse writable-data image. Repackaging preserves
an existing image. The default kernel does not mount it yet; it is the stable
artifact boundary for the in-progress block-storage backend.

## Host requirements

- nightly Rust and the targets in `rust-toolchain.toml`
- QEMU for interactive and smoke tests
- xorriso
- curl and tar to provision the pinned Limine release
- mtools for the internal UEFI El Torito boot images
- EDK2 firmware for QEMU

The first package build downloads Limine 12.3.3, verifies its SHA-256 digest,
and extracts its architecture-specific UEFI loaders under `target/`.

## Build

```sh
just build-x86_64
just build-arm64

# Desktop and both kernels:
just build
```

The equivalent commands are:

```sh
cargo xtask package x86_64
cargo xtask package aarch64
```

## Run in QEMU

```sh
just x86_64
just arm64
```

Both ISOs use Limine under UEFI. QEMU uses a framebuffer and serial console;
the shell accepts serial input on both architectures and PS/2 keyboard input
on x86_64.

For non-Homebrew firmware installations, set:

- `AFOS_X86_64_CODE`
- `AFOS_X86_64_VARS`
- `AFOS_AARCH64_CODE`
- `AFOS_AARCH64_VARS`

Set `AFOS_QEMU_SHARE` when the standard firmware filenames live in another
QEMU data directory.

Automated boot checks:

```sh
just test-x86_64
just test-arm64
just test-bare-metal
```

The smoke harness boots each ISO through UEFI, sends `hello` over the serial
console, and verifies the bundled Rhai application output.

## Filesystem behavior

Files tagged `/sys/...` become the read-only system tree. Initial `/apps` and
`/user` files are copied into an in-memory writable filesystem. Changes
survive for the current boot but are not persistent in the default build.

The shared `afos-storage` crate now provides a 512-byte block-device trait and
an atomic two-slot snapshot store with generation selection and checksums.
Experimental VirtIO block and RNG drivers compile for both kernel targets
behind the `experimental-virtio` feature. They remain disabled because x86_64
legacy queue completion and AArch64 device-MMIO page mappings still need to be
finished and covered by reboot tests.

The kernel heap is no longer a fixed static array. It selects an aligned usable
region from Limine's memory map and currently caps the selected heap at 128
MiB.

Desktop mounts its adjacent `fs/` tree directly, so the same paths persist
there. Adding persistent bare-metal storage is a platform-backend task and
does not require changing shell, VFS, authorization, or runtime logic.

## Updating an image

Edit files under the repository's `fs/` directory or rebuild the kernel after
Rust changes, then rerun the architecture's package command. `xtask`
regenerates Limine's module list from the files present. The kernel and
filesystem content remain separate update units.
