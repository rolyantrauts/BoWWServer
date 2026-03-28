#include "AlsaSinkManager.h"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace boww {

    AlsaSinkManager::AlsaSinkManager() {}

    AlsaSinkManager::~AlsaSinkManager() {
        std::lock_guard<std::mutex> lock(global_mutex_);
        for (auto& pair : sinks_) {
            std::lock_guard<std::mutex> dev_lock(pair.second->device_mutex);
            if (pair.second->handle) {
                snd_pcm_close(pair.second->handle);
            }
            if (pair.second->worker_thread.joinable()) {
                pair.second->worker_thread.join();
            }
        }
    }

    std::shared_ptr<SinkDevice> AlsaSinkManager::GetOrCreateSink(const std::string& device_name) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        if (sinks_.find(device_name) == sinks_.end()) {
            sinks_[device_name] = std::make_shared<SinkDevice>();
        }
        return sinks_[device_name];
    }

    bool AlsaSinkManager::OpenAlsaDevice(std::shared_ptr<SinkDevice> sink, const std::string& device_name) {
        if (snd_pcm_open(&sink->handle, device_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
            std::cerr << "[AlsaManager] Failed to open ALSA device: " << device_name << "\n";
            return false;
        }

        // 1. Hardware Parameters
        snd_pcm_hw_params_t* hw_params;
        snd_pcm_hw_params_alloca(&hw_params);
        snd_pcm_hw_params_any(sink->handle, hw_params);
        snd_pcm_hw_params_set_access(sink->handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(sink->handle, hw_params, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(sink->handle, hw_params, 1);
        unsigned int rate = 16000;
        snd_pcm_hw_params_set_rate_near(sink->handle, hw_params, &rate, 0);

        snd_pcm_uframes_t buffer_size = 8192; // 512ms buffer total
        snd_pcm_uframes_t period_size = 1024; // 64ms periods
        snd_pcm_hw_params_set_buffer_size_near(sink->handle, hw_params, &buffer_size);
        
        // --- FIXED: Added the 0 (null pointer) for the 4th argument ---
        snd_pcm_hw_params_set_period_size_near(sink->handle, hw_params, &period_size, 0); 
        
        snd_pcm_hw_params(sink->handle, hw_params);

        // 2. Software Parameters (The ALSA Defenses)
        snd_pcm_sw_params_t* sw_params;
        snd_pcm_sw_params_alloca(&sw_params);
        snd_pcm_sw_params_current(sink->handle, sw_params);

        // DEFENSE A: Start Threshold (Jitter Buffer)
        // Wait until we have 2400 frames (150ms) before physically starting playback
        snd_pcm_sw_params_set_start_threshold(sink->handle, sw_params, 2400);

        // DEFENSE B: Silence Padding
        // If the buffer drops below 2400 frames, automatically inject zeroes to prevent underruns
        snd_pcm_sw_params_set_silence_threshold(sink->handle, sw_params, 2400);
        snd_pcm_sw_params_set_silence_size(sink->handle, sw_params, 2400);

        snd_pcm_sw_params(sink->handle, sw_params);
        snd_pcm_prepare(sink->handle);
        return true;
    }

    void AlsaSinkManager::CloseAlsaDevice(std::shared_ptr<SinkDevice> sink) {
        if (sink->handle) {
            snd_pcm_drain(sink->handle); // Play remaining buffer
            snd_pcm_close(sink->handle);
            sink->handle = nullptr;
        }
    }

    bool AlsaSinkManager::RequestLiveStream(const std::string& device_name) {
        auto sink = GetOrCreateSink(device_name);
        std::lock_guard<std::mutex> lock(sink->device_mutex);

        if (sink->state != SinkState::FREE) {
            return false; // Device is busy, client must use WavWriter and queue it
        }

        if (OpenAlsaDevice(sink, device_name)) {
            sink->state = SinkState::LIVE_BUSY;
            std::cout << "[AlsaManager] " << device_name << " locked for LIVE streaming.\n";
            return true;
        }
        return false;
    }

    void AlsaSinkManager::WriteLiveStream(const std::string& device_name, const std::vector<int16_t>& pcm_data) {
        auto sink = GetOrCreateSink(device_name);
        std::lock_guard<std::mutex> lock(sink->device_mutex);
        
        if (sink->state == SinkState::LIVE_BUSY && sink->handle) {
            int frames = snd_pcm_writei(sink->handle, pcm_data.data(), pcm_data.size());
            if (frames < 0) {
                // If it still manages to underrun, recover gracefully
                snd_pcm_recover(sink->handle, frames, 1);
            }
        }
    }

    void AlsaSinkManager::CloseLiveStream(const std::string& device_name) {
        auto sink = GetOrCreateSink(device_name);
        std::unique_lock<std::mutex> lock(sink->device_mutex);

        if (sink->state == SinkState::LIVE_BUSY) {
            CloseAlsaDevice(sink);
            sink->state = SinkState::FREE;
            std::cout << "[AlsaManager] " << device_name << " released from LIVE streaming.\n";
            
            // Check if anyone queued files while we were live!
            if (!sink->playback_queue.empty()) {
                lock.unlock(); // Unlock before spinning up thread
                ProcessQueue(sink, device_name);
            }
        }
    }

    void AlsaSinkManager::QueueWavFile(const std::string& device_name, const std::string& wav_path, 
                                       const std::string& json_path, const nlohmann::json& metadata) {
        auto sink = GetOrCreateSink(device_name);
        std::unique_lock<std::mutex> lock(sink->device_mutex);

        QueuedAudio qa{wav_path, json_path, metadata};
        sink->playback_queue.push(qa);
        
        std::cout << "[AlsaManager] Queued file for " << device_name << ". Queue size: " << sink->playback_queue.size() << "\n";

        if (sink->state == SinkState::FREE) {
            lock.unlock();
            ProcessQueue(sink, device_name);
        }
    }

    void AlsaSinkManager::ProcessQueue(std::shared_ptr<SinkDevice> sink, const std::string& device_name) {
        std::lock_guard<std::mutex> lock(sink->device_mutex);
        
        if (sink->state != SinkState::FREE || sink->playback_queue.empty()) return;
        
        sink->state = SinkState::FILE_BUSY;

        // Clean up old thread if it exists
        if (sink->worker_thread.joinable()) {
            sink->worker_thread.join();
        }

        // Spin up background thread to play the queue
        sink->worker_thread = std::thread([this, sink, device_name]() {
            while (true) {
                QueuedAudio audio;
                {
                    std::lock_guard<std::mutex> q_lock(sink->device_mutex);
                    if (sink->playback_queue.empty()) {
                        sink->state = SinkState::FREE;
                        std::cout << "[AlsaManager] Queue empty. " << device_name << " is now FREE.\n";
                        break;
                    }
                    audio = sink->playback_queue.front();
                    sink->playback_queue.pop();
                }

                PlayWavFile(sink, audio);
            }
        });
    }

    void AlsaSinkManager::PlayWavFile(std::shared_ptr<SinkDevice> sink, const QueuedAudio& audio) {
        std::cout << "[AlsaManager] Playing queued file: " << audio.wav_path << "\n";
        
        {
            std::lock_guard<std::mutex> lock(sink->device_mutex);
            if (!OpenAlsaDevice(sink, audio.metadata["device"].get<std::string>())) return;
        }

        std::ifstream file(audio.wav_path, std::ios::binary);
        if (file.is_open()) {
            file.seekg(44); // Skip standard WAV header
            
            std::vector<int16_t> buffer(1024);
            while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size() * sizeof(int16_t))) {
                std::lock_guard<std::mutex> lock(sink->device_mutex);
                if (sink->handle) {
                    int frames = snd_pcm_writei(sink->handle, buffer.data(), buffer.size());
                    if (frames < 0) snd_pcm_recover(sink->handle, frames, 1);
                }
            }
            
            // Handle remainder
            size_t remaining = file.gcount() / sizeof(int16_t);
            if (remaining > 0) {
                std::lock_guard<std::mutex> lock(sink->device_mutex);
                if (sink->handle) {
                    snd_pcm_writei(sink->handle, buffer.data(), remaining);
                }
            }
            file.close();
        }

        {
            std::lock_guard<std::mutex> lock(sink->device_mutex);
            CloseAlsaDevice(sink);
        }

        // Fire the JSON trigger now that playback is complete!
        std::ofstream json_file(audio.json_path);
        if (json_file.is_open()) {
            nlohmann::json final_meta = audio.metadata;
            final_meta["played_from_queue"] = true; // Let the downstream STT know it was delayed
            json_file << final_meta.dump(4);
            json_file.close();
        }
    }
}
