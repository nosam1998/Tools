#pragma once
#include "Settings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace drift::apo {
class Processor {
  public:
    void prepare(double rate, unsigned channels, Parameters p) {
        channels_ = channels;
        step_ = 1.0 / (rate * 0.05); // 50 ms full-effect enable/disable ramp
        wet_ = 0;
        p.enabled = true;
        for (unsigned pair = 0; pair < (channels + 1) / 2; ++pair)
            engines_[pair].prepare(rate, p); // identical motion on every channel
    }
    void process(const float *input, float *output, std::size_t frames, bool silent,
                 Parameters p) noexcept {
        if (!p.enabled && wet_ == 0) {
            if (silent)
                std::memset(output, 0, frames * channels_ * sizeof(float));
            else if (input != output)
                std::memcpy(output, input, frames * channels_ * sizeof(float));
            return; // exact passthrough when fully disabled
        }
        const double target = p.enabled ? 1 : 0;
        p.enabled = true;
        std::array<float, 512> stereo{};
        std::array<float, 256> blend{};
        for (std::size_t begin = 0; begin < frames; begin += 256) {
            const auto count = std::min(std::size_t(256), frames - begin);
            for (std::size_t i = 0; i < count; ++i) {
                wet_ += std::clamp(target - wet_, -step_, step_);
                blend[i] = float(wet_);
            }
            for (unsigned ch = 0; ch < channels_; ch += 2) {
                const auto right = std::min(ch + 1, channels_ - 1);
                for (std::size_t i = 0; i < count; ++i) {
                    const auto offset = (begin + i) * channels_;
                    stereo[i * 2] = silent ? 0 : input[offset + ch];
                    stereo[i * 2 + 1] = silent ? 0 : input[offset + right];
                }
                engines_[ch / 2].process(stereo.data(), count, p);
                for (std::size_t i = 0; i < count; ++i) {
                    const auto offset = (begin + i) * channels_;
                    for (unsigned c = ch; c <= right; ++c) {
                        const float dry = silent ? 0 : input[offset + c];
                        const float clean = std::isfinite(dry) ? dry : 0;
                        output[offset + c] = clean + (stereo[i * 2 + c - ch] - clean) * blend[i];
                    }
                }
            }
        }
    }

  private:
    std::array<Engine, 16> engines_{};
    unsigned channels_ = 2;
    double wet_ = 0, step_ = 1;
};
} // namespace drift::apo
