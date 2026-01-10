# Home Directory Setup

The `files/home/` directory is automatically copied into the FAT32 disk image during build.

## Usage

1. **Put files in `files/home/` directory:**
   ```bash
   cp yourfile.wav files/home/
   cp document.txt files/home/
   ```

2. **Build and run:**
   ```bash
   make go
   ```

3. **Access files in kernel:**
   Files will appear in `/home/` directory in the kernel:
   ```bash
   ls /home
   play /home/yourfile.wav
   ```

## Requirements

The build process uses `mtools` (specifically `mcopy`) to copy files to the FAT32 disk image.

**Install mtools:**
- Ubuntu/Debian/WSL: `sudo apt install mtools`
- macOS: `brew install mtools`

If mtools is not installed, the build will continue but files won't be copied to the disk image.

## File Size Limits

- **No limit for disk files** - Files in `home/` can be any size (up to disk capacity)
- Files are stored on the FAT32 disk, not embedded in the kernel binary
- This is perfect for large files like WAV files (10MB+)

## Example

```bash
# Copy a large WAV file
cp ~/Music/song.wav files/home/

# Build and run
make go

# In the kernel shell:
play /home/song.wav
```

