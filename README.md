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

Bare-metal builds produce a replaceable kernel ELF and `system.tar`. Limine
loads both files from the ISO into memory, so bundled Rhai applications and
other `/sys` data can be updated without embedding them into the kernel.
Desktop release packages place the same files under
`dist/desktop/share/afos/sys`.

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

# Build desktop, kernel ELFs, system.tar, and Limine ISOs
cargo xtask build all

# Run interactively in QEMU
cargo xtask run x86_64
cargo xtask run aarch64

# Automated boot tests
cargo xtask smoke x86_64
cargo xtask smoke aarch64
```

`cargo xtask run` requires QEMU and xorriso. AArch64 packaging additionally
uses mtools and EDK2 firmware. Limine is downloaded at its pinned version and
verified on the first package build. See
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

On desktop, persistent files use `--data-dir`, `AFOS_DATA_DIR`, or the
platform application-data directory. Bare metal currently uses a RAM-backed
overlay for `/apps` and `/user`; `/sys` is loaded from `system.tar`.

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
