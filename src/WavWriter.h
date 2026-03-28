#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace boww {

    class WavWriter {
    public:
        WavWriter();
        ~WavWriter();

        // Opens the temp file and writes the initial WAV header
        bool Open(const std::string& temp_path, const std::string& dest_wav_path, const std::string& dest_json_path, 
                  nlohmann::json metadata, int sample_rate, int channels);

        // Appends raw PCM data to the file
        void Write(const std::vector<int16_t>& pcm_data);

        // Updates the WAV header sizes, closes the file, moves it to dest_dir, and writes the JSON trigger
        void CloseAndPublish();

        bool IsOpen() const { return is_open_; }
        std::string GetTempPath() const { return temp_path_; }

    private:
        std::ofstream file_;
        bool is_open_ = false;
        
        std::string temp_path_;
        std::string dest_wav_path_;
        std::string dest_json_path_;
        nlohmann::json metadata_;
        
        uint32_t data_chunk_size_ = 0;
        int sample_rate_;
        int channels_;

        void WriteHeader();
        void UpdateHeaderSizes();
    };

}
