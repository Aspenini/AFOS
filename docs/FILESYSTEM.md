---
title: Filesystem and persistence
description: Understand AFOS paths, ownership, persistence, and platform mapping.
permalink: /filesystem/
---

{% include nav.md %}

# Filesystem and persistence

AFOS exposes one virtual filesystem on every target. Applications never see
native host paths or UEFI volume paths.

## Directory roles

| Virtual path | Owner | Writable | Intended content |
| --- | --- | --- | --- |
| `/sys` | AFOS build | No | Embedded system files |
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

On UEFI, persistent files live beneath `\AFOS` on the FAT volume that loaded
AFOS. `/sys` is embedded in the executable on every platform and never maps to
writable storage.

## Path rules

- AFOS paths use `/` on every platform.
- Absolute and current-directory-relative paths are supported.
- `.` is normalized.
- Attempts to use `..` above the virtual root are rejected.
- NULs and platform-native backslashes are rejected.
- `/sys` cannot be modified, renamed, or removed.
- Desktop storage rejects paths that escape through symbolic links.
