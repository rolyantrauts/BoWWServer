#pragma once
#include "BoWWServerDefs.h"
#include <vector>
#include <mutex>
#include <fstream>
#include <string>

namespace boww {

    class AudioOutputRouter {
    public:
        AudioOutputRouter(const GroupConfig& config);
        ~AudioOutputRouter();

        bool OpenStream(const std::string& source_client_guid);
        void WriteChunk(const std::vector<int16_t>& data);
        void CloseStream();
        bool IsBusy() const;

    private:
        GroupConfig config_;
        bool is_busy_ = false;
        bool using_fallback_ = false;

        std::ofstream wav_file_handle_;
        void* alsa_handle_ = nullptr; 
        std::mutex mutex_;
        
        std::string GenerateFilename(const std::string& guid);
    };
}
