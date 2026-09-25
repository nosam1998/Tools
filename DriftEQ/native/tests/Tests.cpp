#include "AudioQueue.h"
#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
int main() {
    constexpr double pi = 3.14159265358979323846, headroom = 0.251188643150958;
    for (double rate : {16000., 44100., 48000., 96000.})
        for (bool pulse : {false, true}) {
            drift::Parameters p{2, 0.5f, 1, true, pulse};
            drift::Engine engine, reference;
            engine.prepare(rate, p, 19);
            reference.prepare(rate, p, 19);
            std::vector<float> samples(512), original(512);
            double difference = 0;
            for (int block = 0; block < 3000; ++block) {
                for (std::size_t i = 0; i < 256; ++i) {
                    const float value =
                        float(0.4 * std::sin(2 * pi * 400 * (block * 256 + i) / rate));
                    samples[i * 2] = samples[i * 2 + 1] = value;
                }
                original = samples;
                auto other = samples;
                engine.process(samples.data(), 256, p);
                reference.process(other.data(), 256, p);
                for (std::size_t i = 0; i < samples.size(); ++i) {
                    require(std::isfinite(samples[i]), "finite audio");
                    require(std::abs(samples[i]) <= 0.981f, "bounded output");
                    require(samples[i] == other[i], "reproducible seed");
                    difference += std::abs(samples[i] - original[i] * headroom);
                }
                for (std::size_t i = 0; i < 256; ++i)
                    require(samples[i * 2] == samples[i * 2 + 1], "linked stereo");
                for (auto gain : engine.gains())
                    require(std::abs(gain) <= 2.000001, "bounded EQ gain");
            }
            require(difference > 1, "nonzero effect");
            p.depth = 0;
            engine.prepare(rate, p);
            for (std::size_t i = 0; i < samples.size(); ++i)
                samples[i] = float(std::sin(i) * 0.5);
            original = samples;
            engine.process(samples.data(), 256, p);
            for (std::size_t i = 0; i < samples.size(); ++i)
                require(std::abs(samples[i] - original[i] * headroom) < 1e-6,
                        "zero-depth transparency");
            p.depth = 2;
            p.enabled = false;
            engine.prepare(rate, p);
            samples = original;
            engine.process(samples.data(), 256, p);
            for (std::size_t i = 0; i < samples.size(); ++i)
                require(std::abs(samples[i] - original[i] * headroom) < 1e-6,
                        "bypass transparency");
        }
    drift::Engine e;
    drift::Parameters p{2, 0.5f, 1, true, true};
    e.prepare(48000, p);
    std::vector<float> loud(2048, 16);
    e.process(loud.data(), 1024, p);
    for (float x : loud)
        require(std::abs(x) <= 0.981, "sample peak guard");
    loud[0] = std::numeric_limits<float>::quiet_NaN();
    p.depth = std::numeric_limits<float>::infinity();
    e.process(loud.data(), 1024, p);
    for (float x : loud)
        require(std::isfinite(x), "invalid input containment");
    // Block boundaries must not change the modulation trajectory or audio result.
    p = drift::preset(1);
    drift::Engine a, b;
    a.prepare(48000, p);
    b.prepare(48000, p);
    std::vector<float> one(96000, 0.2f), many = one;
    a.process(one.data(), 48000, p);
    for (std::size_t i = 0; i < 48000; i += 64)
        b.process(many.data() + i * 2, std::min(std::size_t(64), 48000 - i), p);
    for (std::size_t i = 0; i < one.size(); ++i)
        require(std::abs(one[i] - many[i]) < 1e-7, "block-size independence");
    // Rapidly changing controls remain finite and do not produce full-scale jumps.
    e.prepare(48000, p);
    float previous = 0;
    double maxJump = 0;
    for (unsigned block = 0; block < 400; ++block) {
        p = drift::preset(block % 3);
        p.enabled = block % 2 == 0;
        p.depth = block % 2 ? 0 : 2;
        std::fill(loud.begin(), loud.end(), 0.3f);
        e.process(loud.data(), 1024, p);
        for (std::size_t i = 0; i < loud.size(); i += 2) {
            maxJump = std::max(maxJump, std::abs(double(loud[i] - previous)));
            previous = loud[i];
        }
    }
    require(maxJump < 0.1, "smooth control changes");
    drift::AudioQueue queue;
    queue.reset(512);
    std::vector<float> in(2048, 0.25f), out(512);
    queue.pull(out.data(), 256);
    for (float x : out)
        require(x == 0, "empty queue silence");
    require(queue.push(in.data(), 1024) == 1024, "queue input");
    queue.pull(out.data(), 256);
    require(out.back() > 0.24, "queue playback");
    for (int i = 0; i < 20; ++i)
        queue.pull(out.data(), 256);
    require(queue.underruns > 0, "underflow counter");
    std::vector<float> full((drift::AudioQueue::capacity + 200) * 2, 0.1f);
    queue.reset();
    require(queue.push(full.data(), drift::AudioQueue::capacity + 200) ==
                drift::AudioQueue::capacity,
            "overflow bounded");
    queue.pull(out.data(), 256);
    require(queue.overruns > 0, "overflow counter");
    queue.reset(512);
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (unsigned i = 0; i < 5000; ++i) {
            queue.push(in.data(), 256);
            std::this_thread::yield();
        }
        done = true;
    });
    while (!done.load()) {
        queue.pull(out.data(), 256);
        for (float x : out)
            require(std::isfinite(x) && std::abs(x) <= 0.251, "concurrent queue");
    }
    producer.join();
    std::cout << "PASS: DSP, presets, stability, bypass, transitions, block sizes, peak guard, and "
                 "audio queue\n";
}
