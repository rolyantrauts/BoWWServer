import asyncio
import websockets
import json
import sys
import socket
import wave
import time
from zeroconf import ServiceBrowser, Zeroconf

# --- CONFIGURATION ---
TARGET_SERVICE = "_boww._tcp.local."
WAV_FILE = "jfk-sil.wav"
CHUNK_SIZE = 1024  # 64ms chunks

class BoWWListener:
    def __init__(self):
        self.found_ip = None
        self.found_port = None
        self.event = asyncio.Event()

    def remove_service(self, *args): pass
    def update_service(self, *args): pass

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        if info:
            address = socket.inet_ntoa(info.addresses[0])
            print(f"\n[mDNS] Found Server: {name} at {address}:{info.port}")
            self.found_ip = address
            self.found_port = info.port
            self.event.set()

async def receive_messages(websocket, stop_event):
    """Listens for Stop signals from the server."""
    try:
        async for message in websocket:
            data = json.loads(message)
            if data.get("type") == "stop":
                print("\n[Server] Sent STOP signal (VAD Timeout triggered).")
                stop_event.set() # Tell sender to stop
                return
    except:
        pass

async def send_file_audio(websocket, stop_event):
    """Streams the WAV file in real-time."""
    try:
        with wave.open(WAV_FILE, 'rb') as wf:
            # 1. Verify Format
            if wf.getnchannels() != 1 or wf.getframerate() != 16000 or wf.getsampwidth() != 2:
                print(f"Error: {WAV_FILE} must be 16kHz, Mono, 16-bit PCM.")
                print(f"Current: {wf.getframerate()}Hz, {wf.getnchannels()}ch, {wf.getsampwidth()*8}bit")
                stop_event.set()
                return

            print(f"Streaming '{WAV_FILE}'...")
            
            # Calculate sleep time to match 1x playback speed
            # 1024 samples / 16000 Hz = 0.064 seconds (64ms)
            interval = CHUNK_SIZE / 16000.0
            
            chunk_count = 0
            
            while not stop_event.is_set():
                data = wf.readframes(CHUNK_SIZE)
                
                # If EOF, we can loop silence or just stop. 
                # Since you added silence to the file, we just stop when file ends.
                if len(data) == 0:
                    print("\n[Client] End of file reached. Waiting for Server VAD...")
                    # Keep connection open so server can send STOP signal
                    while not stop_event.is_set():
                        await asyncio.sleep(0.1)
                    break

                await websocket.send(data)
                
                # Real-time pacing
                await asyncio.sleep(interval)
                
                chunk_count += 1
                if chunk_count % 15 == 0:
                    sys.stdout.write(".")
                    sys.stdout.flush()

    except FileNotFoundError:
        print(f"\nError: Could not find '{WAV_FILE}'")
        stop_event.set()
    except websockets.exceptions.ConnectionClosed:
        print("\n[Network] Pipe broken.")
        stop_event.set()

async def run_client():
    # 1. Discovery
    print(f"Scanning for {TARGET_SERVICE}...")
    zc = Zeroconf()
    listener = BoWWListener()
    ServiceBrowser(zc, TARGET_SERVICE, listener)

    try:
        await asyncio.wait_for(listener.event.wait(), timeout=5.0)
    except asyncio.TimeoutError:
        print("Error: BoWW Server not found via mDNS.")
        return
    finally:
        zc.close()

    uri = f"ws://{listener.found_ip}:{listener.found_port}"
    print(f"Connecting to {uri}...")

    # 2. Connection & Onboarding
    async with websockets.connect(uri) as websocket:
        print("--- Connected! ---")
        my_guid = None

        async for message in websocket:
            try:
                data = json.loads(message)
                if data.get("type") == "assign_id":
                    my_guid = data["id"]
                    print(f">>> AUTHORIZED! GUID: {my_guid}")
                    break 
            except: pass

    # 3. Streaming Phase
    if my_guid:
        print(f"--- Reconnecting as {my_guid} ---")
        async with websockets.connect(uri) as websocket:
            # Handshake
            await websocket.send(json.dumps({"type": "hello", "guid": my_guid}))
            
            # Trigger
            print("Sending Confidence: 1.0")
            await websocket.send(json.dumps({"type": "confidence", "value": 1.0}))
            
            # Wait for ACK
            resp = await websocket.recv()
            print(f"Server ACK: {resp}")

            # Coordination Event
            stop_event = asyncio.Event()

            # Run Sender and Receiver
            sender_task = asyncio.create_task(send_file_audio(websocket, stop_event))
            receiver_task = asyncio.create_task(receive_messages(websocket, stop_event))

            # Wait for stop signal or completion
            await stop_event.wait()
            
            # Cleanup
            sender_task.cancel()
            receiver_task.cancel()
                
            print("\nSession Ended.")

if __name__ == "__main__":
    try:
        asyncio.run(run_client())
    except KeyboardInterrupt:
        print("\nExiting.")
