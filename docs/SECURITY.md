---
title: Security model
description: AFOS capability checks, passwords, trust boundaries, and limitations.
permalink: /security/
---

{% include nav.md %}

# Security model

AFOS capability checks isolate interpreted applications from system services.
They do not defend against physical modification of the storage device,
malicious firmware, or replacement of the AFOS executable.

## Trust boundaries

- `/sys` and `/sys/apps` are loaded from read-only bundle files and cannot
  be written through the VFS.
- Bundled system applications are trusted and automatically receive the
  capabilities declared in their source.
- Installed `/apps` applications receive only console, arguments, current
  directory, and their own appdata by default.
- Filesystem, clock, system-info, and network operations must each be declared.
  A `net:<host>` capability is matched against the requested host, and `net:*`
  authorizes any host; outbound connections are otherwise refused.
- Every other installed-app operation must be declared, confirmed by the user,
  and authenticated when a master password exists.
- Authorization applies to one operation only. AFOS does not cache or persist
  grants.
- `/user/config/security` is blocked from every application, including trusted
  scripts.

## Installed-app authorization

For each protected operation, AFOS:

1. normalizes the requested path;
2. rejects protected system paths;
3. verifies that the script declared a matching capability;
4. displays the app ID, operation, and resource;
5. asks the user to approve that operation once;
6. verifies the master password when configured;
7. performs only that operation.

AFOS repeats the process for the next protected operation. Approval is not
cached or written to disk.

## Master password

First-run setup offers an optional master password. AFOS stores an Argon2id
verifier with a unique firmware/OS-provided random salt, never the password.
Three failed attempts activate an increasing delay, capped at 32 seconds.

`passwd` changes or disables the password. Changing an existing password
requires the current one.

Desktop uses the host operating system's entropy. x86_64 bare metal uses
RDRAND only after checking CPU support and retrying failed samples. The
experimental VirtIO RNG backend is not enabled by default, so AArch64 password
creation still returns an error rather than silently generating a weak salt.

The password controls AFOS API access. It does not encrypt `/user`, hide
filenames, or encrypt underlying storage.

## Limitations

Desktop users or physical attackers who can edit the underlying data directory
can delete the verifier, installed apps, or user files. Secure Boot and
encrypted storage are separate deployment concerns. Rhai resource limits
reduce accidental or malicious denial of service but do not provide process
or memory isolation equivalent to an operating-system process boundary.

See [Filesystem and persistence]({{ '/filesystem/' | relative_url }}) for
path ownership and the difference between saves and appdata.
