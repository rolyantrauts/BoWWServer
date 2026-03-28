#pragma once
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <alsa/asoundlib.h>
#include <nlohmann/json.hpp>

namespace boww {

    struct QueuedAudio {
        std::string wav_path;
        std::string json_path;
        nlohmann::json metadata;
    };

    enum class SinkState { FREE, LIVE_BUSY, FILE_BUSY };

    struct SinkDevice {
        SinkState state = SinkState::FREE;
        snd_pcm_t* handle = nullptr;
        std::queue<QueuedAudio> playback_queue;
        std::thread worker_thread;
        std::mutex device_mutex;
    };

    class AlsaSinkManager {
    public:
        AlsaSinkManager();
        ~AlsaSinkManager();

        // --- Live Streaming API ---
        // Returns true if the device is FREE and successfully locked for live streaming
        bool RequestLiveStream(const std::string& device_name);
        
        // Pushes raw audio to the open live stream
        void WriteLiveStream(const std::string& device_name, const std::vector<int16_t>& pcm_data);
        
        // Closes the live stream and triggers the background queue if files are waiting
        void CloseLiveStream(const std::string& device_name);

        // --- Queueing API ---
        // Adds a finished WAV to the queue. If the device is FREE, it starts playing immediately.
        void QueueWavFile(const std::string& device_name, const std::string& wav_path, 
                          const std::string& json_path, const nlohmann::json& metadata);

    private:
        std::map<std::string, std::shared_ptr<SinkDevice>> sinks_;
        std::mutex global_mutex_;

        std::shared_ptr<SinkDevice> GetOrCreateSink(const std::string& device_name);
        bool OpenAlsaDevice(std::shared_ptr<SinkDevice> sink, const std::string& device_name);
        void CloseAlsaDevice(std::shared_ptr<SinkDevice> sink);
        
        void ProcessQueue(std::shared_ptr<SinkDevice> sink, const std::string& device_name);
        void PlayWavFile(std::shared_ptr<SinkDevice> sink, const QueuedAudio& audio);
    };

}
