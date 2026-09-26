#pragma once
#include "Engine.h"
#include "Ids.h"
#include <atomic>
#include <string>
#include <thread>

namespace drift::apo {
// One DWORD is the entire versioned setting. Registry updates and RT snapshots
// therefore cannot observe half of a UI change. Unknown schemas fail to bypass.
inline std::uint32_t pack(const Parameters &value) noexcept {
    Controls sanitized;
    sanitized.set(value);
    const auto p = sanitized.read();
    return 0xd3000000u | std::uint32_t(p.depth * 10 + 0.5f) |
           (std::uint32_t(p.seconds * 10 + 0.5f) << 5) |
           (std::uint32_t(p.volume * 100 + 0.5f) << 13) | (p.enabled ? 1u << 20 : 0) |
           (p.pulse ? 1u << 21 : 0);
}
inline Parameters unpack(std::uint32_t word) noexcept {
    Parameters p;
    p.enabled = false;
    if ((word & 0xffc00000u) != 0xd3000000u || (word & 31) > 20 || ((word >> 5) & 255) < 5 ||
        ((word >> 5) & 255) > 160 || ((word >> 13) & 127) > 100)
        return p;
    p.depth = float(word & 31) / 10;
    p.seconds = float((word >> 5) & 255) / 10;
    p.volume = float((word >> 13) & 127) / 100;
    p.enabled = (word & (1u << 20)) != 0;
    p.pulse = (word & (1u << 21)) != 0;
    return p;
}
inline std::wstring normalizeGuid(const wchar_t *text) {
    GUID id{};
    wchar_t normalized[40]{};
    if (!text || FAILED(CLSIDFromString(text, &id)))
        return {};
    StringFromGUID2(id, normalized, 40);
    return normalized;
}
bool readSettings(const std::wstring &guid, std::uint32_t &word) noexcept;
bool writeSettings(const std::wstring &guid, std::uint32_t word) noexcept;

class SettingsWatcher {
  public:
    std::atomic<std::uint32_t> word{0}; // zero means unmodified audio
    ~SettingsWatcher();
    void stop() noexcept;
    void start(const std::wstring &guid); // outside the audio callback only
  private:
    HANDLE stop_ = nullptr;
    std::thread thread_;
};
} // namespace drift::apo
