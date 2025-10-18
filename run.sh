#!/bin/bash
set -euo pipefail

# Calculator mode: build and run CLI calculator when invoked as: ./run.sh calc
if [[ "${1:-}" == "calc" ]]; then
gcc -O2 -Wall -Wextra -o calc calc.c
echo "Calculator built: ./calc"
./calc
exit 0
fi

# Assemble and compile (32-bit, freestanding)
gcc -m32 -c boot.S -o boot.o
gcc -m32 -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-stack-protector

# Link with ld to avoid extra sections before the header
ld -m elf_i386 -T linker.ld -o RunDemo.bin boot.o kernel.o

# Verify Multiboot
if grub-file --is-x86-multiboot RunDemo.bin; then
  echo "Multiboot: OK"
else
  echo "Multiboot: FAIL"
  exit 1
fi

# Build ISO
rm -rf isodir
mkdir -p isodir/boot/grub
cp RunDemo.bin isodir/boot/RunDemo.bin
cp grub.cfg    isodir/boot/grub/grub.cfg
grub-mkrescue -o RunDemo.iso isodir
echo "ISO created: RunDemo.iso"

# Run in QEMU (BIOS)
qemu-system-i386 -cdrom RunDemo.iso -boot d -m 64M -monitor none -serial stdio