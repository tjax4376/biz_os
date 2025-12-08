# BIZ_OS Docker Development Environment

## Overview

The Docker-based development environment provides a consistent Ubuntu 22.04 LTS container with all required development tools pre-installed. This eliminates the need for VirtualBox and provides a seamless development experience on macOS.

## Prerequisites

- **Docker Desktop for macOS**: https://www.docker.com/products/docker-desktop
- **System Requirements**:
  - macOS 10.15+ (Catalina or later)
  - 8GB+ RAM (16GB recommended)
  - 20GB+ free disk space
  - Apple Silicon (M1/M2/M3) or Intel Mac

## Quick Start

### 1. Build the Docker Image

```bash
./scripts/docker-build.sh
```

This builds the `bizos-dev:latest` image with:
- Ubuntu 22.04 LTS
- Rust toolchain (stable)
- Python 3 with AI libraries (PyTorch, ONNX Runtime, Transformers)
- Node.js 20.x LTS
- Kernel build tools (gcc, make, libncurses-dev, etc.)
- Cross-compilation toolchains (x86_64, ARM64)

### 2. Start the Development Container

**Using Docker Compose (Recommended):**
```bash
# Start in background
docker-compose up -d

# Access shell
docker-compose exec dev bash

# Or use helper script
./scripts/docker-shell.sh
```

**Using Docker Run:**
```bash
./scripts/docker-run.sh
```

### 3. Verify Installation

Inside the container:
```bash
# Check Rust
rustc --version
cargo --version

# Check Python
python3 --version
python3 -c "import torch; print(torch.__version__)"

# Check Node.js
node --version
npm --version

# Check kernel build tools
gcc --version
make --version
```

## Container Structure

### Volume Mounts

The container mounts several volumes for persistence:

- **Project Root**: `/workspace` → Your project directory
- **Cargo Cache**: `/home/developer/.cargo/registry` → Rust package cache
- **Cargo Git**: `/home/developer/.cargo/git` → Rust git dependencies
- **Python Cache**: `/home/developer/.cache/pip` → Python package cache
- **Node Modules**: `/workspace/node_modules` → Node.js packages
- **Kernel Build**: `/workspace/kernel/linux` → Kernel build artifacts

### Port Mappings

- **8080**: AI Runtime REST API
- **3000**: UI Framework (React dev server)
- **5001**: Python services (Flask, etc.) - mapped from container port 5000 to host port 5001

**Note**: Port 5000 is mapped to 5001 on the host because macOS uses port 5000 for AirPlay Receiver by default. If you need to use port 5000, you can either:
1. Disable AirPlay Receiver: System Settings → General → AirDrop & Handoff → AirPlay Receiver → Off
2. Change the mapping in `docker-compose.yml` to use a different port

### Environment Variables

- `RUST_BACKTRACE=1`: Full Rust backtraces for debugging
- `CARGO_TARGET_DIR=/workspace/target`: Rust build output location
- `PYTHONUNBUFFERED=1`: Unbuffered Python output
- `TERM=xterm-256color`: Terminal color support

## Development Workflow

### Daily Development

1. **Start container:**
   ```bash
   docker-compose up -d
   ```

2. **Access shell:**
   ```bash
   docker-compose exec dev bash
   # or
   ./scripts/docker-shell.sh
   ```

3. **Work on code:**
   ```bash
   # Code is mounted at /workspace
   cd /workspace
   
   # Build Rust components
   cd ai-runtime/aiservd
   cargo build
   cargo test
   
   # Build kernel
   cd /workspace
   ./scripts/build-kernel.sh
   ```

4. **Stop container (optional):**
   ```bash
   docker-compose down
   ```

### Building Components

**AI Runtime:**
```bash
cd /workspace/ai-runtime/aiservd
cargo build --release
cargo test
```

**Kernel:**
```bash
cd /workspace
./scripts/build-kernel.sh
```

**UI Framework:**
```bash
cd /workspace/ui-framework
npm install
npm start  # Runs on port 3000
```

## Common Tasks

### Rebuild Docker Image

After modifying `Dockerfile`:
```bash
docker-compose build --no-cache
# or
./scripts/docker-build.sh
```

### View Container Logs

```bash
docker-compose logs -f dev
```

### Check Container Status

```bash
docker-compose ps
docker stats bizos-dev
```

### Clean Up

**Stop and remove container:**
```bash
docker-compose down
```

**Remove volumes (clears caches):**
```bash
docker-compose down -v
```

**Remove image:**
```bash
docker rmi bizos-dev:latest
```

## Troubleshooting

### Container Won't Start

**Check Docker Desktop:**
- Ensure Docker Desktop is running
- Check system resources (RAM, disk space)

**Check ports:**
```bash
# Check if ports are in use
lsof -i :8080
lsof -i :3000
lsof -i :5001
```

### Port Already in Use (macOS)

**Port 5000 Conflict:**
macOS uses port 5000 for AirPlay Receiver by default. The Docker setup maps container port 5000 to host port 5001 to avoid this conflict.

If you need to use port 5000 on the host:
1. **Disable AirPlay Receiver:**
   - System Settings → General → AirDrop & Handoff → AirPlay Receiver → Off
2. **Update docker-compose.yml:**
   ```yaml
   ports:
     - "5000:5000"  # Change from 5001:5000
   ```

**Other Port Conflicts:**
If other ports (8080, 3000) are in use:
```bash
# Find what's using the port
lsof -i :8080
lsof -i :3000

# Stop the conflicting service or change the port mapping in docker-compose.yml
```

### Permission Issues

The container runs as user `developer` (non-root). If you encounter permission issues:

```bash
# Inside container, check ownership
ls -la /workspace

# Fix ownership if needed (from host)
sudo chown -R $(id -u):$(id -g) .
```

### Build Failures

**Make Version Error (GNU Make >= 4.0 required):**
If you see "GNU Make >= 4.0 is required. Your Make version is 3.81":
- **Ensure you're running inside the Docker container**: The container has Make 4.3
- **Check if running outside container**: macOS may have older Make version
  ```bash
  # Run inside container
  docker-compose exec dev bash
  cd /workspace
  ./scripts/build-kernel.sh
  ```
- **Rebuild container if needed**: `docker-compose build --no-cache`
- **Verify Make version in container**:
  ```bash
  docker-compose exec dev make --version
  # Should show: GNU Make 4.3
  ```

**Kernel build issues:**
- Ensure sufficient disk space (kernel builds require ~10GB)
- Check available memory: `docker stats bizos-dev`
- Always run kernel builds inside the Docker container

**Rust build issues:**
- Clear cargo cache: `docker-compose down -v cargo-cache`
- Rebuild: `cargo clean && cargo build`

### Performance Issues

**Increase resources in docker-compose.yml:**
```yaml
deploy:
  resources:
    limits:
      cpus: '8'  # Increase CPU cores
      memory: 16G  # Increase memory
```

**Use Docker Desktop settings:**
- Docker Desktop → Settings → Resources
- Increase CPU cores and memory allocation

## GPU Support (Future)

For GPU acceleration in Docker (requires NVIDIA GPU):

1. Install NVIDIA Container Toolkit
2. Use `nvidia-docker` runtime
3. Update `docker-compose.yml` with GPU configuration

**Note:** GPU passthrough in Docker on macOS is limited. For full GPU support, consider:
- Native Linux development
- Cloud GPU instances (AWS, GCP)
- Remote Linux development server

## Differences from VM Approach

| Feature | Docker | VM (VirtualBox) |
|---------|--------|-----------------|
| Startup time | ~5 seconds | ~30-60 seconds |
| Resource usage | Lower overhead | Higher overhead |
| GPU passthrough | Limited on macOS | Full support |
| File sharing | Volume mounts | Shared folders |
| Snapshot/rollback | Image layers | VM snapshots |
| Cross-platform | Works everywhere | Platform-specific |

## Best Practices

1. **Use docker-compose** for consistent environment
2. **Commit frequently** - code is persisted via volume mounts
3. **Use volume caches** - speeds up rebuilds significantly
4. **Keep image updated** - rebuild periodically for security updates
5. **Monitor resources** - kernel builds are resource-intensive

## Next Steps

After setting up Docker environment:

1. Clone Linux kernel: `git clone https://github.com/torvalds/linux.git kernel/linux`
2. Download Mistral model: `./scripts/download-mistral.sh`
3. Begin Phase 1 development (see MVP_PLAN.md)

## Additional Resources

- [Docker Documentation](https://docs.docker.com/)
- [Docker Compose Documentation](https://docs.docker.com/compose/)
- [Docker Desktop for Mac](https://docs.docker.com/desktop/mac/)
