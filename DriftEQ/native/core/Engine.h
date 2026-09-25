#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace drift {
constexpr std::size_t bandCount = 5;
constexpr std::array<double, bandCount> frequencies{120, 400, 1200, 3500, 9000};
struct Parameters {
    float depth = 0.8f;
    float seconds = 6.0f;
    float volume = 0.6f;
    bool enabled = true;
    bool pulse = false;
};
// UI writes atomics; the audio callback takes one nonblocking snapshot per block.
// No pointers, allocations, or UI objects cross the audio boundary.
struct Controls {
    std::atomic<float> depth{0.8f}, seconds{6.0f}, volume{0.6f};
    std::atomic<bool> enabled{true}, pulse{false};
    Parameters read() const noexcept;
    void set(const Parameters &) noexcept;
};
static_assert(std::atomic<float>::is_always_lock_free, "Audio parameters must be lock free");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Audio counters must be lock free");
Parameters preset(unsigned index) noexcept;

class Engine {
  public:
    // prepare is called only while audio is stopped. Processing is stereo float32.
    void prepare(double sampleRate, const Parameters &, std::uint32_t seed = 729413);
    void process(float *interleavedStereo, std::size_t frames, const Parameters &) noexcept;
    std::array<double, bandCount> gains() const noexcept;

  private:
    struct Coefficients {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct Band {
        std::uint32_t random = 1;
        double from = 0, target = 0, phase = 0, duration = 1, value = 0;
        Coefficients current, step;
        std::array<double, 2> z1{}, z2{};
    };
    std::array<Band, bandCount> bands_{};
    double sampleRate_ = 48000, depth_ = 0.8, volume_ = 0.6, wet_ = 1;
    double smooth_ = 0, crossfade_ = 0, limiterRelease_ = 0, limiter_ = 1;
    unsigned controlFrame_ = 0;
    bool pulse_ = false;
    static double random(Band &) noexcept;
    static Coefficients coefficients(double frequency, double gain, double rate) noexcept;
};
} // namespace drift
