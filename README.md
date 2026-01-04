# AFOS - Aspen Feltner Operating System

A 32-bit operating system kernel written in C and Assembly, featuring a BASIC interpreter and executable system.

- **Ring 0 execution** - All programs run in kernel space
- **In-memory filesystem** - Files embedded at build time, no disk persistence
- **No process isolation** - Programs share kernel memory space
- **No standard library** - Programs use kernel functions directly
