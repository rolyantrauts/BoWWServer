#pragma once
#include "BoWWServerDefs.h"
#include "ClientSession.h"
#include "AudioOutputRouter.h"
#include "VADEngine.h"
#include "SimpleAGC.h"
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>

namespace boww {

    enum class GroupState {
        IDLE,
        ARBITRATING,
        STREAMING
    };

    class GroupController {
    public:
        GroupController(GroupConfig config, VADEngine& vad_engine, bool debug_mode = false);
        ~GroupController(); 

        void HandleConfidenceScore(std::shared_ptr<ClientSession> session, float score);
        void HandleAudioStream(std::shared_ptr<ClientSession> session, std::vector<int16_t>& pcm_data); 
        void OnTick();

    private:
        GroupConfig config_;
        VADEngine& vad_engine_;
        bool debug_mode_;
        std::mutex mutex_;
        
        AudioOutputRouter router_;
        SimpleAGC agc_engine_;

        GroupState state_ = GroupState::IDLE;
        std::chrono::steady_clock::time_point arbitration_start_time_;
        
        std::shared_ptr<ClientSession> best_candidate_ = nullptr;
        float best_score_ = 0.0f;
        std::string active_client_guid_ = "";
        
        // --- NEW: Arbitration Tracking ---
        std::vector<std::shared_ptr<ClientSession>> current_candidates_;
        std::map<std::string, std::vector<int16_t>> arbitration_buffers_;
    };
}
