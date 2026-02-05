# BoWW Server (Broadcast-On-Wakeword)

A high-performance, hardware-aware audio streaming server designed for the Raspberry Pi (Zero 2 W / 4 / 5).

This server manages audio ingestion from distributed clients via WebSockets, utilizing a sophisticated "Sidechain" DSP architecture. It decouples machine hearing (VAD) from human listening (Recording) to ensure high-precision detection without compromising the dynamic range of the collected dataset.

## 🚀 Features

* **Zero-Conf Discovery:** Automatically discoverable on the network via mDNS/Bonjour (`_boww._tcp`).
* **Group Arbitration:** Handles multiple clients competing for the same audio channel using confidence scores and mutex locking.
* **Sidechain DSP Architecture:**
    * **Path A (Detection):** Aggressive AGC + Silero VAD (V5) for >99% speech detection accuracy.
    * **Path B (Recording):** Clean, dynamic audio path with safety limiting (anti-clipping) for high-quality ASR datasets.
* **Hardware Efficient:** Written in C++17, utilizing ONNX Runtime and WebSocket++ for low-latency performance on edge devices.

---

## 🛠️ Installation (Raspberry Pi)

### 1. Clone the Repository
```bash
git clone [https://github.com/yourusername/boww_server.git](https://github.com/yourusername/boww_server.git)
cd boww_server
# 2. Install Dependencies
# We provide a helper script to install system libraries (ALSA, Boost, Avahi) and fetch the specific ARM64 binary for ONNX Runtime (v1.16.3).

chmod +x setup_env_pi.sh
./setup_env_pi.sh

# 3. Fetch AI Models
# Download the specific Silero VAD V5 model required by the pipeline.

python3 setup_resources.py

# This places silero_vad.onnx into the models/ directory.
```
🏗️ Build Instructions  
The project uses CMake and links against the local ONNX Runtime found in libs/  
```
mkdir build
cd build
cmake ..
make -j2  # Use -j1 on Pi Zero if memory is tight
# Run the Server
# Standard run
./boww_server

# Debug mode (View VAD probabilities, AGC gain levels, and mDNS logs)
./boww_server --debug
```
🧪 Testing (Python Client)  
Included is test_client_discovery.py, a robust test harness that simulates a hardware client (like an ESP32 or another Pi).  

Prerequisites  
You need a 16kHz mono WAV file named jfk-sil.wav in the same directory (or update the script variable WAV_FILE)  
```
pip install websockets zeroconf
python3 test_client_discovery.py
```
Test Workflow  
Discovery: Scans mDNS for _boww._tcp.  

Handshake: Connects and authenticates via clients.yaml.  

Arbitration: Sends a confidence score ({"type": "confidence", "value": 1.0}).  

Streaming: Streams audio in 64ms chunks upon winning the floor.  

Auto-Stop: Server detects silence via VAD and sends a STOP command; client disconnects.  

⚙️ Process Architecture  
The BoWW Server operates as a stateful pipeline designed to optimize both detection and recording quality simultaneously.  

1. Discovery & Handshake  
The server broadcasts availability via Avahi (mDNS).  

Clients connect via persistent WebSocket.  

Clients are authenticated against clients.yaml. Unknown clients are assigned a temp-ID for onboarding.  

2. The "Sidechain" Audio Pipeline  
When a client streams audio, the signal is split into two parallel processing paths:  

Path A: The VAD Sidechain (The Brain)  

Input: Raw Audio  

AGC: Applies aggressive gain (targeting -4dB) to normalize whispers or distant speech.  

Inference: The boosted signal is fed to Silero VAD V5 via ONNX Runtime.  

Result: High-precision Probability output (0.0 - 1.0).  

Path B: The Audio Sink (The File)  

Input: Raw Audio (Same source as A).  

Processing: The AGC is bypassed to preserve natural dynamics.  

Safety Limiter: Signal is multiplied by 0.4 to prevent hardware clipping.  

Output: Written to disk (WAV) or Hardware Output (ALSA).  

3. State Management  
Jitter Buffer: Smooths out network inconsistency before writing to disk.  

VAD Logic: Maintains a "Speech State". If silence persists beyond vad_no_voice_ms (configurable), the server autonomously closes the file and terminates the stream.  
