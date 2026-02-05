#!/bin/bash

echo "--- Installing System Libraries (PiOS/Debian) ---"
# PiOS usually ships Boost 1.74, which works PERFECTLY with WebSocket++
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libasound2-dev \
    libboost-all-dev \
    libwebsocketpp-dev \
    wget \
    tar

# 2. Setup Local Libs Directory
mkdir -p libs
cd libs

# 3. Fetch ONNX Runtime (Linux aarch64 for Pi 4/5)
ARCH=$(uname -m)
ONNX_VER="1.16.3"
BASE_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VER}"

# Detect Arch to ensure we get the right one (aarch64 vs armv7l)
if [ "$ARCH" == "aarch64" ]; then
    FILE="onnxruntime-linux-aarch64-${ONNX_VER}.tgz"
elif [ "$ARCH" == "x86_64" ]; then
    FILE="onnxruntime-linux-x64-${ONNX_VER}.tgz"
else
    echo "Warning: Architecture $ARCH detected. Assuming aarch64 for Pi 4/5 (64-bit OS)."
    FILE="onnxruntime-linux-aarch64-${ONNX_VER}.tgz"
fi

if [ ! -d "onnxruntime" ]; then
    echo "--- Downloading ONNX Runtime ($ARCH) ---"
    wget -q --show-progress "${BASE_URL}/${FILE}"
    
    if [ -f "$FILE" ]; then
        echo "Extracting..."
        tar -xzf $FILE
        mv "onnxruntime-linux-${ARCH}-${ONNX_VER}" onnxruntime
        rm $FILE
    else
        echo "Failed to download ONNX Runtime."
        exit 1
    fi
else
    echo "ONNX Runtime already detected."
fi

cd ..
echo "--- Environment Setup Complete ---"
