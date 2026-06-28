# Porting AFOS

The shared crates require `core` and `alloc`. A new target provides one
`afos_api::Platform` implementation.

Required services:

1. Text output, line input, secret input masking, and clear-screen support.
2. A persistent filesystem rooted specifically for AFOS.
3. Monotonic milliseconds.
4. Cryptographically secure random bytes for password salts.
5. Platform and architecture identification.
6. Optional nonblocking cancellation polling.

Storage methods receive normalized relative paths beneath the AFOS persistent
root. They must not permit traversal, symlink escape, or access to unrelated
host storage.

The target frontend constructs `Afos`, registers `RhaiRuntime`, calls
`initialize`, and starts either the interactive shell or one-shot command
execution. Do not duplicate VFS, permission, application-discovery, or shell
logic in a platform crate.

Before declaring a port supported:

- run the shared unit and runtime tests;
- compile the shared crates for the target;
- run bundled Rhai commands;
- validate protected operations;
- persist a file, restart the target, and read it again.

The reserved web boundary is documented in
[`platforms/web/README.md`](../platforms/web/README.md).

