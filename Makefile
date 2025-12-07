# BIZ_OS Master Makefile
# Orchestrates builds across all components

.PHONY: all clean kernel ai-runtime help

# Default target
all: help

help:
	@echo "BIZ_OS Build System"
	@echo "==================="
	@echo ""
	@echo "Available targets:"
	@echo "  make kernel       - Build customized Linux kernel"
	@echo "  make ai-runtime   - Build AI runtime daemon"
	@echo "  make buildroot    - Build minimal Linux distribution"
	@echo "  make clean        - Clean all build artifacts"
	@echo "  make setup        - Set up development environment"
	@echo ""

setup:
	@echo "Setting up development environment..."
	@./scripts/setup-dev-env.sh

kernel:
	@echo "Building kernel..."
	@./scripts/build-kernel.sh

ai-runtime:
	@echo "Building AI runtime daemon..."
	@cd ai-runtime/aiservd && cargo build --release

buildroot:
	@echo "Building minimal Linux distribution..."
	@echo "Note: Requires Buildroot to be installed"
	@echo "See buildroot/configs/bizos_defconfig"

clean:
	@echo "Cleaning build artifacts..."
	@cd kernel/linux && make clean || true
	@cd ai-runtime/aiservd && cargo clean || true
	@echo "✓ Clean complete"

test:
	@echo "Running tests..."
	@cd ai-runtime/aiservd && cargo test

