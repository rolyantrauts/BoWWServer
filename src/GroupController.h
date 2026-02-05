#pragma once
#include <map>
#include <vector>
#include <deque>
#include <string>
#include <memory>
#include <mutex>
#include <chrono>

#include "BoWWServerDefs.h"
#include "VADEngine.h"
#include "ClientSession.h"
#include "AudioOutputRouter.h"
#include "SimpleAGC.h"

namespace boww {

    enum class GroupState {
        IDLE,
        ARBITRATING,
        LOCKED 
    };

    struct ConfidenceEntry {
        float score;
        std::weak_ptr<ClientSession> session;
    };

    class GroupController {
    public:
        GroupController(GroupConfig config, VADEngine& vad_engine, bool debug_mode = false);
        
        void HandleConfidenceScore(std::shared_ptr<ClientSession> session, float score);
        void OnTick();
        void HandleAudioStream(std::shared_ptr<ClientSession> session, const std::vector<int16_t>& pcm_data);

    private:
        GroupConfig config_;
        VADEngine& vad_engine_;
        AudioOutputRouter audio_router_;
        SimpleAGC agc_; 
        bool debug_mode_;
        
        std::mutex mutex_;
        GroupState state_ = GroupState::IDLE;
        
        std::map<std::string, ConfidenceEntry> candidates_;
        std::shared_ptr<ClientSession> active_streamer_;
        std::chrono::steady_clock::time_point arbitration_start_time_;

        std::deque<int16_t> ingest_buffer_;      
        std::vector<int16_t> alsa_accumulator_;  
        
        const size_t VAD_CHUNK_SIZE = 512;       
        const size_t JITTER_TARGET = 2048;       

        void ResolveArbitration();
        void ResetGroup();
    };
}
