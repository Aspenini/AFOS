# Makefile for AFOS Kernel

# Toolchain - auto-detect cross-compiler, can be overridden
ASM = nasm
# Try to find cross-compiler, fall back to system gcc
CROSS_CC := $(shell which x86_64-elf-gcc 2>/dev/null || which i386-elf-gcc 2>/dev/null || echo "")
CROSS_LD := $(shell which x86_64-elf-ld 2>/dev/null || which i386-elf-ld 2>/dev/null || echo "")
ifneq ($(CROSS_CC),)
    CC = $(CROSS_CC)
    ifneq ($(CROSS_LD),)
        LD = $(CROSS_LD)
        # Use linker directly with 32-bit architecture
        LDFLAGS = -m elf_i386 -T linker.ld -nostdlib
    else
        LD = $(CROSS_CC)
        LDFLAGS = -m32 -nostdlib -ffreestanding -T linker.ld
    endif
else
    CC = gcc
    LD = gcc
    LDFLAGS = -m32 -nostdlib -ffreestanding -Wl,-T,linker.ld
endif

# Flags
ASMFLAGS = -f elf32
CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -Wall -Wextra -std=c11 -I./include -ffreestanding

# Directories
BOOT_DIR = boot
KERNEL_DIR = kernel
INCLUDE_DIR = include
BUILD_DIR = build

# Source files
BOOT_SRC = $(BOOT_DIR)/boot.asm $(BOOT_DIR)/entry.asm $(BOOT_DIR)/isr.asm $(BOOT_DIR)/gdt.asm
KERNEL_SRC = $(KERNEL_DIR)/kernel.c $(KERNEL_DIR)/idt.c $(KERNEL_DIR)/isr.c $(KERNEL_DIR)/gdt.c \
             $(KERNEL_DIR)/keyboard.c $(KERNEL_DIR)/filesystem.c $(KERNEL_DIR)/shell.c \
             $(KERNEL_DIR)/executable.c $(KERNEL_DIR)/sysfs_data.c $(KERNEL_DIR)/basic.c \
             $(KERNEL_DIR)/brainfuck.c $(KERNEL_DIR)/graphics.c $(KERNEL_DIR)/vesa.c $(KERNEL_DIR)/malloc.c \
             $(KERNEL_DIR)/ata.c $(KERNEL_DIR)/blockdev.c $(KERNEL_DIR)/fat32.c \
             $(KERNEL_DIR)/rtl8139.c $(KERNEL_DIR)/ethernet.c $(KERNEL_DIR)/arp.c $(KERNEL_DIR)/ip.c $(KERNEL_DIR)/icmp.c

# Object files
BOOT_OBJ = $(BOOT_SRC:.asm=.o)
KERNEL_OBJ = $(KERNEL_SRC:.c=.o)
OBJ = $(BOOT_OBJ) $(KERNEL_OBJ)

# Output
KERNEL = afos.bin
ISO = afos.iso
DISK_IMAGE = afos_disk.img
DISK_SIZE = 100  # MB

.PHONY: all clean run iso disk clean-disk

all: sysfs_data $(KERNEL)

# Generate filesystem data from sys/ directory
sysfs_data: build_sysfs.py
	@echo "Building filesystem from sys/ directory..."
	@python3 build_sysfs.py

# Build kernel binary
$(KERNEL): $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

# Assemble boot files
%.o: %.asm
	$(ASM) $(ASMFLAGS) -o $@ $<

# Compile kernel C files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Create FAT32 disk image (only if it doesn't exist)
disk: $(DISK_IMAGE)

$(DISK_IMAGE):
	@if [ ! -f $(DISK_IMAGE) ]; then \
		echo "Creating $(DISK_SIZE) MB FAT32 disk image..."; \
		if command -v dd >/dev/null 2>&1 && command -v mkfs.vfat >/dev/null 2>&1; then \
			dd if=/dev/zero of=$(DISK_IMAGE) bs=1M count=$(DISK_SIZE) 2>/dev/null || \
			dd if=/dev/zero of=$(DISK_IMAGE) bs=1024 count=$$((1024 * $(DISK_SIZE))) 2>/dev/null; \
			mkfs.vfat -F 32 $(DISK_IMAGE) >/dev/null 2>&1; \
			echo "Disk image created: $(DISK_IMAGE)"; \
		else \
			echo "Warning: dd or mkfs.vfat not found. Skipping disk creation."; \
			echo "Install dosfstools to create disk image:"; \
			echo "  sudo apt install dosfstools  # Ubuntu/Debian/WSL"; \
			echo "  brew install dosfstools      # macOS"; \
			touch $(DISK_IMAGE); \
		fi \
	else \
		echo "Disk image already exists: $(DISK_IMAGE)"; \
	fi

# Create ISO for testing
iso: $(KERNEL)
	@echo "Creating bootable ISO..."
	@rm -rf iso
	@mkdir -p iso/boot/grub
	@cp $(KERNEL) iso/boot/
	@cp grub.cfg iso/boot/grub/grub.cfg
	@if command -v i686-elf-grub-mkrescue >/dev/null 2>&1; then \
		echo "Using i686-elf-grub-mkrescue (better for i386)..."; \
		i686-elf-grub-mkrescue -o $(ISO) iso 2>&1; \
	elif command -v x86_64-elf-grub-mkrescue >/dev/null 2>&1; then \
		echo "Using x86_64-elf-grub-mkrescue..."; \
		x86_64-elf-grub-mkrescue -o $(ISO) iso 2>&1; \
	elif command -v i686-elf-grub-mkrescue >/dev/null 2>&1; then \
		echo "Using i686-elf-grub-mkrescue..."; \
		i686-elf-grub-mkrescue -o $(ISO) iso 2>&1; \
	elif command -v grub-mkrescue >/dev/null 2>&1; then \
		echo "Using grub-mkrescue..."; \
		grub-mkrescue -o $(ISO) iso 2>&1; \
	else \
		echo "Error: grub-mkrescue not found."; \
		echo "Install GRUB:"; \
		echo "  brew install grub"; \
		echo ""; \
		echo "For cross-compiler GRUB:"; \
		echo "  brew install x86_64-elf-grub"; \
		exit 1; \
	fi
	@if [ -f $(ISO) ]; then \
		echo ""; \
		echo "ISO created successfully: $(ISO)"; \
		ls -lh $(ISO); \
		file $(ISO); \
		echo ""; \
		echo "To test: make run-iso"; \
	else \
		echo "Error: ISO creation failed"; \
		exit 1; \
	fi

# Run with QEMU
run: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL)

# Run ISO with QEMU
run-iso: iso $(DISK_IMAGE)
	@echo "Booting ISO in QEMU with disk attached..."
	@echo "Note: If boot fails, try: qemu-system-i386 -cdrom $(ISO) -boot d -m 128"
	qemu-system-i386 -cdrom $(ISO) -drive format=raw,file=$(DISK_IMAGE),if=ide,index=0 -netdev user,id=net0 -device rtl8139,netdev=net0 -boot d -m 128

# Clean build artifacts
clean-disk:
	rm -f $(DISK_IMAGE)

clean: clean-disk
	rm -f $(OBJ) $(KERNEL) $(ISO)
	rm -f $(KERNEL_DIR)/sysfs_data.c
	rm -rf iso

