---
title: Architecture
description: How AFOS shares system logic across desktop and UEFI backends.
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
      └── UEFI adapter (x86_64 and AArch64)
```

The same command on desktop and UEFI goes through the same parser,
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
| Rhai limits and System API bindings | Target entry point and packaging |

The backend receives normalized storage paths. It does not decide which paths
an app may access; the shared core makes that decision first.

## Workspace

- `afos-api` contains the public `no_std` contracts and shared types.
- `afos-core` owns paths, VFS routing, application discovery, shell state,
  authentication, and per-operation authorization.
- `afos-runtime-rhai` maps `SystemApi` into a constrained Rhai engine.
- `afos-desktop` implements the platform contract with standard input/output,
  native files, system entropy, and a monotonic clock.
- `afos-uefi` implements it with UEFI Boot Services.
- `xtask` provides reproducible build, packaging, QEMU, and validation tasks.

Only platform crates may depend on host or firmware APIs. Shared crates compile
for native targets, UEFI targets, and `wasm32-unknown-unknown`.

The browser build currently proves that shared crates do not accidentally
depend on desktop or UEFI APIs. It is not yet a runnable web frontend.

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
- polling-based Escape cancellation on UEFI.

Recoverable failures use `afos_api::Error`; platform and application errors do
not intentionally panic.

Execution is cooperative. AFOS does not currently provide processes, threads,
background jobs, or preemptive scheduling.

## Virtual filesystem

`/sys` is a compile-time table of embedded bytes and is never delegated to a
platform storage backend. `/apps` and `/user` are normalized before the
platform sees them. Paths containing NULs, backslashes, or attempts to escape
the root are rejected.

The desktop adapter canonicalizes existing paths, canonicalizes writable
parents, and rejects writable symbolic-link targets. UEFI FAT filesystems do
not support symbolic links.

See [Filesystem and persistence]({{ '/filesystem/' | relative_url }}) for
the user-facing paths and [Porting AFOS]({{ '/porting/' | relative_url }})
for the backend contract.
