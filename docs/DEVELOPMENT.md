# BIZ_OS Development Guide

## Quick Start

1. **Set up development environment:**
   ```bash
   ./scripts/setup-dev-env.sh
   ```

2. **Build AI runtime:**
   ```bash
   cd ai-runtime/aiservd
   cargo build
   ```

3. **Build kernel:**
   ```bash
   ./scripts/build-kernel.sh
   ```

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

## Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) for code standards and guidelines.

