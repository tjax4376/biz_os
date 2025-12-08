# Docker Development Environment

This directory contains Docker-related files for the BIZ_OS development environment.

## Files

- `docker-entrypoint.sh`: Entrypoint script that runs when container starts
  - Sets up directory structure
  - Displays environment information
  - Executes commands passed to container

## Usage

The Docker setup is managed from the project root:

```bash
# Build image
./scripts/docker-build.sh

# Run container
./scripts/docker-run.sh

# Access shell
./scripts/docker-shell.sh
```

See [DOCKER_SETUP.md](../docs/DOCKER_SETUP.md) for complete documentation.
