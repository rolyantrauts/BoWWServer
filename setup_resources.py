import os
import requests
import sys
from pathlib import Path

# --- CONFIGURATION ---
# Correct URL structure for v5.1.2
MODEL_URLS = [
    "https://github.com/snakers4/silero-vad/raw/v5.1.2/src/silero_vad/data/silero_vad.onnx",
]

DEST_DIR = Path("models")
DEST_FILE = DEST_DIR / "silero_vad.onnx"

def download_file(url, dest_path):
    print(f"Attempting download from: {url}")
    try:
        response = requests.get(url, stream=True)
        response.raise_for_status()
        
        # Check size to ensure we didn't get a 404 HTML page or empty file
        content_length = int(response.headers.get('content-length', 0))
        
        with open(dest_path, 'wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                f.write(chunk)
        
        file_size = dest_path.stat().st_size
        print(f"Download complete. Size: {file_size} bytes")
        
        # V5 model is approx 1.7MB.
        if file_size < 1500000: 
            print("WARNING: File seems too small. It might be V4 or corrupt.")
            
        return True
    except Exception as e:
        print(f"\nFailed: {e}")
        return False

def main():
    if not DEST_DIR.exists():
        DEST_DIR.mkdir(parents=True, exist_ok=True)
    
    # Always overwrite to ensure we replace the bad model
    if DEST_FILE.exists():
        print(f"Removing old model ({DEST_FILE.stat().st_size} bytes)...")
        DEST_FILE.unlink()

    for url in MODEL_URLS:
        if download_file(url, DEST_FILE):
            print(f"SUCCESS: Silero VAD V5 model saved to: {DEST_FILE.absolute()}")
            return
            
    print("\nFAILURE: Could not download model.")

if __name__ == "__main__":
    main()
