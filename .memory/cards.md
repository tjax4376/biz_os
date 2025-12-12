# BIZ_OS Development Memory Cards

## Common Issues and Solutions

### Issue: Unsafe or Ambiguous Auto-Remediation Requirements
**Problem**: Requests for \"auto-resolve issues using AI\" can lead to unsafe designs (e.g., executing arbitrary LLM-suggested commands) and unclear scope (what issues, what actions, what approvals).
**Solution**: Require a fully specified requirements block before coding: define data sources, detection scope, a deterministic remediation action catalog (allowlisted, parameter-validated), policy gates (suggest-only default, explicit execute enablement), authN/Z, auditing, rollback, and failure-safe behavior (LLM failure must not trigger execution).
**Status**: In progress - requirements + STRIDE/PHA + test plan drafted before implementation

### Issue: Scope Definition Ambiguity
**Problem**: "Operating System" can mean many things - full OS vs. application layer vs. containerized environment
**Solution**: User chose full OS (Option A). Pragmatic approach: Start with Linux kernel base, customize extensively, build custom userland. Evolve toward fully custom kernel in future phases.
**Status**: Resolved - Hybrid approach documented in MVP_PLAN.md

### Issue: Research Phase Requirements
**Problem**: Need to research feasibility before planning
**Solution**: Conduct comprehensive research, document findings, ask clarifying questions before proceeding
**Status**: Completed - Research documented in RESEARCH.md

### Issue: Full OS Complexity for Solo Developer
**Problem**: Building full OS from scratch is extremely complex for solo developer + AI
**Solution**: Hybrid approach - Linux kernel base + custom userland. Focus on unique AI capabilities. Leverage AI code generation extensively. Use existing open-source components.
**Status**: Mitigated - Strategy documented in MVP_PLAN.md

### Issue: User Rules Compliance
**Problem**: Must follow user rules for journal creation, memory cards, and planning
**Solution**: Created JOURNAL.md, .memory/cards.md, and structured planning documents
**Status**: Implemented

### Issue: Ambitious Requirements vs. MVP Timeline
**Problem**: User wants full OS replacing all business apps, but MVP timeline is limited
**Solution**: Define MVP scope focusing on core use case (automating repetitive tasks). Exclude full CRM, accounting, etc. from MVP. Build foundation for future expansion.
**Status**: Resolved - MVP scope defined in REQUIREMENTS.md

## Development Principles

1. **Pragmatic Full OS**: Linux kernel base + custom userland (meets full OS requirement pragmatically)
2. **Focus on Core Use Case**: MVP focuses on automating repetitive tasks from learned patterns
3. **Leverage AI**: Use AI code generation extensively for development acceleration
4. **Iterative Development**: 6-phase plan with clear milestones
5. **Document Everything**: All decisions and architecture documented

## Architecture Decisions

1. **Kernel**: Customized Linux 6.x (foundation for future custom kernel)
2. **AI Runtime**: ONNX Runtime with local LLM (Llama 3.2/Mistral quantized)
3. **UI Framework**: Web-based for MVP (faster development, can migrate to native later)
4. **Development Languages**: Rust (safety) + Python (AI ecosystem)
5. **GPU Support**: CUDA/OpenCL for local model acceleration

## Current Status

- ✅ Research phase completed
- ✅ Requirements gathered (all 20 questions answered)
- ✅ Requirements specification created (REQUIREMENTS.md)
- ✅ MVP development plan created and optimized (MVP_PLAN.md)
- ✅ Technical architecture designed (ARCHITECTURE.md)
- ✅ All clarification questions answered
- ✅ Timeline optimized: 12 months → 6 months
- ✅ Technology decisions finalized (Mistral 7B, web-based UI)
- ✅ Quick-start guide created (QUICK_START.md)
- ✅ **AI Task Manager feature implemented** (kernel hooks, diagnostic service, REST API)
- ✅ **Ready to begin Phase 1 development**

## Timeline Optimization Learnings

**Issue**: User requested fastest possible timeline
**Solution**: 
- Compressed phases from sequential to parallel/overlapping
- Reduced MVP scope further (core features only)
- Aggressive AI code generation strategy
- Leverage existing tools extensively
- Incremental testing throughout

**Result**: 12 months → 6 months (aggressive but achievable)

## Technology Decisions Finalized

1. **Kernel**: Linux 6.x base (customized) ✅
2. **LLM**: Mistral 7B quantized (~4GB) ✅
3. **UI MVP**: Web-based (Electron/CEF) ✅
4. **UI Future**: Native/web choice at setup ✅
5. **Timeline**: 6 months (24 weeks) ✅

## New Features Implemented

### AI Task Manager (2025-01-27)
**Feature**: AI-powered system diagnostics with natural language queries
**Components**:
- Kernel syscalls: `sys_get_system_metrics`, `sys_diagnose_system`
- Diagnostic service module: `ai-runtime/diagnostic-service/`
- REST API endpoint: `POST /api/v1/diagnose` (port 8080)
- Integration: Part of aiservd daemon

**Status**: ✅ Implemented (basic version, ready for Mistral LLM integration)
**Files**: 
- `kernel/syscalls/ai_syscalls.c` (updated)
- `ai-runtime/diagnostic-service/` (new module)
- `ai-runtime/aiservd/src/main.rs` (updated)

### Docker Development Environment (2025-01-27)
**Issue**: VirtualBox doesn't work on MacBook, need alternative development environment
**Solution**: Created Docker-based Ubuntu development environment with all required tools
**Components**:
- Dockerfile: Ubuntu 22.04 base with Rust, Python AI libraries, Node.js, kernel build tools
- docker-compose.yml: Container orchestration with volume mounts and port forwarding
- Helper scripts: docker-build.sh, docker-run.sh, docker-shell.sh
- Documentation: DOCKER_SETUP.md with comprehensive guide

**Benefits**:
- ✅ No VirtualBox dependency
- ✅ Faster startup (~5 seconds vs 30-60 seconds)
- ✅ Lower resource overhead
- ✅ Consistent environment across team
- ✅ Volume mounts for code persistence
- ✅ Cached builds for faster rebuilds

**Status**: ✅ Implemented
**Files**:
- `Dockerfile` (new, updated to fix Python package installation)
- `docker-compose.yml` (new)
- `docker/docker-entrypoint.sh` (new)
- `.dockerignore` (new)
- `scripts/docker-build.sh` (new)
- `scripts/docker-run.sh` (new)
- `scripts/docker-shell.sh` (new)
- `docs/DOCKER_SETUP.md` (new)
- `docs/DEVELOPMENT.md` (updated)
- `scripts/setup-dev-env.sh` (updated)

**Build Fixes Applied**:
- Added missing Python system dependencies (libjpeg-dev, zlib1g-dev, libpng-dev, libffi-dev, libopenblas-dev, liblapack-dev)
- Split PyTorch installation to use CPU-only index (avoids CUDA dependencies, faster builds)
- Split pip installations into separate RUN commands for better error handling
- Fixed Node.js installation to update apt-get before installing

### Build Instructions Documentation (2025-01-27)
**Issue**: No comprehensive build instructions for downloading and compiling BIZ_OS
**Solution**: Created BUILD_INSTRUCTIONS.md with complete workflow from git clone to OS compilation using Bizos-dev Docker container
**Components**:
- Prerequisites (Git, Docker, system requirements)
- Git clone instructions
- Docker setup and container building (3 methods: docker-compose, script, manual)
- Kernel compilation steps (with architecture options)
- AI runtime compilation (kernel modules + Rust daemon)
- Testing procedures
- Comprehensive troubleshooting section
- Quick reference for essential commands

**Status**: ✅ Implemented
**Files**:
- `BUILD_INSTRUCTIONS.md` (new, ~600 lines)
- `JOURNAL.md` (updated with session notes)

