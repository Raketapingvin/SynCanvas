#!/bin/bash
# Install dependencies for OS development on Debian/Ubuntu/Zorin
set -e

echo "Updating package lists..."
sudo apt-get update

echo "Installing essential build tools..."
sudo apt-get install -y build-essential

echo "Installing assembler and emulator..."
sudo apt-get install -y nasm qemu-system-x86

echo "Installing bootloader and ISO creation tools..."
sudo apt-get install -y xorriso mtools grub-pc-bin grub-common

echo "Installing debugging and other utilities..."
sudo apt-get install -y gdb bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo

echo "All packages installed successfully! You are ready to build the OS."
