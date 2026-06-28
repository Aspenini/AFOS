# AFOS Web Target

The browser frontend is intentionally deferred. The shared `afos-api`,
`afos-core`, and `afos-runtime-rhai` crates are compile-checked for
`wasm32-unknown-unknown` so that platform assumptions do not leak into them.

A future web adapter must implement the same `Platform` contract using:

- a browser terminal or canvas for `Console`;
- IndexedDB-backed persistent storage;
- `performance.now()` for the monotonic clock;
- `crypto.getRandomValues()` for entropy.

It must not change application paths, capability behavior, or Rhai semantics.

