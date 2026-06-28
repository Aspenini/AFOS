# Show the available development commands.
[group('Help')]
default:
    @just --list

# Run AFOS as an interactive native desktop application.
[group('Desktop')]
desktop:
    cargo run -p afos-desktop

# Run one desktop command with temporary storage.
[group('Desktop')]
desktop-command command:
    cargo run -p afos-desktop -- --ephemeral --command "{{command}}"

# Build and boot the x86_64 Limine ISO interactively in QEMU.
[group('Bare metal / QEMU')]
x86_64:
    cargo xtask run x86_64

# Build and boot the AArch64 Limine ISO interactively in QEMU.
[group('Bare metal / QEMU')]
arm64:
    cargo xtask run aarch64

# Verify that the x86_64 Limine ISO reaches the AFOS shell.
[group('Validation')]
test-x86_64:
    cargo xtask smoke x86_64

# Verify that the AArch64 Limine ISO reaches the AFOS shell.
[group('Validation')]
test-arm64:
    cargo xtask smoke aarch64

# Run both bare-metal QEMU smoke tests.
[group('Validation')]
test-bare-metal: test-x86_64 test-arm64

# Run formatting, tests, Clippy, kernel checks, WASM checks, and docs validation.
[group('Validation')]
check:
    cargo xtask check

# Build desktop plus both kernel ELF and Limine ISO targets.
[group('Build')]
build:
    cargo xtask build all

# Build the x86_64 kernel ELF, system image, and Limine ISO.
[group('Build')]
build-x86_64:
    cargo xtask package x86_64

# Build the AArch64 kernel ELF, system image, and Limine ISO.
[group('Build')]
build-arm64:
    cargo xtask package aarch64
