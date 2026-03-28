#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace boww {

    constexpr int DEFAULT_SAMPLE_RATE = 16000;
    constexpr int DEFAULT_CHANNELS = 1;

    namespace Protocol {
        const std::string MSG_HELLO = "hello";           
        const std::string MSG_HELLO_ACK = "hello_ack"; // <-- NEW
        const std::string MSG_ENROLL = "enroll";                 
        const std::string MSG_ASSIGN_TEMP_ID = "assign_temp_id"; 
        const std::string MSG_CONFIDENCE = "confidence"; 
        const std::string MSG_CONF_REC = "conf_rec";     
        const std::string MSG_START = "start";           
        const std::string MSG_STOP = "stop";             
        const std::string MSG_ASSIGN_ID = "assign_id";   
    }

    enum class OutputType { ALSA, FILE };

    struct ServerConfig {
        std::string temp_dir = "/tmp/boww/";
        std::string dest_dir = "./output/";
    };

    struct GroupConfig {
        std::string name;
        int sample_rate = DEFAULT_SAMPLE_RATE;
        int channels = DEFAULT_CHANNELS;
        bool use_agc = false; 
        float vad_threshold = 0.5f; 
        int arbitration_timeout_ms = 200;
        int vad_no_voice_ms = 1000;
        float preroll_seconds = 2.0f; // <-- NEW
        OutputType output_type = OutputType::FILE;
        std::string output_target; 
        bool fallback_to_file_on_busy = true;
    };

    struct ClientInfo {
        std::string guid;
        std::string group_name;
    };
}
