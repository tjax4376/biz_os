# BIZ_OS Development Environment Dockerfile
# Ubuntu-based development container with all required tools
#
# Version: 1.0
# Author: BIZ_OS Development Team
# Timestamp: 2025-01-27
# Change Rationale: Redesign development environment to use Docker instead of VirtualBox

FROM ubuntu:22.04

# Metadata
LABEL maintainer="BIZ_OS Development Team"
LABEL description="BIZ_OS Development Environment - Ubuntu 22.04 with kernel build tools, Rust, Python AI libraries, and Node.js"
LABEL version="1.0"

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Set working directory
WORKDIR /workspace

# Install system dependencies and development tools
RUN apt-get update && apt-get install -y \
    # Basic utilities
    git \
    curl \
    wget \
    vim \
    nano \
    sudo \
    ca-certificates \
    gnupg \
    lsb-release \
    # Build essentials
    build-essential \
    gcc \
    g++ \
    make \
    cmake \
    pkg-config \
    # Kernel build dependencies
    libncurses-dev \
    flex \
    bison \
    libssl-dev \
    libelf-dev \
    bc \
    dwarves \
    # Cross-compilation toolchains
    gcc-x86-64-linux-gnu \
    gcc-aarch64-linux-gnu \
    binutils-x86-64-linux-gnu \
    binutils-aarch64-linux-gnu \
    # Python and pip
    python3 \
    python3-pip \
    python3-dev \
    python3-venv \
    # Additional Python dependencies
    libpython3-dev \
    # Python package build dependencies
    libjpeg-dev \
    zlib1g-dev \
    libpng-dev \
    libffi-dev \
    libopenblas-dev \
    liblapack-dev \
    # Node.js dependencies
    software-properties-common \
    # Buildroot dependencies
    file \
    patch \
    cpio \
    unzip \
    rsync \
    # Network tools
    net-tools \
    iputils-ping \
    # Debugging tools
    gdb \
    strace \
    # Cleanup
    && rm -rf /var/lib/apt/lists/*

# Verify Make version (kernel builds require >= 4.0)
# Ubuntu 22.04 ships with Make 4.3, which meets the requirement
RUN make --version | head -n1 && \
    MAKE_MAJOR=$(make --version | head -n1 | grep -oE '^GNU Make [0-9]+' | grep -oE '[0-9]+') && \
    if [ -z "$MAKE_MAJOR" ] || [ "$MAKE_MAJOR" -lt 4 ]; then \
        echo "ERROR: Make version must be >= 4.0 for kernel builds. Found: $(make --version | head -n1)" && \
        exit 1; \
    else \
        echo "✓ Make version check passed (>= 4.0)"; \
    fi

# Install Rust toolchain
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
ENV PATH="/root/.cargo/bin:${PATH}"

# Install Node.js 20.x LTS
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get update \
    && apt-get install -y nodejs \
    && rm -rf /var/lib/apt/lists/*

# Install Python AI libraries
# Upgrade pip first
RUN pip3 install --no-cache-dir --upgrade pip setuptools wheel

# Install PyTorch CPU-only (lighter and faster for Docker, GPU not available in macOS Docker)
# Using CPU-only index to avoid CUDA dependencies
RUN pip3 install --no-cache-dir \
    torch \
    torchvision \
    torchaudio \
    --index-url https://download.pytorch.org/whl/cpu

# Install other AI libraries
RUN pip3 install --no-cache-dir \
    onnxruntime \
    transformers \
    accelerate \
    huggingface-hub \
    numpy \
    pandas \
    requests

# Install sqlite-vector (may need to be installed separately if it fails)
RUN pip3 install --no-cache-dir sqlite-vector || echo "Warning: sqlite-vector installation failed, may need to install manually"

# Create development user (non-root for security)
RUN useradd -m -s /bin/bash developer && \
    echo "developer ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers && \
    mkdir -p /workspace && \
    chown -R developer:developer /workspace

# Switch to development user
USER developer
WORKDIR /workspace

# Set up Rust environment for developer user
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
ENV PATH="/home/developer/.cargo/bin:${PATH}"

# Create entrypoint script
COPY --chown=developer:developer docker/docker-entrypoint.sh /home/developer/docker-entrypoint.sh
RUN chmod +x /home/developer/docker-entrypoint.sh

# Expose common development ports
# 8080: AI Runtime REST API
# 3000: UI Framework (React dev server)
# 5000: Python Flask (mapped to 5001 on host to avoid macOS AirPlay Receiver conflict)
EXPOSE 8080 3000 5000

# Set entrypoint
ENTRYPOINT ["/home/developer/docker-entrypoint.sh"]
CMD ["/bin/bash"]
