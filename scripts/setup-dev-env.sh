#!/bin/bash
# BIZ_OS Development Environment Setup Script
# Run this script to set up the development environment

set -e

echo "BIZ_OS Development Environment Setup"
echo "====================================="

# Check if running on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "Warning: This script is designed for macOS. Some steps may need adjustment."
fi

# Check for required tools
echo ""
echo "Checking for required tools..."

command -v git >/dev/null 2>&1 || { echo "Error: git is required but not installed. Aborting." >&2; exit 1; }
command -v rustc >/dev/null 2>&1 || { echo "Warning: rustc not found. Install Rust: https://rustup.rs/"; }
command -v python3 >/dev/null 2>&1 || { echo "Warning: python3 not found. Install Python 3."; }
command -v node >/dev/null 2>&1 || { echo "Warning: node not found. Install Node.js: https://nodejs.org/"; }

echo "✓ Basic tools check complete"

# Create necessary directories
echo ""
echo "Creating directory structure..."
mkdir -p kernel/patches kernel/configs kernel/syscalls
mkdir -p ai-runtime/{model-manager,learning-engine,inference-engine,pattern-recognition,aiservd,include,models}
mkdir -p business-intel/{data-integration/{email,database,api},process-mining,automation}
mkdir -p ui-framework/{ui-generation,component-library,layout-manager,rendering,input,adaptation,preferences}
mkdir -p applications/{unified-inbox,task-manager,workflow-builder}/ui
mkdir -p system-services/{init,security}
mkdir -p buildroot/configs
mkdir -p scripts tests docs

echo "✓ Directory structure created"

# Initialize Git if not already initialized
if [ ! -d .git ]; then
    echo ""
    echo "Initializing Git repository..."
    git init
    echo "✓ Git repository initialized"
fi

# Create initial README if it doesn't exist
if [ ! -f README.md ]; then
    echo ""
    echo "Creating initial README..."
    cat > README.md << 'EOF'
# BIZ_OS

AI-Powered Business Operating System

See [README.md](README.md) for project overview and [QUICK_START.md](QUICK_START.md) for development setup.
EOF
    echo "✓ README created"
fi

# Check for Docker
echo ""
echo "Checking for Docker..."
if command -v docker >/dev/null 2>&1; then
    echo "✓ Docker found"
    if command -v docker-compose >/dev/null 2>&1 || docker compose version >/dev/null 2>&1; then
        echo "✓ Docker Compose found"
        echo ""
        echo "Docker-based development environment is available!"
        echo "To use Docker:"
        echo "  1. Build image: ./scripts/docker-build.sh"
        echo "  2. Start container: ./scripts/docker-run.sh"
        echo "  3. Or use: docker-compose up -d"
        echo ""
    else
        echo "Warning: Docker Compose not found. Install Docker Desktop for macOS."
    fi
else
    echo "Warning: Docker not found. Install Docker Desktop for macOS: https://www.docker.com/products/docker-desktop"
    echo ""
    echo "Alternative: Use VM-based development (see QUICK_START.md)"
fi

echo ""
echo "Development environment setup complete!"
echo ""
echo "Next steps:"
echo "1. Use Docker (recommended): ./scripts/docker-build.sh && ./scripts/docker-run.sh"
echo "2. Or set up VM (see QUICK_START.md)"
echo "3. Configure kernel build environment"
echo "4. Begin Phase 1 development"

