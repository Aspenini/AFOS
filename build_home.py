#!/usr/bin/env python3
"""
Build script to copy home/ directory contents into FAT32 disk image.
Uses mtools (mcopy) if available, otherwise provides instructions.
"""

import os
import sys
import subprocess

def check_mtools():
    """Check if mtools (mcopy) is available."""
    try:
        subprocess.run(['mcopy', '-V'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def copy_with_mtools(home_dir, disk_image):
    """Copy files using mtools mcopy."""
    if not os.path.exists(home_dir):
        print(f"Warning: {home_dir} directory does not exist.")
        print(f"Create {home_dir} and add files to copy them to the disk image.")
        return 0
    
    if not os.path.exists(disk_image):
        print(f"Error: Disk image {disk_image} does not exist.")
        print("Run 'make disk' first to create the disk image.")
        return 1
    
    # Create home directory on disk image first
    try:
        subprocess.run(
            ['mmd', '-i', disk_image, 'home'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False  # Don't fail if directory already exists
        )
    except Exception:
        pass  # Ignore errors (directory might already exist)
    
    files_copied = 0
    
    # Walk through home directory
    for root, dirs, files in os.walk(home_dir):
        # Skip hidden files and directories
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        files = [f for f in files if not f.startswith('.')]
        
        for filename in files:
            src_path = os.path.join(root, filename)
            # Get relative path from home_dir
            rel_path = os.path.relpath(src_path, home_dir)
            # Convert to FAT32 path format (use forward slashes)
            fat_path = rel_path.replace('\\', '/')
            
            # Create subdirectories if needed
            if '/' in fat_path:
                subdirs = '/'.join(fat_path.split('/')[:-1])
                if subdirs:
                    try:
                        subprocess.run(
                            ['mmd', '-i', disk_image, f'home/{subdirs}'],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            check=False
                        )
                    except Exception:
                        pass
            
            # Copy file to disk image
            # mcopy syntax: mcopy -i image source ::dest
            # Use ::/home/ for absolute path or ::home/ for relative
            try:
                result = subprocess.run(
                    ['mcopy', '-i', disk_image, src_path, f'::/home/{fat_path}'],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=True
                )
                print(f"  Copied: home/{fat_path}")
                files_copied += 1
            except subprocess.CalledProcessError as e:
                # Try alternative path format
                try:
                    result = subprocess.run(
                        ['mcopy', '-i', disk_image, src_path, f'::home\\{fat_path}'],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        check=True
                    )
                    print(f"  Copied: home/{fat_path}")
                    files_copied += 1
                except subprocess.CalledProcessError as e2:
                    print(f"  Warning: Failed to copy {src_path}")
                    print(f"    Error: {e.stderr.decode().strip()}")
    
    if files_copied > 0:
        print(f"Copied {files_copied} file(s) from {home_dir}/ to disk image")
    else:
        print(f"No files copied from {home_dir}/")
    
    return 0

def main():
    home_dir = "files/home"
    disk_image = "afos_disk.img"
    
    if not os.path.exists(disk_image):
        print(f"Error: Disk image {disk_image} does not exist.")
        print("Run 'make disk' first to create the disk image.")
        return 1
    
    if check_mtools():
        print(f"Copying files from {home_dir}/ to FAT32 disk image...")
        return copy_with_mtools(home_dir, disk_image)
    else:
        print("Error: mtools (mcopy) not found.")
        print("Install mtools to copy files to FAT32 disk image:")
        print("  sudo apt install mtools  # Ubuntu/Debian/WSL")
        print("  brew install mtools      # macOS")
        print("")
        print("Alternatively, you can manually mount the disk image and copy files.")
        return 1

if __name__ == "__main__":
    sys.exit(main())

