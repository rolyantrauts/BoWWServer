#pragma once
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace boww {

    class SimpleAGC {
    private:
        std::deque<std::vector<float>> delay_line_;
        float current_gain_;
        float target_peak_;
        float max_gain_;
        float attack_coeff_;
        float release_coeff_;
        bool initialized_ = false;

    public:
        // Capped max_gain at 5.0f to prevent turning fans into jet engines
        SimpleAGC(int chunks=5, float target=0.80f, float max_g=5.0f, float att=0.05f, float rel=1.5f, float sr=16000.0f)
            : current_gain_(1.0f), target_peak_(target), max_gain_(max_g) {
            attack_coeff_ = std::exp(-1.0f / (att * sr));
            release_coeff_ = std::exp(-1.0f / (rel * sr));
        }

        void Process(std::vector<int16_t>& pcm_data) {
            if (pcm_data.empty()) return;

            // Dynamically initialize the delay line to match whatever chunk size the network sent
            if (!initialized_) {
                for (int i = 0; i < 5; ++i) {
                    delay_line_.push_back(std::vector<float>(pcm_data.size(), 0.0f));
                }
                initialized_ = true;
            }

            // 1. Convert incoming 16-bit PCM to floats (-1.0 to 1.0)
            std::vector<float> in_float(pcm_data.size());
            for (size_t i = 0; i < pcm_data.size(); ++i) {
                in_float[i] = static_cast<float>(pcm_data[i]) / 32768.0f;
            }

            // 2. Push to delay line and find the peak amplitude
            delay_line_.push_back(in_float);
            float max_peak = 0.0001f; 
            for (const auto& chunk : delay_line_) {
                for (float val : chunk) {
                    if (std::abs(val) > max_peak) max_peak = std::abs(val);
                }
            }
            
            // 3. Calculate Target Gain and pop the oldest chunk out of the delay line
            float target_gain = std::min(target_peak_ / max_peak, max_gain_);
            std::vector<float> out_float = delay_line_.front();
            delay_line_.pop_front();

            // 4. Apply smoothed gain curve and convert back to 16-bit PCM
            for (size_t i = 0; i < out_float.size(); ++i) {
                if (target_gain < current_gain_) {
                    current_gain_ = attack_coeff_ * current_gain_ + (1.0f - attack_coeff_) * target_gain;
                } else {
                    current_gain_ = release_coeff_ * current_gain_ + (1.0f - release_coeff_) * target_gain;
                }
                
                float clamped_sample = std::clamp(out_float[i] * current_gain_, -1.0f, 1.0f);
                pcm_data[i] = static_cast<int16_t>(clamped_sample * 32767.0f);
            }
        }
    };
}
