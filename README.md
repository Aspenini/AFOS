# AFOS

AFOS is a portable, terminal-first application environment written in Rust.
The same `no_std + alloc` core runs as a native desktop program and inside a
standalone Limine-loaded kernel on x86_64 and AArch64.

AFOS applications are single-file, interpreted Rhai scripts. Bundled system
apps are loaded as files under `/sys/apps`; installed apps and user data live
on the platform storage backend.

## Supported targets

| Target | Status |
| --- | --- |
| macOS, Linux, Windows terminal | Supported |
| x86_64 bare metal | Limine ELF/ISO, QEMU-tested |
| AArch64 bare metal | Limine ELF/ISO, QEMU-tested |
| `wasm32-unknown-unknown` shared crates | Compile-checked; browser frontend deferred |

Every release uses the same bundle layout:

```text
<target>/
├── boot/afos[.elf|.exe]
└── fs/
    ├── sys/
    ├── apps/
    └── user/
```

Desktop mounts `fs/` directly. Bare-metal ISOs keep the same tree; Limine
loads each file into memory with its AFOS path before starting the kernel.

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

# Build the desktop and both bare-metal bundles
cargo xtask build all

# Run interactively in QEMU
cargo xtask run x86_64
cargo xtask run aarch64

# Automated boot tests
cargo xtask smoke x86_64
cargo xtask smoke aarch64
```

`cargo xtask run` requires QEMU, EDK2 firmware, xorriso, and mtools. Limine is
downloaded at its pinned version and verified on the first package build. See
[docs/BARE_METAL.md](docs/BARE_METAL.md).

With [`just`](https://github.com/casey/just) installed, the common development
commands are shorter:

```sh
just desktop
just build-x86_64
just build-arm64
just x86_64
just arm64
just test-bare-metal
just check
```

## Filesystem

```text
/
├── sys/                  system-image files, immutable at runtime
│   └── apps/             trusted bundled Rhai applications
├── apps/                 installed single-file applications
└── user/
    ├── config/           AFOS configuration and password verifier
    ├── saves/            user-managed files
    └── appdata/<app-id>/ private application storage
```

Packaged desktop builds persist directly in their adjacent `fs/` directory.
`--data-dir` and `AFOS_DATA_DIR` can override that location. Bare metal loads
the initial `fs/` files through Limine and currently keeps changes to `/apps`
and `/user` in RAM.
Bare-metal packages now also preserve a separate `afos-data.img` and the
workspace contains the portable snapshot-storage layer. Hardware-backed
mounting remains experimental, so normal builds do not claim reboot
persistence yet.

## Documentation

- [Documentation website](https://aspenini.github.io/AFOS/)
- [Getting started](docs/GETTING_STARTED.md)
- [Filesystem and persistence](docs/FILESYSTEM.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Writing applications](docs/APPS.md)
- [Security model](docs/SECURITY.md)
- [Bare-metal build and image layout](docs/BARE_METAL.md)
- [Porting AFOS](docs/PORTING.md)

## License

MIT
