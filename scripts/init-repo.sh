#!/bin/bash
# Initialize BIZ_OS repository structure

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

echo "Initializing BIZ_OS repository structure..."

# Create all directories
./scripts/setup-dev-env.sh

# Initialize Rust projects
echo ""
echo "Initializing Rust projects..."

# AI Runtime Daemon
if [ ! -f "ai-runtime/aiservd/Cargo.toml" ]; then
    echo "AI runtime daemon Cargo.toml already exists"
else
    echo "✓ AI runtime daemon structure ready"
fi

# Initialize Git if not already done
if [ ! -d .git ]; then
    echo ""
    echo "Initializing Git repository..."
    git init
    git add .gitignore CONTRIBUTING.md README.md
    echo "✓ Git repository initialized"
fi

echo ""
echo "Repository initialization complete!"
echo ""
echo "Next steps:"
echo "1. Review kernel configuration: kernel/configs/baseline.config"
echo "2. Build kernel: ./scripts/build-kernel.sh"
echo "3. Set up Buildroot: See buildroot/configs/bizos_defconfig"
echo "4. Build AI runtime: cd ai-runtime/aiservd && cargo build"

