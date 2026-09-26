#pragma once
#include "AudioQueue.h"
#include "Engine.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

struct Device {
    std::wstring id, name;
};
std::vector<Device> audioDevices(bool capture);
bool cableDevice(const std::wstring &name);
std::wstring defaultOutputId();
class WasapiAudio {
  public:
    explicit WasapiAudio(drift::Controls &controls) : controls_(controls) {}
    ~WasapiAudio() {
        stop();
    }
    void start(const std::wstring &capture, const std::wstring &output);
    void stop();
    bool running() const {
        return running_;
    }
    bool starting() const {
        return starting_;
    }
    std::wstring error() const;
    std::uint64_t underruns() const {
        return queue_.underruns.load();
    }
    std::uint64_t overruns() const {
        return queue_.overruns.load();
    }

  private:
    void run(std::wstring capture, std::wstring output) noexcept;
    drift::Controls &controls_;
    drift::Engine engine_;
    drift::AudioQueue queue_;
    std::thread thread_;
    std::atomic<bool> stopping_{false}, running_{false}, starting_{false};
    mutable std::mutex mutex_;
    std::wstring error_;
};
