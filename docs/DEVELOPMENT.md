# BIZ_OS Development Guide

## Development Environment Setup

### Option 1: Docker (Recommended for macOS)

Docker provides a consistent Ubuntu-based development environment without needing VirtualBox.

**Prerequisites:**
- Docker Desktop for macOS installed: https://www.docker.com/products/docker-desktop
- 8GB+ RAM available for Docker
- 20GB+ free disk space

**Quick Start:**

1. **Build the Docker image:**
   ```bash
   ./scripts/docker-build.sh
   ```

2. **Start the development container:**
   ```bash
   # Option A: Using docker-compose (recommended)
   docker-compose up -d
   docker-compose exec dev bash
   
   # Option B: Using run script
   ./scripts/docker-run.sh
   
   # Option C: Open shell in running container
   ./scripts/docker-shell.sh
   ```

3. **Inside the container, build components:**
   ```bash
   # Build AI runtime
   cd ai-runtime/aiservd
   cargo build
   
   # Build kernel
   cd /workspace
   ./scripts/build-kernel.sh
   ```

**Docker Features:**
- ✅ Ubuntu 22.04 LTS base
- ✅ All development tools pre-installed (Rust, Python, Node.js, kernel build tools)
- ✅ Volume mounts for code persistence
- ✅ Port forwarding (8080, 3000, 5000)
- ✅ Cached builds for faster rebuilds

**Docker Commands:**
```bash
# Start container in background
docker-compose up -d

# Stop container
docker-compose down

# View logs
docker-compose logs -f

# Rebuild image after Dockerfile changes
docker-compose build --no-cache
```

### Option 2: Native macOS Development

1. **Set up development environment:**
   ```bash
   ./scripts/setup-dev-env.sh
   ```

2. **Install dependencies manually:**
   - Rust: https://rustup.rs/
   - Python 3: `brew install python3`
   - Node.js: `brew install node`
   - Cross-compilation toolchains: `brew install gcc`

3. **Build AI runtime:**
   ```bash
   cd ai-runtime/aiservd
   cargo build
   ```

4. **Build kernel:**
   ```bash
   ./scripts/build-kernel.sh
   ```

**Note:** Kernel builds may require Linux-specific tools. Docker is recommended for kernel development.

## Project Structure

See the [Implementation Plan](../.cursor/plans/biz_os_implementation_plan_684054af.plan.md) for detailed structure.

## Development Workflow

1. **Create feature branch:**
   ```bash
   git checkout -b feature/phase1-foundation
   ```

2. **Make changes and test:**
   ```bash
   # For Rust code
   cd ai-runtime/aiservd
   cargo test
   cargo clippy
   ```

3. **Commit changes:**
   ```bash
   git add .
   git commit -m "Description of changes"
   ```

## Building Components

### Kernel
- Configuration: `kernel/configs/baseline.config`
- Build script: `scripts/build-kernel.sh`
- Custom syscalls: `kernel/syscalls/ai_syscalls.c`

### AI Runtime
- Main daemon: `ai-runtime/aiservd/`
- Build: `cd ai-runtime/aiservd && cargo build`
- Configuration: `ai-runtime/aiservd/config.toml`

### Buildroot
- Configuration: `buildroot/configs/bizos_defconfig`
- Requires Buildroot installation

## Testing

- **Rust tests:** `cd ai-runtime/aiservd && cargo test`
- **Kernel tests:** Run kernel in VM and test syscalls
- **Integration tests:** See `tests/` directory (to be implemented)

## Debugging

- **Rust:** Use `cargo test -- --nocapture` for output
- **Kernel:** Use `dmesg` to view kernel logs
- **AI Runtime:** Check logs at `/var/log/bizos/aiservd.log`

**Docker Debugging:**
```bash
# Access container shell
docker-compose exec dev bash

# View container logs
docker-compose logs -f dev

# Check container resources
docker stats bizos-dev
```

## Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) for code standards and guidelines.

