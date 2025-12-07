#!/bin/bash
# Download Mistral 7B Quantized Model
# Downloads the Mistral 7B model in GGUF format for BIZ_OS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MODEL_DIR="$PROJECT_ROOT/ai-runtime/models/mistral7b"

echo "BIZ_OS Mistral 7B Model Download"
echo "================================"
echo ""

# Create model directory
mkdir -p "$MODEL_DIR"

# Check if model already exists
if [ -f "$MODEL_DIR/mistral-7b-instruct-v0.2.Q4_K_M.gguf" ]; then
    echo "Model already exists at: $MODEL_DIR"
    echo "Skipping download."
    exit 0
fi

echo "Downloading Mistral 7B quantized model..."
echo "Model will be saved to: $MODEL_DIR"
echo ""
echo "Options:"
echo "1. Download from Hugging Face (requires huggingface-hub)"
echo "2. Manual download instructions"
echo ""
read -p "Choose option (1/2): " -n 1 -r
echo

if [[ $REPLY =~ ^[1]$ ]]; then
    # Check for huggingface-hub
    if ! command -v huggingface-cli &> /dev/null; then
        echo "Installing huggingface-hub..."
        pip3 install huggingface-hub
    fi
    
    echo "Downloading from Hugging Face..."
    huggingface-cli download TheBloke/Mistral-7B-Instruct-v0.2-GGUF \
        mistral-7b-instruct-v0.2.Q4_K_M.gguf \
        --local-dir "$MODEL_DIR" \
        --local-dir-use-symlinks False
    
    echo ""
    echo "✓ Model downloaded successfully!"
else
    echo ""
    echo "Manual download instructions:"
    echo "1. Visit: https://huggingface.co/TheBloke/Mistral-7B-Instruct-v0.2-GGUF"
    echo "2. Download: mistral-7b-instruct-v0.2.Q4_K_M.gguf"
    echo "3. Save to: $MODEL_DIR"
    echo ""
    echo "Or use wget:"
    echo "wget -O $MODEL_DIR/mistral-7b-instruct-v0.2.Q4_K_M.gguf \\"
    echo "  https://huggingface.co/TheBloke/Mistral-7B-Instruct-v0.2-GGUF/resolve/main/mistral-7b-instruct-v0.2.Q4_K_M.gguf"
fi

echo ""
echo "Model size: ~4GB"
echo "After download, update model_manager.rs to use the correct path."

