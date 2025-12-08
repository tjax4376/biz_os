#!/bin/bash
# BIZ_OS Docker Build Script
# Builds the development Docker image
#
# Version: 1.0
# Author: BIZ_OS Development Team
# Timestamp: 2025-01-27

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

echo "=========================================="
echo "Building BIZ_OS Development Docker Image"
echo "=========================================="
echo ""

# Build the Docker image
docker build -t bizos-dev:latest -f Dockerfile .

echo ""
echo "✓ Docker image built successfully!"
echo ""
echo "Next steps:"
echo "  1. Run: ./scripts/docker-run.sh"
echo "  2. Or use: docker-compose up -d"
echo ""
