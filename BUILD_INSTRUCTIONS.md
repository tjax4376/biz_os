# BIZ_OS Build Instructions

## Overview

This guide provides step-by-step instructions for downloading BIZ_OS from git and compiling the operating system using the Bizos-dev Docker container. The Docker container provides a consistent development environment with all required tools pre-installed.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Downloading from Git](#downloading-from-git)
3. [Docker Setup](#docker-setup)
4. [Building the Docker Container](#building-the-docker-container)
5. [Entering the Development Container](#entering-the-development-container)
6. [Compiling the OS](#compiling-the-os)
   - [Kernel Compilation](#kernel-compilation)
   - [AI Runtime Compilation](#ai-runtime-compilation)
   - [Building All Components](#building-all-components)
7. [Testing](#testing)
8. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Required Software

- **Git**: Version control system
  ```bash
  # macOS (using Homebrew)
  brew install git
  
  # Ubuntu/Debian
  sudo apt-get install git
  
  # Verify installation
  git --version
  ```

- **Docker**: Container platform (version 20.10 or later)
  ```bash
  # macOS: Download Docker Desktop from https://www.docker.com/products/docker-desktop
  # Ubuntu/Debian
  sudo apt-get update
  sudo apt-get install docker.io docker-compose
  
  # Verify installation
  docker --version
  docker-compose --version
  ```

- **Docker Compose**: Container orchestration (version 1.29 or later)
  - Usually included with Docker Desktop
  - For Linux, install separately: `sudo apt-get install docker-compose`

### System Requirements

- **macOS**: 10.15+ (Intel or Apple Silicon)
- **Linux**: Ubuntu 20.04+ or Debian 11+
- **RAM**: 8GB minimum (16GB recommended)
- **Disk Space**: 50GB+ free space
- **CPU**: Multi-core processor recommended

---

## Downloading from Git

### Step 1: Clone the Repository

```bash
# Clone the BIZ_OS repository
git clone https://github.com/your-username/biz_os.git
# Or if using SSH:
# git clone git@github.com:your-username/biz_os.git

# Navigate to the project directory
cd biz_os
```

### Step 2: Verify Repository Structure

```bash
# Check that essential files exist
ls -la

# Expected files:
# - Dockerfile
# - docker-compose.yml
# - Makefile
# - README.md
# - ARCHITECTURE.md
# - scripts/
# - kernel/
# - ai-runtime/
```

### Step 3: Check Out Desired Branch (Optional)

```bash
# List available branches
git branch -a

# Check out a specific branch (e.g., main, develop)
git checkout main

# Or checkout a specific tag/version
git checkout v1.0.0
```

---

## Docker Setup

### Step 1: Start Docker

**macOS:**
- Open Docker Desktop application
- Wait for Docker to start (whale icon in menu bar should be steady)

**Linux:**
```bash
# Start Docker service
sudo systemctl start docker
sudo systemctl enable docker  # Enable on boot

# Verify Docker is running
sudo docker ps
```

### Step 2: Verify Docker Access

```bash
# Test Docker installation
docker run hello-world

# If you get a permission error on Linux, add your user to docker group:
sudo usermod -aG docker $USER
# Log out and log back in for changes to take effect
```

---

## Building the Docker Container

The Bizos-dev Docker container contains all development tools including:
- Ubuntu 22.04 base
- Kernel build tools (gcc, make, libncurses-dev, etc.)
- Rust toolchain (stable)
- Python 3 with AI libraries (PyTorch, ONNX Runtime, transformers)
- Node.js 20.x LTS
- Cross-compilation toolchains (x86_64, ARM64)

### Option 1: Using Docker Compose (Recommended)

```bash
# From the project root directory
cd biz_os

# Build the Docker image
docker-compose build

# This will:
# - Build the bizos-dev:latest image
# - Install all dependencies
# - Set up the development environment
# - May take 10-30 minutes depending on your internet connection
```

### Option 2: Using Docker Build Script

```bash
# From the project root directory
cd biz_os

# Make the script executable (if not already)
chmod +x scripts/docker-build.sh

# Build the Docker image
./scripts/docker-build.sh
```

### Option 3: Manual Docker Build

```bash
# From the project root directory
cd biz_os

# Build the image
docker build -t bizos-dev:latest -f Dockerfile .

# Tag with version (optional)
docker tag bizos-dev:latest bizos-dev:1.0.0
```

### Build Progress

The build process will:
1. Download Ubuntu 22.04 base image
2. Install system dependencies (~5-10 minutes)
3. Install Rust toolchain (~5-10 minutes)
4. Install Node.js (~2-5 minutes)
5. Install Python AI libraries (~10-20 minutes)
6. Set up development user and environment

**Total build time**: 20-45 minutes (depending on internet speed)

---

## Entering the Development Container

### Option 1: Using Docker Compose (Recommended)

```bash
# Start container in detached mode
docker-compose up -d

# Access shell in running container
docker-compose exec dev bash

# Or start and access in one command
docker-compose run --rm dev bash
```

### Option 2: Using Docker Run Script

```bash
# Make the script executable
chmod +x scripts/docker-run.sh

# Run the container (interactive)
./scripts/docker-run.sh
```

### Option 3: Manual Docker Run

```bash
# Run container interactively
docker run -it --rm \
    --name bizos-dev \
    -v "$(pwd):/workspace" \
    -v cargo-cache:/home/developer/.cargo/registry \
    -v cargo-git:/home/developer/.cargo/git \
    -v python-cache:/home/developer/.cache/pip \
    -v node-modules:/workspace/node_modules \
    -v kernel-build:/workspace/kernel/linux \
    -p 8080:8080 \
    -p 3000:3000 \
    -p 5001:5000 \
    -e RUST_BACKTRACE=1 \
    -e CARGO_TARGET_DIR=/workspace/target \
    -e PYTHONUNBUFFERED=1 \
    -e TERM=xterm-256color \
    bizos-dev:latest \
    /bin/bash
```

### Verify Container Environment

Once inside the container, verify the environment:

```bash
# Check working directory
pwd
# Should output: /workspace

# Verify tools are installed
rustc --version
python3 --version
node --version
make --version
gcc --version

# Check project files are mounted
ls -la /workspace
```

---

## Compiling the OS

### Kernel Compilation

#### Step 1: Clone Linux Kernel Source (First Time Only)

If the kernel source doesn't exist, clone it:

```bash
# Inside the Docker container
cd /workspace

# Option 1: Use the build script with --clone flag
./scripts/build-kernel.sh --clone

# Option 2: Manual clone
mkdir -p kernel
cd kernel
git clone https://github.com/torvalds/linux.git linux
cd linux
git checkout v6.1  # or latest stable version
cd /workspace
```

#### Step 2: Configure Kernel

```bash
# Inside the Docker container
cd /workspace

# Run kernel build script
./scripts/build-kernel.sh

# Or manually:
cd kernel/linux

# Use default configuration
make defconfig

# Or customize configuration (interactive)
make menuconfig

# For x86_64 architecture
make ARCH=x86_64 defconfig
make ARCH=x86_64 menuconfig
```

#### Step 3: Build Kernel

```bash
# Inside the Docker container
cd /workspace/kernel/linux

# Build kernel (uses all available CPU cores)
make -j$(nproc) ARCH=x86_64

# Or specify architecture explicitly
ARCH=x86_64 make -j$(nproc)

# For ARM64
ARCH=arm64 make -j$(nproc)
```

#### Step 4: Locate Kernel Image

After successful build, the kernel image will be located at:

- **x86_64**: `kernel/linux/arch/x86/boot/bzImage`
- **ARM64**: `kernel/linux/arch/arm64/boot/Image`

```bash
# Verify kernel image exists
ls -lh kernel/linux/arch/x86/boot/bzImage

# Check kernel version
file kernel/linux/arch/x86/boot/bzImage
```

**Build time**: 30-90 minutes (depending on CPU cores and configuration)

### AI Runtime Compilation

The AI Runtime consists of kernel modules and user-space daemon.

#### Step 1: Build Kernel Modules

```bash
# Inside the Docker container
cd /workspace

# Navigate to AI runtime directory
cd kernel/ai-runtime

# Build kernel modules
make

# Or use the master Makefile from project root
cd /workspace
make ai-runtime
```

#### Step 2: Build User-Space Daemon (Rust)

```bash
# Inside the Docker container
cd /workspace

# Navigate to AI runtime daemon
cd kernel/ai-runtime/aiservd

# Build in release mode (optimized)
cargo build --release

# Or build in debug mode (faster, includes debug symbols)
cargo build

# Run tests
cargo test
```

#### Step 3: Verify AI Runtime Build

```bash
# Check kernel module objects
ls -lh kernel/ai-runtime/*.o

# Check Rust binary
ls -lh kernel/ai-runtime/aiservd/target/release/aiservd

# Or debug binary
ls -lh kernel/ai-runtime/aiservd/target/debug/aiservd
```

### Building All Components

Use the master Makefile to build all components:

```bash
# Inside the Docker container
cd /workspace

# Show available targets
make help

# Build kernel
make kernel

# Build AI runtime
make ai-runtime

# Clean all build artifacts
make clean

# Run tests
make test
```

---

## Testing

### Test Kernel Modules

```bash
# Inside the Docker container
cd /workspace

# Navigate to test directory
cd kernel/ai-runtime/tests

# Build tests
make

# Run tests (requires kernel module to be loaded)
# Note: Some tests require root privileges and kernel module loading
sudo make test
```

### Test AI Runtime Daemon

```bash
# Inside the Docker container
cd /workspace

# Run the daemon (if built)
cd kernel/ai-runtime/aiservd
./target/release/aiservd --help

# Or run in debug mode
./target/debug/aiservd --help
```

### Test System Calls

```bash
# Inside the Docker container
cd /workspace

# Compile test program (if exists)
gcc -o test_syscall tests/test_ai_syscalls.c

# Run test (may require root)
sudo ./test_syscall
```

---

## Troubleshooting

### Docker Issues

#### Docker Build Fails

**Problem**: Docker build fails with network errors or timeouts

**Solution**:
```bash
# Check Docker daemon is running
docker ps

# Try building with no cache
docker build --no-cache -t bizos-dev:latest -f Dockerfile .

# Check Docker logs
docker logs bizos-dev
```

#### Permission Denied Errors

**Problem**: Permission denied when accessing Docker

**Solution** (Linux):
```bash
# Add user to docker group
sudo usermod -aG docker $USER

# Log out and log back in
# Or use newgrp
newgrp docker
```

#### Container Won't Start

**Problem**: Container exits immediately

**Solution**:
```bash
# Check container logs
docker logs bizos-dev

# Run container interactively to see errors
docker run -it --rm bizos-dev:latest /bin/bash
```

### Build Issues

#### Kernel Build Fails - Make Version

**Problem**: Error about Make version < 4.0

**Solution**:
```bash
# Verify Make version inside container
make --version

# Should show GNU Make 4.3 or higher
# If not, rebuild Docker image
docker-compose build --no-cache
```

#### Kernel Build Fails - Missing Dependencies

**Problem**: Missing header files or libraries

**Solution**:
```bash
# Inside container, install missing dependencies
sudo apt-get update
sudo apt-get install -y libncurses-dev flex bison libssl-dev libelf-dev bc dwarves

# Rebuild kernel
cd /workspace/kernel/linux
make clean
make -j$(nproc) ARCH=x86_64
```

#### Rust Build Fails

**Problem**: Cargo build errors

**Solution**:
```bash
# Update Rust toolchain
rustup update stable

# Clean and rebuild
cd /workspace/kernel/ai-runtime/aiservd
cargo clean
cargo build --release
```

#### Python Package Installation Fails

**Problem**: pip install errors

**Solution**:
```bash
# Inside container
pip3 install --upgrade pip setuptools wheel

# Install packages individually to identify problem
pip3 install torch --no-cache-dir
pip3 install onnxruntime --no-cache-dir
```

### Volume Mount Issues

#### Files Not Visible in Container

**Problem**: Project files not visible inside container

**Solution**:
```bash
# Check volume mounts
docker inspect bizos-dev | grep Mounts

# Verify mount path
docker run -it --rm -v "$(pwd):/workspace" bizos-dev:latest ls -la /workspace
```

#### Permission Issues with Mounted Files

**Problem**: Permission denied accessing mounted files

**Solution**:
```bash
# Check file permissions on host
ls -la

# Fix permissions if needed
chmod -R 755 .

# Or run container with host user ID
docker run -it --rm \
    -v "$(pwd):/workspace" \
    -u $(id -u):$(id -g) \
    bizos-dev:latest \
    /bin/bash
```

### Network Issues

#### Can't Access Services on Exposed Ports

**Problem**: Services not accessible on host ports 8080, 3000, 5001

**Solution**:
```bash
# Check port mappings
docker port bizos-dev

# Verify ports are not in use
lsof -i :8080
lsof -i :3000
lsof -i :5001

# Use different ports if needed
docker run -it --rm \
    -p 8081:8080 \
    -p 3001:3000 \
    -p 5002:5000 \
    ...
```

### Getting Help

If you encounter issues not covered here:

1. **Check Logs**:
   ```bash
   docker logs bizos-dev
   ```

2. **Review Documentation**:
   - `README.md` - Project overview
   - `ARCHITECTURE.md` - System architecture
   - `kernel/ai-runtime/README.md` - AI runtime documentation
   - `kernel/ai-runtime/USER_GUIDE.md` - User guide
   - `kernel/ai-runtime/DEVELOPER_GUIDE.md` - Developer guide

3. **Check Journal**:
   - `JOURNAL.md` - Development notes and decisions

4. **Review Memory Cards**:
   - `.memory/cards.md` - Common issues and solutions

---

## Quick Reference

### Essential Commands

```bash
# Build Docker image
docker-compose build

# Start container
docker-compose up -d

# Access container shell
docker-compose exec dev bash

# Build kernel
cd /workspace && ./scripts/build-kernel.sh

# Build AI runtime
cd /workspace && make ai-runtime

# Clean all builds
cd /workspace && make clean

# Stop container
docker-compose down
```

### File Locations

- **Kernel Image**: `kernel/linux/arch/x86/boot/bzImage`
- **AI Runtime Binary**: `kernel/ai-runtime/aiservd/target/release/aiservd`
- **Kernel Modules**: `kernel/ai-runtime/*.ko`
- **Build Artifacts**: `target/` (Rust), `kernel/linux/` (kernel)

### Environment Variables

- `ARCH`: Target architecture (x86_64, arm64)
- `RUST_BACKTRACE`: Enable Rust backtraces (set to 1)
- `CARGO_TARGET_DIR`: Rust build directory (`/workspace/target`)
- `PYTHONUNBUFFERED`: Unbuffered Python output (set to 1)

---

## Next Steps

After successfully building BIZ_OS:

1. **Review Architecture**: Read `ARCHITECTURE.md` to understand system design
2. **Run Tests**: Execute test suites to verify functionality
3. **Development**: Start developing new features using the development environment
4. **Documentation**: Review `DEVELOPER_GUIDE.md` for coding guidelines

---

**Last Updated**: 2025-01-27  
**Version**: 1.0  
**Maintainer**: BIZ_OS Development Team
