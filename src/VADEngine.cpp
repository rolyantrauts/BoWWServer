#include "VADEngine.h"
#include <iostream>
#include <vector>

namespace boww {

    VADEngine::VADEngine(bool debug) 
        : debug_(debug), 
          env_(ORT_LOGGING_LEVEL_WARNING, "BoWW_VAD"),
          memory_info_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) 
    {}

    VADEngine::~VADEngine() {}

    bool VADEngine::Initialize(const std::string& model_path) {
        try {
            Ort::SessionOptions session_options;
            session_options.SetIntraOpNumThreads(1);
            session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), session_options);
            return true;
        } catch (const Ort::Exception& e) {
            std::cerr << "[VAD] Init Error: " << e.what() << std::endl;
            return false;
        }
    }

    std::shared_ptr<VADSessionState> VADEngine::CreateSessionState() {
        auto s = std::make_shared<VADSessionState>();
        s->state.resize(256, 0.0f);
        s->sr.push_back(16000);
        s->audio_buffer.clear();
        s->last_prob = 0.0f;
        s->smoothed_prob = 0.0f; // <-- NEW: Start at zero
        return s;
    }

    float VADEngine::Process(std::shared_ptr<VADSessionState> state_ptr, const std::vector<int16_t>& pcm_data) {
        if (!session_ || !state_ptr) return 0.0f;

        for (auto sample : pcm_data) {
            state_ptr->audio_buffer.push_back(static_cast<float>(sample) / 32768.0f);
        }

        const size_t SILERO_CHUNK_SIZE = 512;
        
        while (state_ptr->audio_buffer.size() >= SILERO_CHUNK_SIZE) {
            std::vector<float> input_tensor(state_ptr->audio_buffer.begin(), state_ptr->audio_buffer.begin() + SILERO_CHUNK_SIZE);
            state_ptr->audio_buffer.erase(state_ptr->audio_buffer.begin(), state_ptr->audio_buffer.begin() + SILERO_CHUNK_SIZE);

            std::vector<int64_t> input_shape = {1, static_cast<int64_t>(SILERO_CHUNK_SIZE)};
            std::vector<int64_t> state_shape = {2, 1, 128};
            std::vector<int64_t> sr_shape = {1};

            std::vector<Ort::Value> input_tensors;
            input_tensors.push_back(Ort::Value::CreateTensor<float>(memory_info_, input_tensor.data(), input_tensor.size(), input_shape.data(), input_shape.size()));
            input_tensors.push_back(Ort::Value::CreateTensor<float>(memory_info_, state_ptr->state.data(), state_ptr->state.size(), state_shape.data(), state_shape.size()));
            input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(memory_info_, state_ptr->sr.data(), state_ptr->sr.size(), sr_shape.data(), sr_shape.size()));

            const char* input_names[] = {"input", "state", "sr"};
            const char* output_names[] = {"output", "stateN"};

            try {
                auto output_tensors = session_->Run(
                    Ort::RunOptions{nullptr}, 
                    input_names, input_tensors.data(), 3, 
                    output_names, 2
                );

                float* new_state_data = output_tensors[1].GetTensorMutableData<float>();
                std::memcpy(state_ptr->state.data(), new_state_data, state_ptr->state.size() * sizeof(float));

                float* output_data = output_tensors[0].GetTensorMutableData<float>();
                state_ptr->last_prob = output_data[0]; 

            } catch (const std::exception& e) {
                if (debug_) std::cerr << "[VAD] Run Error: " << e.what() << std::endl;
            }
        }

        return state_ptr->last_prob;
    }
}
