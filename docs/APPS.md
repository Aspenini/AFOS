---
title: Writing applications
description: Build portable single-file Rhai applications for AFOS.
permalink: /apps/
---

{% include nav.md %}

# AFOS applications

Applications are UTF-8, single-file interpreted programs. Rhai (`.rhai`) is
the only V1 runtime.

Copy installed applications to `/apps`. Immutable applications shipped with
AFOS live in `/sys/apps`. Commands search `/sys/apps` before `/apps`, so an
installed file cannot silently replace a system command. Use an explicit path
such as `/apps/example.rhai` when names conflict.

## Minimal application

Create `hello.rhai`:

```rhai
print("Hello from AFOS!");
0
```

Copy it into the persistent apps directory. It appears as
`/apps/hello.rhai` and can be run as `hello`. If a bundled app already has
that name, use the explicit path `/apps/hello.rhai`.

## Header directives

Directives are optional leading comments in the same file:

```rhai
// afos:api=1
// afos:id=com.example.notes
// afos:capabilities=fs.read:/user/saves,fs.write:/user/saves,clock

print("Hello from AFOS");
0
```

- `api` defaults to `1`; unsupported versions are rejected.
- `id` defaults to the file stem and may contain letters, numbers, `.`, `-`,
  and `_`.
- `capabilities` is a comma-separated list.

Available capabilities are `fs.read:<path>`, `fs.write:<path>`, `clock`,
`system.info`, and `net:<host>`. A filesystem capability covers the named path
and descendants. A network capability authorizes outbound TCP connections to a
host; `net:*` matches any host and `net:example.com` matches only that host.

Every app automatically receives console I/O, arguments, current directory,
and private storage under `/user/appdata/<app-id>`. Installed apps must declare
all other operations and receive a confirmation/password prompt for every
protected call.

Bundled `/sys/apps` scripts are immutable and trusted. Their declared
capabilities are granted without prompts. Merely placing a script under
`/apps` never makes it trusted.

## Private state example

Appdata is appropriate for state owned by one application:

```rhai
let count = 0;
try {
    count = appdata_read("launches.txt").to_int();
} catch {
    // First launch.
}

count += 1;
appdata_write("launches.txt", count.to_string());
print("Launch " + count);
0
```

Use `/user/saves` when a file belongs to the user. See
[Filesystem and persistence]({{ '/filesystem/' | relative_url }}).

## Rhai System API

| Function | Result |
| --- | --- |
| `args()` | command name and arguments as an array |
| `cwd()` | current virtual directory |
| `print_raw(text)` | output without a newline |
| `read_line(prompt)` | one line of user input |
| `fs_read(path)` | UTF-8 file contents |
| `fs_write(path, text)` | create or replace a UTF-8 file |
| `fs_list(path)` | names, with `/` suffixes for directories |
| `fs_mkdir(path)` | recursively create a directory |
| `fs_remove(path)` | remove a file or directory tree |
| `fs_rename(from, to)` | move a file or directory |
| `appdata_read(path)` | read private application data |
| `appdata_write(path, text)` | write private application data |
| `monotonic_millis()` | monotonic milliseconds |
| `system_info()` | map containing name, version, platform, architecture |
| `net_status()` | map with `link_up`, `mac`, `address`, and `gateway` |
| `net_connect(host, port)` | open a TCP connection, returning a handle |
| `net_send(handle, text)` | send bytes, returning the number written |
| `net_recv(handle, max)` | receive up to `max` bytes as text (`""` at close) |
| `net_close(handle)` | close a connection |

Networking requires a `net:<host>` capability. `host` may be an IPv4 literal or
a DNS name; the platform supplies DHCP, DNS, and the TCP/IP stack. A connection
handle belongs to the application that opened it and cannot be used after it is
closed.

```rhai
// afos:capabilities=net:*
let socket = net_connect("example.com", 80);
net_send(socket, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");
print(net_recv(socket, 512));
net_close(socket);
0
```

The final script value is its integer exit status. A script returning `()` or
no value exits successfully.

Denied capabilities, host I/O failures, parse failures, and resource limits
become Rhai runtime errors and nonzero command results.

## Adding another interpreter

Implement `afos_api::AppRuntime`, choose a unique extension, and register it
with `RuntimeRegistry`. The adapter must expose the same API version and route
every system operation through `SystemApi`; it must not access platform
storage directly.

AFOS uses the official `rhai` crate pinned in `Cargo.lock`. AFOS provides host
functions and limits but does not maintain a custom Rhai interpreter.
