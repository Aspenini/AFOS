---
title: Porting AFOS
description: Implement and validate another AFOS platform backend.
permalink: /porting/
---

{% include nav.md %}

# Porting AFOS

The shared crates require `core` and `alloc`. A new target provides one
`afos_api::Platform` implementation.

Required services:

1. Text output, line input, secret input masking, and clear-screen support.
2. A storage implementation rooted specifically for AFOS.
3. Monotonic milliseconds.
4. Cryptographically secure random bytes for password salts.
5. Platform and architecture identification.
6. Optional nonblocking cancellation polling.

These services correspond to `afos_api::Platform`. Keep the implementation in
a platform crate rather than adding target conditionals to `afos-core`.

Storage methods receive normalized relative paths beneath the AFOS persistent
root. They must not permit traversal, symlink escape, or access to unrelated
host storage.

The target frontend constructs `Afos`, registers `RhaiRuntime`, calls
`initialize`, and starts either the interactive shell or one-shot command
execution. Do not duplicate VFS, permission, application-discovery, or shell
logic in a platform crate.

## Backend invariants

- Console functions preserve UTF-8 as far as the platform permits.
- Secret input does not echo the entered password.
- Storage is rooted beneath one AFOS-owned location; nonpersistent adapters
  document that behavior explicitly.
- Storage paths are normalized and relative before reaching the backend.
- Writes cannot escape through links, aliases, devices, or mount points.
- Entropy failures are reported instead of replaced with predictable bytes.
- The monotonic clock does not move backward during one session.
- Unsupported operations return typed errors rather than panicking.

Before declaring a port supported:

- run the shared unit and runtime tests;
- compile the shared crates for the target;
- run bundled Rhai commands;
- validate protected operations;
- test persistence across restart when the backend claims persistence.

Add the target to `xtask` after those checks can be automated. A supported
port needs a reproducible package layout and a restart-persistence smoke test.

The reserved web boundary is documented in the
[`platforms/web` source directory](https://github.com/Aspenini/AFOS/tree/main/platforms/web).
