# AFOS - Aspen Feltner Operating System

A 32-bit operating system kernel written in C and Assembly, featuring a BASIC interpreter and executable system.

- **Ring 0 execution** - All programs run in kernel space
- **In-memory filesystem** - Files embedded at build time, no disk persistence
- **No process isolation** - Programs share kernel memory space
- **No standard library** - Programs use kernel functions directly

## Quick Start

### Building and Running Locally

```bash
# Build the kernel
make all

# Create ISO and run in QEMU
make iso
make run-iso
```

### Running in Browser (Web/WASM)

Build for web deployment:
```bash
make web
```

Then start a web server in the `web/` directory:
```bash
cd web
python -m http.server 8000  # Windows: double-click server.bat
```

Open http://localhost:8000 in your browser!

See `web/README.md` for more details about the web version.
