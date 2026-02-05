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
        // Initialize State: 2 * 1 * 128 (Silero V5)
        s->state.resize(256, 0.0f);
        // Initialize SR: 16000
        s->sr.push_back(16000);
        return s;
    }

    float VADEngine::Process(std::shared_ptr<VADSessionState> state_ptr, const std::vector<int16_t>& pcm_data) {
        if (!session_ || !state_ptr) return 0.0f;

        // 1. Prepare Input (Normalize Int16 -> Float32)
        // Silero expects flat float array
        size_t input_len = pcm_data.size();
        std::vector<float> input_tensor(input_len);
        for (size_t i = 0; i < input_len; ++i) {
            input_tensor[i] = static_cast<float>(pcm_data[i]) / 32768.0f;
        }

        // 2. Define Shapes
        // Input: [1, N]
        std::vector<int64_t> input_shape = {1, static_cast<int64_t>(input_len)};
        // State: [2, 1, 128]
        std::vector<int64_t> state_shape = {2, 1, 128};
        // SR: [1]
        std::vector<int64_t> sr_shape = {1};

        // 3. Create ORT Tensors
        std::vector<Ort::Value> input_tensors;
        input_tensors.push_back(Ort::Value::CreateTensor<float>(memory_info_, input_tensor.data(), input_tensor.size(), input_shape.data(), input_shape.size()));
        input_tensors.push_back(Ort::Value::CreateTensor<float>(memory_info_, state_ptr->state.data(), state_ptr->state.size(), state_shape.data(), state_shape.size()));
        input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(memory_info_, state_ptr->sr.data(), state_ptr->sr.size(), sr_shape.data(), sr_shape.size()));

        // 4. Run Inference
        const char* input_names[] = {"input", "state", "sr"};
        const char* output_names[] = {"output", "stateN"};

        try {
            auto output_tensors = session_->Run(
                Ort::RunOptions{nullptr}, 
                input_names, input_tensors.data(), 3, 
                output_names, 2
            );

            // 5. Update State (stateN -> state)
            float* new_state_data = output_tensors[1].GetTensorMutableData<float>();
            std::memcpy(state_ptr->state.data(), new_state_data, state_ptr->state.size() * sizeof(float));

            // 6. Get Probability (output is [1, 2], we want output[0][1] for speech)
            // But V5 output shape is [1, N] actually containing probabilities?
            // Actually V5 standard: output is [1, 1] prob? No, usually [1, batch].
            // Let's assume standard Silero [1, 1] output for prob.
            
            float* output_data = output_tensors[0].GetTensorMutableData<float>();
            return output_data[0]; 

        } catch (const std::exception& e) {
            if (debug_) std::cerr << "[VAD] Run Error: " << e.what() << std::endl;
            return 0.0f;
        }
    }
}
