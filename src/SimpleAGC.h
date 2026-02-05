#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace boww {

    class SimpleAGC {
    public:
        // Target 20000 (approx -4dB) for optimal VAD resolution
        SimpleAGC(int16_t target_level = 20000, float max_gain = 30.0f) 
            : target_rms_(target_level), max_gain_(max_gain) {}

        void Process(std::vector<int16_t>& buffer) {
            if (buffer.empty()) return;

            long long sum_squares = 0;
            for (int16_t sample : buffer) {
                sum_squares += sample * sample;
            }
            float rms = std::sqrt(sum_squares / buffer.size());

            // Noise Gate: If signal is floor noise, relax gain to unity
            if (rms < 100.0f) {
                current_gain_ = 0.95f * current_gain_ + 0.05f * 1.0f; 
            } 
            else {
                float needed_gain = target_rms_ / (rms + 1.0f);
                
                // Clamp limits
                if (needed_gain > max_gain_) needed_gain = max_gain_;
                if (needed_gain < 0.1f) needed_gain = 0.1f; 

                // Attack (Loud input) = Fast (0.2)
                // Release (Quiet input) = Slow (0.01)
                float alpha = (needed_gain < current_gain_) ? 0.2f : 0.01f; 
                current_gain_ = (1.0f - alpha) * current_gain_ + alpha * needed_gain;
            }

            // Apply Gain
            for (size_t i = 0; i < buffer.size(); ++i) {
                float val = buffer[i] * current_gain_;
                
                if (val > 32767.0f) val = 32767.0f;
                if (val < -32768.0f) val = -32768.0f;
                
                buffer[i] = static_cast<int16_t>(val);
            }
        }
        
        float GetCurrentGain() const { return current_gain_; }

    private:
        float target_rms_;
        float max_gain_;
        float current_gain_ = 1.0f; 
    };
}
