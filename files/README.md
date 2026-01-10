# Files Directory

This directory contains the source files for the AFOS operating system.

## Structure

- **`sys/`** - System files embedded in the kernel binary
  - Files here are compiled into the kernel at build time
  - Appear in `/sys/` in the kernel filesystem
  - Good for small system components and programs

- **`home/`** - User files copied to the FAT32 disk image
  - Files here are copied to the disk image during build
  - Appear in `/home/` in the kernel filesystem
  - Perfect for large files (WAV files, documents, etc.)
  - No size limit (up to disk capacity)

## Usage

### Adding System Files (sys/)
```bash
cp myprogram.bas files/sys/components/
make go
# Access in kernel: /sys/components/myprogram.bas
```

### Adding User Files (home/)
```bash
cp myfile.wav files/home/
make go
# Access in kernel: /home/myfile.wav
```

## Build Process

- `build_sysfs.py` - Processes `files/sys/` and embeds files in kernel
- `build_home.py` - Copies `files/home/` to FAT32 disk image
- Both run automatically during `make go` or `make disk`

