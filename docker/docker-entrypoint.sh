#!/bin/bash
# BIZ_OS Docker Entrypoint Script
# Sets up development environment and runs commands
#
# Version: 1.0
# Author: BIZ_OS Development Team
# Timestamp: 2025-01-27

set -e

echo "=========================================="
echo "BIZ_OS Development Environment"
echo "=========================================="
echo "User: $(whoami)"
echo "Working Directory: $(pwd)"
echo "Rust Version: $(rustc --version 2>/dev/null || echo 'Not installed')"
echo "Python Version: $(python3 --version)"
echo "Node Version: $(node --version 2>/dev/null || echo 'Not installed')"
echo "=========================================="
echo ""

# Ensure we're in the workspace directory
cd /workspace

# Create necessary directories if they don't exist
mkdir -p kernel/patches kernel/configs kernel/syscalls
mkdir -p ai-runtime/{model-manager,learning-engine,inference-engine,pattern-recognition,aiservd,include,models}
mkdir -p business-intel/{data-integration/{email,database,api},process-mining,automation}
mkdir -p ui-framework/{ui-generation,component-library,layout-manager,rendering,input,adaptation,preferences}
mkdir -p applications/{unified-inbox,task-manager,workflow-builder}/ui
mkdir -p system-services/{init,security}
mkdir -p buildroot/configs
mkdir -p scripts tests docs

# Execute the command passed to the container
exec "$@"
