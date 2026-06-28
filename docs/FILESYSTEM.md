---
title: Filesystem and persistence
description: Understand AFOS paths, ownership, persistence, and platform mapping.
permalink: /filesystem/
---

{% include nav.md %}

# Filesystem and persistence

AFOS exposes one virtual filesystem on every target. Applications never see
native host paths or boot-media paths.

## Directory roles

| Virtual path | Owner | Writable | Intended content |
| --- | --- | --- | --- |
| `/sys` | AFOS build | No | Files from the system image |
| `/sys/apps` | AFOS build | No | Trusted bundled scripts |
| `/apps` | User/admin | Yes | Installed single-file apps |
| `/user/config` | AFOS and user | Yes | System configuration |
| `/user/saves` | User | Yes | Documents, projects, game saves, exports |
| `/user/appdata/<app-id>` | One app | Yes | Preferences, cache, indexes, internal state |

## Appdata versus saves

Use appdata when the file is an implementation detail of one application:

- preferences;
- cached or indexed data;
- session recovery;
- an internal database;
- state the user normally does not manage directly.

Use saves when the file is user-owned and should be visible or portable:

- documents;
- project files;
- game save slots;
- imported or exported data;
- content another application may open.

An app can access its own appdata with `appdata_read` and `appdata_write`
without requesting filesystem access. Access to `/user/saves` uses `fs_read`
or `fs_write`, must be declared in the application header, and is authorized
according to the [security policy]({{ '/security/' | relative_url }}).

## Platform mapping

On desktop, `/apps` and `/user` are stored beneath:

1. `--data-dir`, when passed;
2. `AFOS_DATA_DIR`, when set;
3. the native platform application-data directory.

On bare metal, `/sys` comes from the `system.tar` module that Limine loads from
the ISO. `/apps` and `/user` currently use a RAM-backed overlay and therefore
reset on reboot. Persistent bare-metal storage requires a future block-device
and writable-filesystem adapter.

Desktop reads `/sys` from the directory selected by `--system-dir`,
`AFOS_SYSTEM_DIR`, an installed `share/afos/sys` directory, or the repository's
`assets/sys` directory during development. `cargo xtask build desktop`
packages the binary and this external tree under `dist/desktop`.

## Path rules

- AFOS paths use `/` on every platform.
- Absolute and current-directory-relative paths are supported.
- `.` is normalized.
- Attempts to use `..` above the virtual root are rejected.
- NULs and platform-native backslashes are rejected.
- `/sys` cannot be modified, renamed, or removed.
- Desktop storage rejects paths that escape through symbolic links.
