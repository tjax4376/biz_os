#!/bin/bash
# BIZ_OS Docker Run Script
# Starts the development container
#
# Version: 1.0
# Author: BIZ_OS Development Team
# Timestamp: 2025-01-27

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# Check if image exists
if ! docker image inspect bizos-dev:latest >/dev/null 2>&1; then
    echo "Docker image not found. Building..."
    ./scripts/docker-build.sh
fi

echo "=========================================="
echo "Starting BIZ_OS Development Container"
echo "=========================================="
echo ""

# Run the container interactively
docker run -it --rm \
    --name bizos-dev \
    -v "$PROJECT_ROOT:/workspace" \
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
