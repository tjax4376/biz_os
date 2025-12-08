#!/bin/bash
# BIZ_OS Docker Shell Script
# Opens a shell in running development container
#
# Version: 1.0
# Author: BIZ_OS Development Team
# Timestamp: 2025-01-27

set -e

CONTAINER_NAME="bizos-dev"

# Check if container is running
if ! docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo "Container '${CONTAINER_NAME}' is not running."
    echo "Starting container with docker-compose..."
    cd "$(dirname "$0")/.."
    docker-compose up -d
    sleep 2
fi

echo "Opening shell in ${CONTAINER_NAME}..."
docker exec -it ${CONTAINER_NAME} /bin/bash
