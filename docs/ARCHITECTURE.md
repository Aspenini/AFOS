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

## Virtual filesystem

`/sys` is a compile-time table of embedded bytes and is never delegated to a
platform storage backend. `/apps` and `/user` are normalized before the
platform sees them. Paths containing NULs, backslashes, or attempts to escape
the root are rejected.

The desktop adapter canonicalizes existing paths, canonicalizes writable
parents, and rejects writable symbolic-link targets. UEFI FAT filesystems do
not support symbolic links.

