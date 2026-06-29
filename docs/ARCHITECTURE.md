---
title: Architecture
description: How AFOS shares system logic across desktop and bare-metal backends.
permalink: /architecture/
---

{% include nav.md %}

# Architecture

AFOS is an application environment, not a monolithic hardware kernel.
Platform-independent behavior is split from host integration:

```text
Rhai application
      │
      ▼
versioned SystemApi + capability checks
      │
      ▼
shell, runtime registry, VFS, authentication
      │
      ▼
Platform trait
      │
      ├── native desktop adapter
      └── Limine bare-metal adapter (x86_64 and AArch64)
```

The same command on desktop and bare metal goes through the same parser,
application resolver, permission policy, path normalization, and Rhai
runtime. A backend does not reimplement those behaviors.

## Shared versus platform-specific

| Shared AFOS core | Platform backend |
| --- | --- |
| Shell parsing and current directory | Terminal input and output |
| App lookup and runtime dispatch | Persistent byte storage |
| `/sys`, `/apps`, and `/user` semantics | Monotonic time |
| Path normalization and mount routing | Secure random bytes |
| Capability declarations and prompts | Platform and architecture identity |
| Password verification and rate limiting | Cancellation input |
| Network capability checks and host policy | TCP/IP stack and network device |
| Rhai limits and System API bindings | Target entry point and packaging |

The backend receives normalized storage paths. It does not decide which paths
an app may access; the shared core makes that decision first.

## Workspace

- `afos-api` contains the public `no_std` contracts and shared types.
- `afos-core` owns paths, VFS routing, application discovery, shell state,
  authentication, and per-operation authorization.
- `afos-runtime-rhai` maps `SystemApi` into a constrained Rhai engine.
- `afos-storage` defines the low-level block-device boundary and portable,
  checksummed snapshot persistence format.
- `afos-desktop` implements the platform contract with standard input/output,
  native files, system entropy, and a monotonic clock.
- `afos-kernel` is a standalone ELF kernel with framebuffer, serial, keyboard,
  clock, memory-map-backed allocation, VirtIO block persistence, hardware
  entropy, and a VirtIO + smoltcp TCP/IP stack (x86_64).
- `xtask` provides reproducible build, packaging, QEMU, and validation tasks.

Only platform crates may depend on host or hardware APIs. Shared crates compile
for native targets, freestanding targets, and `wasm32-unknown-unknown`.

The browser build currently proves that shared crates do not accidentally
depend on desktop or hardware APIs. It is not yet a runnable web frontend.

## Execution model

AFOS is synchronous and single-threaded. An `AppRuntime` claims a file
extension and receives source plus a restricted `SystemApi`. Adding another
language does not change shell dispatch, the VFS, or platform adapters.

Rhai runs with:

- no floating-point support;
- fixed 64-bit integer semantics;
- five million operations per invocation;
- bounded call and expression depth;
- 1 MiB strings and source files;
- bounded arrays and maps;
- polling-based Escape cancellation on bare metal.

Recoverable failures use `afos_api::Error`; platform and application errors do
not intentionally panic.

Execution is cooperative. AFOS does not currently provide processes, threads,
background jobs, or preemptive scheduling. Networking follows the same model:
the bare-metal TCP/IP stack is polled from the calling thread, with Escape
cancellation and per-operation timeouts instead of blocking indefinitely.

## Virtual filesystem

`/sys` is a read-only file table loaded at startup. Desktop reads it from a
host directory. Bare metal receives each file as a separately tagged Limine
module. `/apps` and `/user` are normalized before the platform sees them.
Paths containing NULs, backslashes, or attempts to escape the root are
rejected.

The desktop adapter canonicalizes existing paths, canonicalizes writable
parents, and rejects writable symbolic-link targets. The current bare-metal
adapter stores writable files in memory.

## Boot and update boundary

Limine reads the kernel ELF and individual `fs/` files from the ISO, allocates
memory for them, maps the ELF segments, and transfers control to the ELF entry
point. The kernel does not contain the bootloader or bundled Rhai scripts.

This creates independent update units:

```text
Limine → boot/afos.elf → fs/sys/apps/*.rhai
```

Replacing a unit and repackaging the ISO updates that layer without changing
the layers to its right.

See [Filesystem and persistence]({{ '/filesystem/' | relative_url }}) for
the user-facing paths and [Porting AFOS]({{ '/porting/' | relative_url }})
for the backend contract.
