#pragma once
#include "BoWWServerDefs.h"
#include "ClientSession.h"
#include "VADEngine.h"
#include "SimpleAGC.h"
#include "WavWriter.h"
#include "AlsaSinkManager.h"
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>

namespace boww {

    enum class GroupState {
        IDLE,
        ARBITRATING,
        STREAMING
    };

    class GroupController {
    public:
        GroupController(GroupConfig config, VADEngine& vad_engine, ServerConfig server_config, AlsaSinkManager& alsa_manager, bool debug_mode = false);
        ~GroupController(); 

        void HandleConfidenceScore(std::shared_ptr<ClientSession> session, float score);
        void HandleAudioStream(std::shared_ptr<ClientSession> session, std::vector<int16_t>& pcm_data); 
        void OnTick();
        
        GroupConfig GetConfig() const { return config_; } // <-- NEW

    private:
        GroupConfig config_;
        ServerConfig server_config_;
        VADEngine& vad_engine_;
        AlsaSinkManager& alsa_manager_;
        bool debug_mode_;
        std::mutex mutex_;
        
        WavWriter wav_writer_;
        SimpleAGC agc_engine_;

        GroupState state_ = GroupState::IDLE;
        std::chrono::steady_clock::time_point arbitration_start_time_;
        
        std::shared_ptr<ClientSession> best_candidate_ = nullptr;
        float best_score_ = 0.0f;
        std::string active_client_guid_ = "";
        
        std::vector<std::shared_ptr<ClientSession>> current_candidates_;
        std::map<std::string, std::vector<int16_t>> arbitration_buffers_;

        bool is_live_streaming_ = false;
        std::string current_base_filename_;
        nlohmann::json current_metadata_;
    };
}
