#!/bin/bash
# BIZ_OS Kernel Build Script
# Builds the customized Linux kernel for BIZ_OS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
KERNEL_DIR="$PROJECT_ROOT/kernel/linux"
CONFIG_DIR="$PROJECT_ROOT/kernel/configs"

ARCH="${ARCH:-x86_64}"

echo "BIZ_OS Kernel Build"
echo "=================="
echo "Architecture: $ARCH"
echo ""

# Check if kernel directory exists
if [ ! -d "$KERNEL_DIR" ]; then
    echo "Error: Kernel directory not found at $KERNEL_DIR"
    echo "Please clone the Linux kernel first:"
    echo "  git clone https://github.com/torvalds/linux.git kernel/linux"
    exit 1
fi

cd "$KERNEL_DIR"

# Checkout stable version (adjust as needed)
echo "Checking kernel version..."
KERNEL_VERSION=$(make kernelversion 2>/dev/null || echo "unknown")
echo "Kernel version: $KERNEL_VERSION"
echo ""

# Copy baseline config if .config doesn't exist
if [ ! -f .config ]; then
    echo "No .config found. Using baseline configuration..."
    if [ -f "$CONFIG_DIR/baseline.config" ]; then
        cp "$CONFIG_DIR/baseline.config" .config
        echo "✓ Baseline config copied"
    else
        echo "Warning: Baseline config not found. Running make defconfig..."
        make defconfig
    fi
fi

# Configure kernel (interactive)
echo ""
echo "Configuring kernel..."
echo "You can customize the configuration with: make menuconfig"
read -p "Run menuconfig? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    make menuconfig
fi

# Build kernel
echo ""
echo "Building kernel..."
echo "This may take a while..."
make -j$(nproc) ARCH=$ARCH

echo ""
echo "✓ Kernel build complete!"
echo ""
echo "Kernel image location:"
if [ "$ARCH" = "x86_64" ]; then
    echo "  $KERNEL_DIR/arch/x86/boot/bzImage"
elif [ "$ARCH" = "arm64" ]; then
    echo "  $KERNEL_DIR/arch/arm64/boot/Image"
fi

