---
title: Getting started
description: Install the AFOS toolchain and run desktop or Limine bare-metal builds.
permalink: /getting-started/
---

{% include nav.md %}

# Getting started

## Requirements

- Git
- Rustup
- The nightly Rust toolchain selected by `rust-toolchain.toml`

Cloning the repository and running Cargo installs the pinned toolchain
components and target standard libraries automatically.

```sh
git clone https://github.com/Aspenini/AFOS.git
cd AFOS
```

## Run the desktop environment

```sh
cargo run
# or
just desktop
```

AFOS performs first-run setup, creates its persistent directory tree, and
opens an interactive prompt:

```text
AFOS:/user$
```

Useful first commands:

```text
help
ls /
ls /sys/apps
hello
sysinfo
exit
```

Execute a command without opening an interactive shell:

```sh
cargo run -- --command "ls /sys/apps"
just desktop-command "ls /sys/apps"
```

Use disposable storage for examples and tests:

```sh
cargo run -- --ephemeral --command hello
```

Choose an explicit persistent directory:

```sh
cargo run -- --data-dir ./afos-data
```

`AFOS_DATA_DIR` provides the same override through an environment variable.
Without either override, AFOS uses the operating system's application-data
location.

## Validate the workspace

```sh
cargo xtask check
```

This runs formatting checks, unit and integration tests, Clippy, both kernel
cross-compiles, and the shared-crate WASM compile check.

For actual Limine boot tests:

```sh
cargo xtask smoke x86_64
cargo xtask smoke aarch64
# or both:
just test-bare-metal
```

The smoke tests require QEMU, EDK2, xorriso, and mtools. See the
[bare-metal guide]({{ '/bare-metal/' | relative_url }}) for installation and
configuration.

## Where to continue

- [Filesystem and persistence]({{ '/filesystem/' | relative_url }})
- [Write a Rhai application]({{ '/apps/' | relative_url }})
- [Understand the shared architecture]({{ '/architecture/' | relative_url }})

## Documentation site

The documentation is a Jekyll site contained entirely in `docs/`. Configure
GitHub Pages under **Settings → Pages** to deploy from the `main` branch and
the `/docs` folder.
