#include "Settings.h"

namespace drift::apo {
bool readSettings(const std::wstring &guid, std::uint32_t &word) noexcept {
    try {
        DWORD value = 0, size = sizeof(value);
        const auto path = std::wstring(settingsRoot) + guid;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), L"Parameters",
                         RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &value,
                         &size) != ERROR_SUCCESS)
            return false;
        word = value;
        return true;
    } catch (...) {
        return false;
    }
}
bool writeSettings(const std::wstring &guid, std::uint32_t word) noexcept {
    try {
        if (normalizeGuid(guid.c_str()).empty())
            return false;
        HKEY key = nullptr;
        const auto path = std::wstring(settingsRoot) + guid;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
                          &key) != ERROR_SUCCESS)
            return false;
        const auto result = RegSetValueExW(key, L"Parameters", 0, REG_DWORD,
                                           reinterpret_cast<const BYTE *>(&word), sizeof(word));
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    } catch (...) {
        return false;
    }
}
SettingsWatcher::~SettingsWatcher() {
    stop();
}
void SettingsWatcher::stop() noexcept {
    if (stop_)
        SetEvent(stop_);
    if (thread_.joinable())
        thread_.join();
    if (stop_)
        CloseHandle(stop_);
    stop_ = nullptr;
}
void SettingsWatcher::start(const std::wstring &guid) {
    if (guid.empty() || thread_.joinable())
        return;
    std::uint32_t initial = 0;
    readSettings(guid, initial);
    word = initial;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_)
        return; // retain the current settings if a worker cannot be created
    try {
        thread_ = std::thread([this, guid] {
            // Registry and waiting are isolated from APOProcess. A bounded polling
            // interval also handles key deletion/recreation during installation.
            while (WaitForSingleObject(stop_, 100) == WAIT_TIMEOUT) {
                std::uint32_t next = 0;
                readSettings(guid, next);
                word.store(next, std::memory_order_relaxed);
            }
        });
    } catch (...) {
        CloseHandle(stop_);
        stop_ = nullptr;
    }
}
} // namespace drift::apo
