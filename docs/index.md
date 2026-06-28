---
title: AFOS documentation
description: Build, use, extend, and port the AFOS portable Rust environment.
permalink: /
---

{% include nav.md %}

# AFOS

AFOS is a terminal-first application environment whose system logic is shared
across native desktop and UEFI targets. The shell, virtual filesystem,
permissions, application discovery, authentication, and Rhai execution all
live in a portable `no_std + alloc` Rust core. Platforms provide only the
services that cannot be universal: console I/O, persistent storage, time,
entropy, and platform information.

## Choose a path

### Run AFOS

Start the native terminal build, execute one command, or boot an EFI image.

[Get started →]({{ '/getting-started/' | relative_url }})

### Write an application

Create a portable, single-file `.rhai` program and use the capability-checked
AFOS System API.

[Application guide →]({{ '/apps/' | relative_url }})

### Understand the design

See exactly which behavior is shared and what a new platform backend must
implement.

[Architecture →]({{ '/architecture/' | relative_url }})

## Target status

| Target | Status | What changes |
| --- | --- | --- |
| macOS, Linux, Windows | Supported | Native terminal and host-backed storage |
| x86_64 UEFI | Supported and QEMU-tested | UEFI console and filesystem protocols |
| AArch64 UEFI | Supported and QEMU-tested | Same UEFI backend, different Rust target |
| Browser WASM | Shared crates compile | Browser backend and UI are deferred |

Desktop and UEFI run the same AFOS core and official Rhai interpreter. No
custom Rhai implementation is maintained in this repository.

## System at a glance

```text
single-file Rhai app
        │
        ▼
AFOS System API + capability policy
        │
        ▼
shell · VFS · authentication · runtime registry
        │
        ▼
portable Platform contract
        │
        ├── desktop backend
        └── UEFI backend (x86_64 / AArch64)
```

## Files at a glance

```text
/
├── sys/                  immutable files embedded into AFOS
│   └── apps/             trusted bundled applications
├── apps/                 installed applications
└── user/
    ├── config/           AFOS configuration
    ├── saves/            user-owned documents and saves
    └── appdata/<app-id>/ private application state
```

Appdata belongs to an application and is available to that app without a
permission prompt. Saves belong to the user and require a declared filesystem
capability.

## Next steps

- [Install prerequisites and run AFOS]({{ '/getting-started/' | relative_url }})
- [Learn the virtual filesystem]({{ '/filesystem/' | relative_url }})
- [Build and boot the UEFI targets]({{ '/uefi/' | relative_url }})
- [Review the security boundaries]({{ '/security/' | relative_url }})
- [Implement another platform backend]({{ '/porting/' | relative_url }})

