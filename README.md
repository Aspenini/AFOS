# AFOS

AFOS is a portable, terminal-first application environment written in Rust.
The same `no_std + alloc` core runs as a native desktop program and as a
UEFI application on x86_64 and AArch64.

AFOS applications are single-file, interpreted Rhai scripts. Bundled system
apps are embedded under `/sys/apps`; installed apps and user data live on
persistent storage.

## Supported targets

| Target | Status |
| --- | --- |
| macOS, Linux, Windows terminal | Supported |
| x86_64 UEFI | Supported and QEMU-tested |
| AArch64 UEFI | Supported and QEMU-tested |
| `wasm32-unknown-unknown` shared crates | Compile-checked; browser frontend deferred |

The UEFI target deliberately remains inside Boot Services. It uses firmware
console, filesystem, clock, allocator, and RNG protocols instead of containing
hardware-specific drivers.

## Quick start

The repository pins nightly Rust because Rhai currently requires nightly for
its `no_std` build.

```sh
# Native desktop environment
cargo run

# Execute one command with temporary storage
cargo run -- --ephemeral --command hello

# Run all checks
cargo xtask check

# Package EFI removable-media directory trees
cargo xtask build all

# Run interactively in QEMU
cargo xtask run x86_64
cargo xtask run aarch64

# Automated boot and persistence tests
cargo xtask smoke x86_64
cargo xtask smoke aarch64
```

`cargo xtask run` requires QEMU, EDK2 firmware, and mtools. Environment
variables for non-Homebrew firmware locations are documented in
[docs/UEFI.md](docs/UEFI.md).

## Filesystem

```text
/
├── sys/                  embedded, immutable system files
│   └── apps/             trusted bundled Rhai applications
├── apps/                 installed single-file applications
└── user/
    ├── config/           AFOS configuration and password verifier
    ├── saves/            user-managed files
    └── appdata/<app-id>/ private application storage
```

On desktop, persistent files use `--data-dir`, `AFOS_DATA_DIR`, or the
platform application-data directory. On UEFI, they use `\AFOS` on the volume
that loaded AFOS.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Writing applications](docs/APPS.md)
- [Security model](docs/SECURITY.md)
- [UEFI build and installation](docs/UEFI.md)
- [Porting AFOS](docs/PORTING.md)

## License

MIT
