#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace drift {
// One producer, one consumer. Storage is allocated before callbacks begin.
// A small adaptive interpolation step reconciles independent hardware clocks.
class AudioQueue {
  public:
    static constexpr std::size_t capacity = 32768;
    void reset(std::size_t target = 2048) noexcept {
        read_ = 0;
        write_ = 0;
        fraction_ = 0;
        fade_ = 0;
        tailRemaining_ = 0;
        last_[0] = last_[1] = 0;
        primed_ = false;
        target_ = std::clamp(target, std::size_t(64), capacity / 4);
        underruns = 0;
        overruns = 0;
    }
    std::size_t push(const float *input, std::size_t frames) noexcept {
        const auto w = write_.load(std::memory_order_relaxed),
                   r = read_.load(std::memory_order_acquire);
        const auto accepted = std::min(frames, capacity - std::size_t(w - r));
        for (std::size_t i = 0; i < accepted; ++i) {
            const auto slot = std::size_t((w + i) % capacity) * 2;
            data_[slot] = input ? input[i * 2] : 0;
            data_[slot + 1] = input ? input[i * 2 + 1] : 0;
        }
        write_.store(w + accepted, std::memory_order_release);
        if (accepted < frames)
            overruns.fetch_add(1, std::memory_order_relaxed);
        return accepted;
    }
    void pull(float *output, std::size_t frames) noexcept {
        auto r = read_.load(std::memory_order_relaxed);
        const auto w = write_.load(std::memory_order_acquire);
        const auto available = std::size_t(w - r);
        if (!primed_ && available >= target_) {
            primed_ = true;
            fade_ = 0;
        }
        if (!primed_) {
            writeTail(output, frames);
            return;
        }
        if (available > target_ * 4) {
            r = w - target_;
            fraction_ = 0;
            fade_ = 0;
            overruns.fetch_add(1, std::memory_order_relaxed);
        }
        const double step = std::clamp(
            1 + 0.002 * (double(w - r) - double(target_)) / double(target_), 0.998, 1.002);
        for (std::size_t i = 0; i < frames; ++i) {
            if (w - r < 2) {
                // Ramp the final sample to silence rather than cutting a waveform.
                tailRemaining_ = 64;
                writeTail(output + i * 2, frames - i);
                primed_ = false;
                fraction_ = 0;
                underruns.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            const auto a = std::size_t(r % capacity) * 2, b = std::size_t((r + 1) % capacity) * 2;
            fade_ = std::min(1.0, fade_ + 1.0 / 128);
            for (unsigned c = 0; c < 2; ++c) {
                last_[c] =
                    float((data_[a + c] + (data_[b + c] - data_[a + c]) * fraction_) * fade_);
                output[i * 2 + c] = last_[c];
            }
            fraction_ += step;
            const auto advance = std::uint64_t(fraction_);
            r += advance;
            fraction_ -= double(advance);
        }
        read_.store(r, std::memory_order_release);
    }
    std::size_t queued() const noexcept {
        const auto r = read_.load(std::memory_order_acquire);
        const auto w = write_.load(std::memory_order_acquire);
        return std::size_t(w - r);
    }
    std::atomic<std::uint64_t> underruns{0}, overruns{0};

  private:
    void writeTail(float *output, std::size_t frames) noexcept {
        for (std::size_t i = 0; i < frames; ++i) {
            if (tailRemaining_)
                --tailRemaining_;
            const float gain = float(tailRemaining_) / 64.0f;
            output[i * 2] = last_[0] * gain;
            output[i * 2 + 1] = last_[1] * gain;
        }
    }
    unsigned tailRemaining_ = 0;
    std::array<float, capacity * 2> data_{};
    alignas(64) std::atomic<std::uint64_t> read_{0};
    alignas(64) std::atomic<std::uint64_t> write_{0};
    std::size_t target_ = 2048;
    double fraction_ = 0, fade_ = 0;
    bool primed_ = false;
    float last_[2]{};
};
} // namespace drift
