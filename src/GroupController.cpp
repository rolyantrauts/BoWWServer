#include "GroupController.h"
#include <iostream>
#include <chrono>
#include <cmath>
#include <ctime>
#include <sstream>

namespace boww {

    GroupController::GroupController(GroupConfig config, VADEngine& vad_engine, ServerConfig server_config, AlsaSinkManager& alsa_manager, bool debug_mode)
        : config_(std::move(config)), vad_engine_(vad_engine), server_config_(server_config), alsa_manager_(alsa_manager), debug_mode_(debug_mode) 
    {
        std::cout << "[Group: " << config_.name << "] Initialized with VAD Threshold: " << config_.vad_threshold << "\n";
    }

    GroupController::~GroupController() {
        if (state_ == GroupState::STREAMING) {
            wav_writer_.CloseAndPublish();
            if (is_live_streaming_) {
                alsa_manager_.CloseLiveStream(config_.output_target);
            }
        }
    }

    void GroupController::HandleConfidenceScore(std::shared_ptr<ClientSession> client, float score) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == GroupState::IDLE) {
            state_ = GroupState::ARBITRATING;
            arbitration_start_time_ = std::chrono::steady_clock::now();
            
            best_candidate_ = client;
            best_score_ = score;
            
            current_candidates_.clear();
            arbitration_buffers_.clear();
            current_candidates_.push_back(client);
            
            std::cout << "[Group: " << config_.name << "] Candidate: " << client->GetID() << " Score: " << score << "\n";
            std::cout << "[Group: " << config_.name << "] Arbitration started.\n";
            
        } else if (state_ == GroupState::ARBITRATING) {
            std::cout << "[Group: " << config_.name << "] Candidate: " << client->GetID() << " Score: " << score << "\n";
            current_candidates_.push_back(client);
            
            if (score > best_score_) {
                best_candidate_ = client;
                best_score_ = score;
            }
        }
    }

    void GroupController::HandleAudioStream(std::shared_ptr<ClientSession> client, std::vector<int16_t>& pcm_data) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == GroupState::ARBITRATING) {
            auto& buffer = arbitration_buffers_[client->GetID()];
            buffer.insert(buffer.end(), pcm_data.begin(), pcm_data.end());
            return; 
        }

        if (state_ != GroupState::STREAMING || active_client_guid_ != client->GetID()) {
            return; 
        }

        int64_t sum = 0;
        for (int16_t sample : pcm_data) sum += sample;
        int16_t mean = static_cast<int16_t>(sum / static_cast<int64_t>(pcm_data.size()));
        for (size_t i = 0; i < pcm_data.size(); ++i) {
            pcm_data[i] = static_cast<int16_t>(pcm_data[i] - mean);
        }

        if (config_.use_agc) {
            agc_engine_.Process(pcm_data);
        }

        wav_writer_.Write(pcm_data);
        if (is_live_streaming_) {
            alsa_manager_.WriteLiveStream(config_.output_target, pcm_data);
        }

        float raw_prob = vad_engine_.Process(client->GetVADState(), pcm_data);
        auto state = client->GetVADState();
        state->smoothed_prob = (0.8f * state->smoothed_prob) + (0.2f * raw_prob);

        if (debug_mode_) {
            int16_t peak = 0;
            for (auto sample : pcm_data) {
                if (std::abs(sample) > peak) peak = std::abs(sample);
            }
            std::cout << "[VAD] Raw: " << raw_prob << " | Smoothed: " << state->smoothed_prob << " | Peak Amp: " << peak << "\n";
        }

        if (state->smoothed_prob > config_.vad_threshold) { 
            client->UpdateLastVoiceTime();
        }
    }

    void GroupController::OnTick() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        if (state_ == GroupState::ARBITRATING) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - arbitration_start_time_).count();
            
            if (elapsed >= config_.arbitration_timeout_ms) {
                if (best_candidate_) {
                    active_client_guid_ = best_candidate_->GetID();
                    state_ = GroupState::STREAMING;
                    
                    std::cout << "[Group: " << config_.name << "] Winner: " << active_client_guid_ << "\n";
                    
                    best_candidate_->InitVADState(vad_engine_.CreateSessionState());
                    
                    auto in_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    
                    std::stringstream ss;
                    ss << config_.name << "_" << active_client_guid_ << "_" << in_time_t;
                    current_base_filename_ = ss.str();

                    std::string temp_wav = server_config_.temp_dir + current_base_filename_ + ".wav";
                    std::string dest_wav = server_config_.dest_dir + current_base_filename_ + ".wav";
                    std::string dest_json = server_config_.dest_dir + current_base_filename_ + ".json";

                    current_metadata_["group"] = config_.name;
                    current_metadata_["guid"] = active_client_guid_;
                    current_metadata_["timestamp"] = in_time_t;
                    current_metadata_["file"] = current_base_filename_ + ".wav";
                    current_metadata_["output_mode"] = (config_.output_type == OutputType::ALSA) ? "alsa" : "file";
                    if (config_.output_type == OutputType::ALSA) {
                        current_metadata_["device"] = config_.output_target;
                    }
                    current_metadata_["played_from_queue"] = false;

                    wav_writer_.Open(temp_wav, dest_wav, dest_json, current_metadata_, config_.sample_rate, config_.channels);

                    is_live_streaming_ = false;
                    if (config_.output_type == OutputType::ALSA) {
                        is_live_streaming_ = alsa_manager_.RequestLiveStream(config_.output_target);
                        if (!is_live_streaming_) {
                            std::cout << "[Group: " << config_.name << "] ALSA Device " << config_.output_target << " is BUSY! Falling back to queue.\n";
                        }
                    }

                    // Process Pre-roll Buffer
                    if (arbitration_buffers_.count(active_client_guid_)) {
                        auto& pre_roll = arbitration_buffers_[active_client_guid_];
                        if (!pre_roll.empty()) {
                            
                            int64_t sum = 0;
                            for (int16_t sample : pre_roll) sum += sample;
                            int16_t mean = static_cast<int16_t>(sum / static_cast<int64_t>(pre_roll.size()));
                            for (size_t i = 0; i < pre_roll.size(); ++i) pre_roll[i] = static_cast<int16_t>(pre_roll[i] - mean);

                            if (config_.use_agc) agc_engine_.Process(pre_roll);
                            
                            wav_writer_.Write(pre_roll);
                            if (is_live_streaming_) alsa_manager_.WriteLiveStream(config_.output_target, pre_roll);
                            
                            // 1. Warm-up VAD internal memory on the pre-roll (ignoring its raw output probability)
                            vad_engine_.Process(best_candidate_->GetVADState(), pre_roll);
                        }
                    }
                    
                    // 2. THE WAKE WORD JUST FIRED: Force VAD state to 1.0 (Voice Detected) to give momentum to the live stream!
                    best_candidate_->GetVADState()->smoothed_prob = 1.0f;
                    best_candidate_->UpdateLastVoiceTime();

                    arbitration_buffers_.clear(); 
                    best_candidate_->SendStartSignal(); 
                    
                    for (auto& candidate : current_candidates_) {
                        if (candidate->GetID() != active_client_guid_) {
                            candidate->SendStopSignal();
                        }
                    }
                    current_candidates_.clear();
                    
                } else {
                    state_ = GroupState::IDLE; 
                }
            }
        } 
        else if (state_ == GroupState::STREAMING) {
            if (best_candidate_) {
                long silence_ms = best_candidate_->GetTimeSinceLastVoiceMs();
                
                if (silence_ms >= config_.vad_no_voice_ms) {
                    std::cout << "[Group: " << config_.name << "] VAD Timeout (" << silence_ms << "ms). Stopping.\n";
                    
                    wav_writer_.CloseAndPublish();
                    
                    if (config_.output_type == OutputType::ALSA) {
                        if (is_live_streaming_) {
                            alsa_manager_.CloseLiveStream(config_.output_target);
                            is_live_streaming_ = false;
                        } else {
                            std::string dest_wav = server_config_.dest_dir + current_base_filename_ + ".wav";
                            std::string dest_json = server_config_.dest_dir + current_base_filename_ + ".json";
                            alsa_manager_.QueueWavFile(config_.output_target, dest_wav, dest_json, current_metadata_);
                        }
                    }

                    best_candidate_->SendStopSignal();
                    
                    state_ = GroupState::IDLE;
                    active_client_guid_ = "";
                    best_candidate_ = nullptr;
                }
            }
        }
    }
}
