#include "WavWriter.h"
#include <iostream>
#include <filesystem>

namespace boww {

    WavWriter::WavWriter() {}

    WavWriter::~WavWriter() {
        if (is_open_) CloseAndPublish();
    }

    bool WavWriter::Open(const std::string& temp_path, const std::string& dest_wav_path, const std::string& dest_json_path, 
                         nlohmann::json metadata, int sample_rate, int channels) {
        
        temp_path_ = temp_path;
        dest_wav_path_ = dest_wav_path;
        dest_json_path_ = dest_json_path;
        metadata_ = metadata;
        sample_rate_ = sample_rate;
        channels_ = channels;
        data_chunk_size_ = 0;

        file_.open(temp_path_, std::ios::binary);
        if (!file_.is_open()) {
            std::cerr << "[WavWriter] Error: Could not open temp file " << temp_path_ << "\n";
            return false;
        }

        WriteHeader();
        is_open_ = true;
        return true;
    }

    void WavWriter::Write(const std::vector<int16_t>& pcm_data) {
        if (!is_open_ || pcm_data.empty()) return;
        
        file_.write(reinterpret_cast<const char*>(pcm_data.data()), pcm_data.size() * sizeof(int16_t));
        data_chunk_size_ += static_cast<uint32_t>(pcm_data.size() * sizeof(int16_t));
    }

    void WavWriter::CloseAndPublish() {
        if (!is_open_) return;

        // 1. Finalize the WAV file size headers
        UpdateHeaderSizes();
        file_.close();
        is_open_ = false;

        // Calculate final duration for metadata
        int bytes_per_sec = sample_rate_ * channels_ * sizeof(int16_t);
        int duration_ms = static_cast<int>((static_cast<float>(data_chunk_size_) / bytes_per_sec) * 1000.0f);
        metadata_["duration_ms"] = duration_ms;

        try {
            // 2. Atomic Move (Copy then Remove to support crossing mount partitions like tmpfs)
            std::filesystem::copy(temp_path_, dest_wav_path_, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(temp_path_);
            
            // 3. Write the JSON Metadata Trigger
            std::ofstream json_file(dest_json_path_);
            if (json_file.is_open()) {
                json_file << metadata_.dump(4);
                json_file.close();
            }

            std::cout << "[WavWriter] Published -> " << dest_wav_path_ << " (" << duration_ms << "ms)\n";

        } catch (const std::exception& e) {
            std::cerr << "[WavWriter] Failed to publish file: " << e.what() << "\n";
        }
    }

    void WavWriter::WriteHeader() {
        file_.write("RIFF", 4);
        uint32_t chunk_size = 36 + data_chunk_size_; // Will update on close
        file_.write(reinterpret_cast<const char*>(&chunk_size), 4);
        file_.write("WAVE", 4);
        file_.write("fmt ", 4);
        uint32_t subchunk1_size = 16;
        file_.write(reinterpret_cast<const char*>(&subchunk1_size), 4);
        uint16_t audio_format = 1; // PCM
        file_.write(reinterpret_cast<const char*>(&audio_format), 2);
        file_.write(reinterpret_cast<const char*>(&channels_), 2);
        file_.write(reinterpret_cast<const char*>(&sample_rate_), 4);
        uint32_t byte_rate = sample_rate_ * channels_ * sizeof(int16_t);
        file_.write(reinterpret_cast<const char*>(&byte_rate), 4);
        uint16_t block_align = channels_ * sizeof(int16_t);
        file_.write(reinterpret_cast<const char*>(&block_align), 2);
        uint16_t bits_per_sample = 16;
        file_.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
        file_.write("data", 4);
        file_.write(reinterpret_cast<const char*>(&data_chunk_size_), 4); // Will update on close
    }

    void WavWriter::UpdateHeaderSizes() {
        if (!file_.is_open()) return;
        
        uint32_t chunk_size = 36 + data_chunk_size_;
        file_.seekp(4, std::ios::beg);
        file_.write(reinterpret_cast<const char*>(&chunk_size), 4);
        
        file_.seekp(40, std::ios::beg);
        file_.write(reinterpret_cast<const char*>(&data_chunk_size_), 4);
        
        file_.seekp(0, std::ios::end); // Reset pointer to end just in case
    }
}
