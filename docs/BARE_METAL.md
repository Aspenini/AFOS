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

Each build produces three replaceable files:

```text
dist/
├── afos-<architecture>.elf   standalone kernel executable
├── afos-<architecture>.iso   Limine bootable image
└── system.tar                /sys files and bundled Rhai applications
```

Inside the ISO, Limine reads `/boot/afos.elf` and `/boot/system.tar`. It loads
the ELF segments and system module into memory before transferring control to
the kernel. This keeps updateable policy and applications out of the kernel:

- replace the ELF to update kernel and shared Rust logic;
- replace `system.tar` to update bundled scripts and other `/sys` files;
- replace Limine files to update the bootloader.

The packaging command regenerates the ISO after any replacement. No standalone
EFI directory or FAT disk image is emitted. UEFI boot still requires Limine's
architecture-specific firmware loader inside the ISO; that is an internal
bootloader detail rather than the AFOS kernel format.

## Host requirements

- nightly Rust and the targets in `rust-toolchain.toml`
- QEMU for interactive and smoke tests
- xorriso
- curl, tar, make, and a C compiler to provision the pinned Limine release
- mtools for the internal AArch64 UEFI El Torito boot image
- EDK2 firmware for AArch64 QEMU

The first package build downloads Limine 12.3.3, verifies its SHA-256 digest,
and builds the portable Limine host tool under `target/`.

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

The x86_64 ISO supports both legacy BIOS and UEFI Limine boot. The AArch64 ISO
uses Limine under UEFI. QEMU uses a framebuffer and serial console; the shell
accepts serial input on both architectures and PS/2 keyboard input on x86_64.

For non-Homebrew AArch64 firmware installations, set:

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

The smoke harness boots each ISO and waits until the shared AFOS shell prompt
appears on its serial console.

## Filesystem behavior

`/sys` is parsed from `system.tar` and remains read-only. `/apps` and `/user`
currently use an in-memory filesystem on bare metal. Changes survive for the
current boot but are not persistent because AFOS does not yet have a block
device and writable filesystem driver.

Desktop persistence is unchanged. Adding persistent bare-metal storage is a
platform-backend task and does not require changing the shell, VFS policy,
authorization, or application runtime.

## Updating an image

Edit files under `assets/sys/` to update the system image, or rebuild the
kernel after Rust changes, then rerun the architecture's package command.
Limine resolves the kernel and module by their filesystem paths, so neither is
compiled into the bootloader.
