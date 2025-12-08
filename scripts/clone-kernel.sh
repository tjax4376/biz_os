#!/bin/bash
# BIZ_OS Kernel Clone Script
# Clones the Linux kernel repository for BIZ_OS development
#
# Version: 1.0
# Author: BIZ_OS Development Team
# Timestamp: 2025-01-27

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
KERNEL_DIR="$PROJECT_ROOT/kernel/linux"

echo "BIZ_OS Kernel Clone"
echo "=================="
echo ""

# Check if kernel directory already exists
if [ -d "$KERNEL_DIR" ] && [ "$(ls -A $KERNEL_DIR 2>/dev/null)" ]; then
    echo "Kernel directory already exists at: $KERNEL_DIR"
    echo ""
    if [ -f "$KERNEL_DIR/Makefile" ]; then
        echo "Kernel source appears to be already cloned."
        cd "$KERNEL_DIR"
        echo "Current branch: $(git branch --show-current 2>/dev/null || echo 'unknown')"
        echo "Latest commit: $(git log -1 --oneline 2>/dev/null || echo 'unknown')"
        echo ""
        read -p "Re-clone kernel? This will remove the existing directory. (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "Aborted."
            exit 0
        fi
        echo "Removing existing kernel directory..."
        rm -rf "$KERNEL_DIR"
    else
        echo "Directory exists but doesn't contain kernel source."
        read -p "Remove and clone fresh? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "Aborted."
            exit 1
        fi
        rm -rf "$KERNEL_DIR"
    fi
fi

# Create parent directory if needed
mkdir -p "$(dirname $KERNEL_DIR)"

# Clone kernel
echo "Cloning Linux kernel from https://github.com/torvalds/linux.git"
echo "This may take several minutes depending on your connection..."
echo ""

cd "$(dirname $KERNEL_DIR)"
git clone https://github.com/torvalds/linux.git linux

cd "$KERNEL_DIR"

# Checkout stable version
echo ""
echo "Checking out stable version v6.1..."
git checkout v6.1

echo ""
echo "✓ Kernel cloned successfully!"
echo ""
echo "Kernel location: $KERNEL_DIR"
echo "Kernel version: $(make kernelversion 2>/dev/null || echo 'unknown')"
echo ""
echo "Next steps:"
echo "  1. Run: ./scripts/build-kernel.sh"
echo "  2. Or configure kernel: cd kernel/linux && make menuconfig"
echo ""
