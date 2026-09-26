#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace drift {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double headroom = 0.251188643150958; // -12 dB; shared by dry and wet.
constexpr unsigned quantum = 32;
double bounded(float value, double lo, double hi, double fallback) noexcept {
    return std::isfinite(value) ? std::clamp(double(value), lo, hi) : fallback;
}
double curve(double x) noexcept {
    return x * x * x * (10 + x * (-15 + 6 * x));
}
} // namespace
Parameters Controls::read() const noexcept {
    return {depth.load(std::memory_order_relaxed), seconds.load(std::memory_order_relaxed),
            volume.load(std::memory_order_relaxed), enabled.load(std::memory_order_relaxed),
            pulse.load(std::memory_order_relaxed)};
}
void Controls::set(const Parameters &p) noexcept {
    depth.store(float(bounded(p.depth, 0, 2, 0.8)), std::memory_order_relaxed);
    seconds.store(float(bounded(p.seconds, 0.5, 16, 6)), std::memory_order_relaxed);
    volume.store(float(bounded(p.volume, 0, 1, 0.6)), std::memory_order_relaxed);
    enabled.store(p.enabled, std::memory_order_relaxed);
    pulse.store(p.pulse, std::memory_order_relaxed);
}
Parameters preset(unsigned index) noexcept {
    if (index == 1)
        return {1.4f, 10, 0.6f, true, false};
    if (index == 2)
        return {0.5f, 1.5f, 0.6f, true, true};
    return {};
}
double Engine::random(Band &b) noexcept {
    auto x = b.random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b.random = x;
    return double(x) / 4294967296.0;
}
Engine::Coefficients Engine::coefficients(double frequency, double gain, double rate) noexcept {
    const double w = 2 * pi * std::min(frequency, rate * 0.43) / rate;
    const double A = std::pow(10.0, gain / 40), alpha = std::sin(w) / (2 * 0.75),
                 c = -2 * std::cos(w), a0 = 1 + alpha / A;
    return {(1 + alpha * A) / a0, c / a0, (1 - alpha * A) / a0, c / a0, (1 - alpha / A) / a0};
}
void Engine::prepare(double rate, const Parameters &p, std::uint32_t seed) {
    if (!std::isfinite(rate) || rate < 8000 || rate > 192000)
        throw std::invalid_argument("Unsupported sample rate");
    sampleRate_ = rate;
    depth_ = bounded(p.depth, 0, 2, 0.8);
    volume_ = bounded(p.volume, 0, 1, 0.6);
    wet_ = p.enabled ? 1 : 0;
    smooth_ = 1 - std::exp(-1 / (0.03 * rate));
    crossfade_ = 1 - std::exp(-1 / (0.025 * rate));
    limiterRelease_ = 1 - std::exp(-1 / (0.1 * rate));
    limiter_ = 1;
    controlFrame_ = 0;
    pulse_ = p.pulse;
    for (std::size_t i = 0; i < bandCount; ++i) {
        auto &b = bands_[i];
        b = Band{};
        b.random = (seed + std::uint32_t(i) * 1009u) | 1u;
        b.target = 2 * random(b) - 1;
        b.duration = 0.65 + 0.7 * random(b);
    }
}
void Engine::process(float *samples, std::size_t frames, const Parameters &p) noexcept {
    if (!samples)
        return;
    const double wantedDepth = bounded(p.depth, 0, 2, 0.8),
                 seconds = bounded(p.seconds, 0.5, 16, 6),
                 wantedVolume = bounded(p.volume, 0, 1, 0.6);
    if (p.pulse != pulse_) {
        pulse_ = p.pulse;
        for (auto &b : bands_) {
            b.from = b.value;
            b.phase = 0;
        }
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        depth_ += smooth_ * (wantedDepth - depth_);
        volume_ += smooth_ * (wantedVolume - volume_);
        wet_ += crossfade_ * ((p.enabled ? 1.0 : 0.0) - wet_);
        for (std::size_t i = 0; i < bandCount; ++i) {
            auto &b = bands_[i];
            b.phase += 1 / (sampleRate_ * seconds * (pulse_ ? 1 : b.duration));
            if (b.phase >= 1) {
                b.phase -= 1;
                b.from = pulse_ ? 0 : b.target;
                b.target = 2 * random(b) - 1;
                b.duration = 0.65 + 0.7 * random(b);
            }
            const double target = pulse_ ? b.target * std::pow(std::sin(pi * b.phase), 2)
                                         : b.from + (b.target - b.from) * curve(b.phase);
            b.value += smooth_ * (target - b.value);
            if (controlFrame_ == 0) {
                const auto c = coefficients(frequencies[i], b.value * depth_, sampleRate_);
                b.step = {(c.b0 - b.current.b0) / quantum, (c.b1 - b.current.b1) / quantum,
                          (c.b2 - b.current.b2) / quantum, (c.a1 - b.current.a1) / quantum,
                          (c.a2 - b.current.a2) / quantum};
            }
            b.current.b0 += b.step.b0;
            b.current.b1 += b.step.b1;
            b.current.b2 += b.step.b2;
            b.current.a1 += b.step.a1;
            b.current.a2 += b.step.a2;
        }
        double out[2]{};
        for (unsigned ch = 0; ch < 2; ++ch) {
            const float raw = samples[frame * 2 + ch];
            const double dry =
                std::isfinite(raw) ? std::clamp(double(raw), -16.0, 16.0) * headroom : 0;
            double x = dry;
            for (auto &b : bands_) {
                const auto &c = b.current;
                const double y = c.b0 * x + b.z1[ch];
                b.z1[ch] = c.b1 * x - c.a1 * y + b.z2[ch];
                b.z2[ch] = c.b2 * x - c.a2 * y;
                if (!std::isfinite(y)) {
                    b.z1[ch] = b.z2[ch] = 0;
                    x = 0;
                } else
                    x = y;
            }
            out[ch] = (dry + (x - dry) * wet_) * volume_;
        }
        const double peak = std::max(std::abs(out[0]), std::abs(out[1]));
        const double needed = peak > 0.98 ? 0.98 / peak : 1;
        // Linked sample-peak guard: immediate attenuation, gradual release.
        // This is not an oversampled true-peak limiter.
        if (needed < limiter_)
            limiter_ = needed;
        else
            limiter_ += limiterRelease_ * (needed - limiter_);
        samples[frame * 2] = float(out[0] * limiter_);
        samples[frame * 2 + 1] = float(out[1] * limiter_);
        controlFrame_ = (controlFrame_ + 1) % quantum;
    }
    for (auto &b : bands_)
        for (unsigned ch = 0; ch < 2; ++ch) {
            if (std::abs(b.z1[ch]) < 1e-24)
                b.z1[ch] = 0;
            if (std::abs(b.z2[ch]) < 1e-24)
                b.z2[ch] = 0;
        }
}
std::array<double, bandCount> Engine::gains() const noexcept {
    std::array<double, bandCount> values{};
    for (std::size_t i = 0; i < bandCount; ++i)
        values[i] = bands_[i].value * depth_;
    return values;
}
} // namespace drift
