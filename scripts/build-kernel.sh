#!/bin/bash
# BIZ_OS Kernel Build Script
# Builds the customized Linux kernel for BIZ_OS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
KERNEL_DIR="$PROJECT_ROOT/kernel/linux"
CONFIG_DIR="$PROJECT_ROOT/kernel/configs"

ARCH="${ARCH:-x86_64}"

# Handle --clone flag
if [ "$1" == "--clone" ]; then
    echo "Cloning Linux kernel..."
    if [ -d "$KERNEL_DIR" ] && [ "$(ls -A $KERNEL_DIR 2>/dev/null)" ]; then
        echo "Warning: Kernel directory already exists and is not empty."
        read -p "Remove existing directory and clone fresh? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf "$KERNEL_DIR"
        else
            echo "Aborted."
            exit 1
        fi
    fi
    
    mkdir -p "$(dirname $KERNEL_DIR)"
    cd "$(dirname $KERNEL_DIR)"
    echo "Cloning Linux kernel (this may take a while)..."
    git clone https://github.com/torvalds/linux.git linux
    cd linux
    echo "Checking out stable version v6.1..."
    git checkout v6.1
    echo "✓ Kernel cloned successfully"
    echo ""
    # Continue with build after cloning
fi

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

# Check if Makefile exists (kernel source is properly cloned)
if [ ! -f "Makefile" ]; then
    echo "Error: Kernel source not found. Makefile is missing in $KERNEL_DIR"
    echo ""
    echo "The kernel directory exists but doesn't contain the Linux kernel source."
    echo "Please clone the Linux kernel:"
    echo ""
    echo "  cd $PROJECT_ROOT"
    echo "  git clone https://github.com/torvalds/linux.git kernel/linux"
    echo "  cd kernel/linux"
    echo "  git checkout v6.1  # or latest stable version"
    echo ""
    echo "Or run this script with --clone flag to clone automatically:"
    echo "  ./scripts/build-kernel.sh --clone"
    exit 1
fi

# Verify Make version before proceeding
echo "Checking Make version..."
MAKE_VERSION=$(make --version 2>/dev/null | head -n1 || echo "unknown")
MAKE_MAJOR=$(echo "$MAKE_VERSION" | grep -oE 'GNU Make [0-9]+' | grep -oE '[0-9]+' || echo "0")
if [ -z "$MAKE_MAJOR" ] || [ "$MAKE_MAJOR" -lt 4 ]; then
    echo "ERROR: GNU Make >= 4.0 is required for kernel builds."
    echo "Current version: $MAKE_VERSION"
    echo "Please ensure you're running this inside the Docker container or have Make 4.0+ installed."
    exit 1
fi
echo "✓ Make version check passed: $MAKE_VERSION"
echo ""

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

