#include "AudioOutputRouter.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>

#ifdef __LINUX_ALSA__
    #include <alsa/asoundlib.h>
#endif

namespace boww {

    struct WavHeader {
        char riff[4] = {'R', 'I', 'F', 'F'};
        uint32_t overall_size = 0;
        char wave[4] = {'W', 'A', 'V', 'E'};
        char fmt_chunk_marker[4] = {'f', 'm', 't', ' '};
        uint32_t length_of_fmt = 16;
        uint16_t format_type = 1; 
        uint16_t channels = 1;
        uint32_t sample_rate = 16000;
        uint32_t byterate = 0;
        uint16_t block_align = 0;
        uint16_t bits_per_sample = 16;
        char data_chunk_header[4] = {'d', 'a', 't', 'a'};
        uint32_t data_size = 0;
    };

    AudioOutputRouter::AudioOutputRouter(const GroupConfig& config) 
        : config_(config), is_busy_(false) {}

    AudioOutputRouter::~AudioOutputRouter() {
        CloseStream();
    }

    bool AudioOutputRouter::OpenStream(const std::string& source_client_guid) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_busy_) return false;

        is_busy_ = true;
        using_fallback_ = false;

        if (config_.output_type == OutputType::FILE) {
            std::string fname = GenerateFilename(source_client_guid);
            wav_file_handle_.open(fname, std::ios::binary);
            if (wav_file_handle_.is_open()) {
                WavHeader h;
                h.sample_rate = config_.sample_rate;
                h.channels = config_.channels;
                h.block_align = config_.channels * 2;
                h.byterate = h.sample_rate * h.block_align;
                wav_file_handle_.write(reinterpret_cast<const char*>(&h), sizeof(WavHeader));
                std::cout << "[Router] Recording to: " << fname << std::endl;
                return true;
            }
        } 
        else if (config_.output_type == OutputType::ALSA) {
            #ifdef __LINUX_ALSA__
                // Basic ALSA Open
                int err = snd_pcm_open((snd_pcm_t**)&alsa_handle_, config_.output_target.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
                if (err < 0) {
                    std::cerr << "[Router] ALSA Error: " << snd_strerror(err) << std::endl;
                    if (config_.fallback_to_file_on_busy) {
                        using_fallback_ = true;
                        std::cout << "[Router] Fallback to FILE." << std::endl;
                        // Recursive call to open file instead
                        config_.output_type = OutputType::FILE;
                        bool res = OpenStream(source_client_guid);
                        config_.output_type = OutputType::ALSA; // Restore config
                        return res;
                    }
                    is_busy_ = false;
                    return false;
                }
                
                snd_pcm_set_params((snd_pcm_t*)alsa_handle_,
                                   SND_PCM_FORMAT_S16_LE,
                                   SND_PCM_ACCESS_RW_INTERLEAVED,
                                   config_.channels,
                                   config_.sample_rate,
                                   1,
                                   50000); // 50ms latency
                return true;
            #else
                std::cerr << "[Router] ALSA not compiled." << std::endl;
            #endif
        }

        return false;
    }

    void AudioOutputRouter::WriteChunk(const std::vector<int16_t>& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_busy_) return;

        if (wav_file_handle_.is_open()) {
            wav_file_handle_.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(int16_t));
        }
        else if (alsa_handle_) {
            #ifdef __LINUX_ALSA__
                snd_pcm_sframes_t frames = snd_pcm_writei((snd_pcm_t*)alsa_handle_, data.data(), data.size());
                if (frames < 0) {
                    frames = snd_pcm_recover((snd_pcm_t*)alsa_handle_, frames, 0);
                }
            #endif
        }
    }

    void AudioOutputRouter::CloseStream() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!is_busy_) return;

        if (config_.output_type == OutputType::ALSA && !using_fallback_) {
            #ifdef __LINUX_ALSA__
                if (alsa_handle_) {
                    snd_pcm_drain((snd_pcm_t*)alsa_handle_);
                    snd_pcm_close((snd_pcm_t*)alsa_handle_);
                    alsa_handle_ = nullptr;
                }
            #endif
        }
        
        if (wav_file_handle_.is_open()) {
            long file_length = wav_file_handle_.tellp();
            long data_length = file_length - sizeof(WavHeader);
            
            wav_file_handle_.seekp(4, std::ios::beg);
            uint32_t overall = file_length - 8;
            wav_file_handle_.write(reinterpret_cast<const char*>(&overall), 4);
            
            wav_file_handle_.seekp(40, std::ios::beg);
            uint32_t dlen = (uint32_t)data_length;
            wav_file_handle_.write(reinterpret_cast<const char*>(&dlen), 4);
            
            wav_file_handle_.close();
            std::cout << "[Router] File closed and header patched." << std::endl;
        }

        is_busy_ = false;
    }

    bool AudioOutputRouter::IsBusy() const {
        return is_busy_;
    }

    std::string AudioOutputRouter::GenerateFilename(const std::string& guid) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y%m%d-%H%M%S");
        
        // Ensure wav/ dir exists
        std::filesystem::create_directory("wav");

        return "wav/" + guid + "_" + config_.name + "_" + ss.str() + ".wav";
    }
}
