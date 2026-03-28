#pragma once
#include <string>
#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>

namespace boww {

    struct VADSessionState {
        std::vector<float> state;
        std::vector<int64_t> sr; 
        std::vector<float> audio_buffer; 
        
        float last_prob = 0.0f;
        float smoothed_prob = 0.0f; // <-- NEW: Tracks the EMA
    };

    class VADEngine {
    public:
        VADEngine(bool debug = false);
        ~VADEngine();

        bool Initialize(const std::string& model_path);
        float Process(std::shared_ptr<VADSessionState> state, const std::vector<int16_t>& pcm_data);
        std::shared_ptr<VADSessionState> CreateSessionState();

    private:
        bool debug_;
        Ort::Env env_;
        std::unique_ptr<Ort::Session> session_;
        Ort::MemoryInfo memory_info_;
    };
}
