#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace boww {

    constexpr int DEFAULT_SAMPLE_RATE = 16000;
    constexpr int DEFAULT_CHANNELS = 1;

    namespace Protocol {
        const std::string MSG_HELLO = "hello";           
        const std::string MSG_CONFIDENCE = "confidence"; 
        const std::string MSG_CONF_REC = "conf_rec";     
        const std::string MSG_STOP = "stop";             
        const std::string MSG_ASSIGN_ID = "assign_id";   
    }

    enum class OutputType { ALSA, FILE };

    struct GroupConfig {
        std::string name;
        int sample_rate = DEFAULT_SAMPLE_RATE;
        int channels = DEFAULT_CHANNELS;
        int arbitration_timeout_ms = 200;
        int vad_no_voice_ms = 1000;
        OutputType output_type = OutputType::FILE;
        std::string output_target; 
        bool fallback_to_file_on_busy = true;
    };

    struct ClientInfo {
        std::string guid;
        std::string group_name;
    };
}
