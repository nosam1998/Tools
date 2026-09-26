#pragma once
#include "Engine.h"
#include <CoreAudio/CoreAudio.h>
#include <memory>
#include <string>
#include <vector>

struct MacDevice {
    AudioObjectID id = 0;
    std::string uid, name;
};
std::vector<MacDevice> macOutputs();
AudioObjectID macDefaultOutput();
class TapAudio {
  public:
    explicit TapAudio(drift::Controls &);
    ~TapAudio();
    bool start(AudioObjectID output, std::string &error);
    void stop();
    bool running() const;
    bool healthy(std::string &reason); // main-thread device/format checks
    std::uint64_t underruns() const;
    std::uint64_t overruns() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
