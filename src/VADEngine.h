#pragma once
#include <string>
#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>

namespace boww {

    // Helper struct to hold state per-session
    struct VADSessionState {
        // Silero V5 state shape: [2, 1, 128]
        std::vector<float> state;
        std::vector<int64_t> sr; // Sample Rate container
    };

    class VADEngine {
    public:
        VADEngine(bool debug = false);
        ~VADEngine();

        bool Initialize(const std::string& model_path);
        
        // Returns probability 0.0 - 1.0
        float Process(std::shared_ptr<VADSessionState> state, const std::vector<int16_t>& pcm_data);

        // Factory for per-client state
        std::shared_ptr<VADSessionState> CreateSessionState();

    private:
        bool debug_;
        Ort::Env env_;
        std::unique_ptr<Ort::Session> session_;
        Ort::MemoryInfo memory_info_;
    };
}
