#include "GroupController.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <algorithm> 

namespace boww {

    GroupController::GroupController(GroupConfig config, VADEngine& vad_engine, bool debug_mode)
        : config_(config), vad_engine_(vad_engine), audio_router_(config), debug_mode_(debug_mode) 
    {
        std::cout << "[Group: " << config.name << "] Initialized." << std::endl;
        alsa_accumulator_.reserve(JITTER_TARGET * 2);
    }

    void GroupController::HandleConfidenceScore(std::shared_ptr<ClientSession> session, float score) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == GroupState::LOCKED) return;

        candidates_[session->GetID()] = {score, session};
        std::cout << "[Group: " << config_.name << "] Candidate: " << session->GetID() << " Score: " << score << std::endl;

        if (state_ == GroupState::IDLE) {
            state_ = GroupState::ARBITRATING;
            arbitration_start_time_ = std::chrono::steady_clock::now();
            std::cout << "[Group: " << config_.name << "] Arbitration started." << std::endl;
        }
    }

    void GroupController::OnTick() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        if (state_ == GroupState::ARBITRATING) {
            long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - arbitration_start_time_).count();
            if (elapsed >= config_.arbitration_timeout_ms) ResolveArbitration();
        }
        else if (state_ == GroupState::LOCKED) {
            if (!active_streamer_) { ResetGroup(); return; }

            long silence_duration = active_streamer_->GetTimeSinceLastVoiceMs();
            if (silence_duration > config_.vad_no_voice_ms) {
                std::cout << "[Group: " << config_.name << "] VAD Timeout (" << silence_duration << "ms). Stopping." << std::endl;
                
                active_streamer_->SendStopSignal();
                for (auto const& [guid, candidate] : candidates_) {
                    if (auto s = candidate.session.lock()) {
                         if (s != active_streamer_) s->SendStopSignal();
                    }
                }
                ResetGroup();
            }
        }
    }

    void GroupController::ResolveArbitration() {
        float best_score = -1.0f;
        std::shared_ptr<ClientSession> winner = nullptr;

        for (auto it = candidates_.begin(); it != candidates_.end();) {
            if (auto s = it->second.session.lock()) {
                if (it->second.score > best_score) {
                    best_score = it->second.score;
                    winner = s;
                }
                ++it;
            } else { it = candidates_.erase(it); }
        }

        if (winner) {
            std::cout << "[Group: " << config_.name << "] Winner: " << winner->GetID() << std::endl;
            state_ = GroupState::LOCKED;
            active_streamer_ = winner;
            
            ingest_buffer_.clear();
            alsa_accumulator_.clear();

            winner->InitVADState(vad_engine_.CreateSessionState());
            audio_router_.OpenStream(winner->GetID());

            for (auto const& [guid, candidate] : candidates_) {
                if (auto s = candidate.session.lock()) {
                    if (s != winner) s->SendStopSignal();
                }
            }
        } else {
            ResetGroup();
        }
    }

    void GroupController::ResetGroup() {
        state_ = GroupState::IDLE;
        candidates_.clear();
        active_streamer_ = nullptr;
        audio_router_.CloseStream();
        ingest_buffer_.clear();
        alsa_accumulator_.clear();
    }

    void GroupController::HandleAudioStream(std::shared_ptr<ClientSession> session, const std::vector<int16_t>& pcm_data) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_ != GroupState::LOCKED || session != active_streamer_) return;

        // --- STAGE 1: INGEST (RAW) ---
        for (int16_t sample : pcm_data) {
            ingest_buffer_.push_back(sample);
        }

        // --- STAGE 2: PROCESS ---
        static std::vector<int16_t> raw_chunk(VAD_CHUNK_SIZE);
        static std::vector<int16_t> agc_chunk(VAD_CHUNK_SIZE);
        static int debug_counter = 0; 

        while (ingest_buffer_.size() >= VAD_CHUNK_SIZE) {
            // 2a. Split Path
            for (size_t i = 0; i < VAD_CHUNK_SIZE; ++i) {
                int16_t val = ingest_buffer_.front();
                raw_chunk[i] = val; // Path A: Output
                agc_chunk[i] = val; // Path B: Detection
                ingest_buffer_.pop_front();
            }

            // 2b. Path B: AGC + VAD
            agc_.Process(agc_chunk);
            float voice_prob = vad_engine_.Process(active_streamer_->GetVADState(), agc_chunk);
            
            if (debug_mode_ && ++debug_counter % 10 == 0) {
               int16_t debug_amp = 0;
               for(auto s : agc_chunk) if(std::abs(s) > debug_amp) debug_amp = std::abs(s);
               std::cout << "[VAD] Prob: " << std::fixed << std::setprecision(2) << voice_prob 
                         << " | Sidechain Amp: " << debug_amp 
                         << " | Gain: " << std::setprecision(1) << agc_.GetCurrentGain() << "x" << std::endl;
            }

            if (voice_prob > 0.5f) {
                active_streamer_->UpdateLastVoiceTime();
            }

            // 2c. Path A: Output (Attenuated Raw)
            for (int16_t raw_sample : raw_chunk) {
                alsa_accumulator_.push_back(static_cast<int16_t>(raw_sample * 0.4f));
            }
        }

        // --- STAGE 3: WRITE ---
        if (alsa_accumulator_.size() >= JITTER_TARGET) {
            audio_router_.WriteChunk(alsa_accumulator_);
            alsa_accumulator_.clear();
        }
    }
}
