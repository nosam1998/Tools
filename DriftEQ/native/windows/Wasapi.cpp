#include "Wasapi.h"
#include <algorithm>
#include <audioclient.h>
#include <avrt.h>
#include <cwctype>
#include <mmdeviceapi.h>

// Windows property keys depend on definitions in mmdeviceapi.h.
#include <functiondiscoverykeys_devpkey.h>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
struct AudioError {
    HRESULT code;
    const wchar_t *action;
};
void check(HRESULT hr, const wchar_t *action) {
    if (FAILED(hr))
        throw AudioError{hr, action};
}
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};
struct Handle {
    HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
    operator HANDLE() const {
        return value;
    }
};
struct Scheduler {
    DWORD index = 0;
    HANDLE handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
    ~Scheduler() {
        if (handle)
            AvRevertMmThreadCharacteristics(handle);
    }
};
struct StopClient {
    IAudioClient *client = nullptr;
    ~StopClient() {
        if (client)
            client->Stop();
    }
};
} // namespace
bool cableDevice(const std::wstring &name) {
    std::wstring lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return wchar_t(towlower(c)); });
    return lower.find(L"cable") != std::wstring::npos;
}
std::vector<Device> audioDevices(bool capture) {
    std::vector<Device> result;
    ComScope com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE)
        return result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator))))
        return result;
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(capture ? eCapture : eRender, DEVICE_STATE_ACTIVE,
                                              &devices)))
        return result;
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        ComPtr<IPropertyStore> props;
        LPWSTR id = nullptr;
        if (FAILED(devices->Item(i, &device)) || FAILED(device->GetId(&id)))
            continue;
        std::wstring name = L"Audio device";
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) &&
                value.vt == VT_LPWSTR)
                name = value.pwszVal;
            PropVariantClear(&value);
        }
        result.push_back({id, name});
        CoTaskMemFree(id);
    }
    return result;
}
std::wstring defaultOutputId() {
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    LPWSTR id = nullptr;
    std::wstring result;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&enumerator))) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
        SUCCEEDED(device->GetId(&id))) {
        result = id;
        CoTaskMemFree(id);
    }
    return result;
}
std::wstring WasapiAudio::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}
void WasapiAudio::start(const std::wstring &capture, const std::wstring &output) {
    stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_.clear();
    }
    stopping_ = false;
    starting_ = true;
    thread_ = std::thread(&WasapiAudio::run, this, capture, output);
}
void WasapiAudio::stop() {
    stopping_ = true;
    if (thread_.joinable())
        thread_.join();
    running_ = false;
    starting_ = false;
}
void WasapiAudio::run(std::wstring capture, std::wstring output) noexcept {
    try {
        ComScope com;
        check(com.hr, L"Initialize audio thread");
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               IID_PPV_ARGS(&enumerator)),
              L"Find audio devices");
        ComPtr<IMMDevice> inputDevice, outputDevice;
        check(enumerator->GetDevice(capture.c_str(), &inputDevice), L"Open virtual cable");
        check(enumerator->GetDevice(output.c_str(), &outputDevice), L"Open output device");
        // Do not allow a cable playback endpoint to feed the cable's capture endpoint.
        ComPtr<IPropertyStore> properties;
        check(outputDevice->OpenPropertyStore(STGM_READ, &properties), L"Inspect output device");
        PROPVARIANT name;
        PropVariantInit(&name);
        const auto propertyResult = properties->GetValue(PKEY_Device_FriendlyName, &name);
        const bool feedback =
            SUCCEEDED(propertyResult) && name.vt == VT_LPWSTR && cableDevice(name.pwszVal);
        PropVariantClear(&name);
        if (feedback)
            throw AudioError{E_INVALIDARG,
                             L"Choose physical speakers or headphones, not a virtual cable output"};
        ComPtr<IAudioClient> input, render;
        check(inputDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void **>(input.GetAddressOf())),
              L"Activate virtual cable");
        check(outputDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void **>(render.GetAddressOf())),
              L"Activate output");
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = 8;
        format.nAvgBytesPerSec = 384000;
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        check(input->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &format, nullptr),
              L"Initialize cable at 48 kHz stereo");
        check(render->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &format, nullptr),
              L"Initialize output at 48 kHz stereo");
        Handle inputEvent, outputEvent;
        if (!inputEvent.value || !outputEvent.value)
            throw AudioError{E_OUTOFMEMORY, L"Create audio events"};
        check(input->SetEventHandle(inputEvent), L"Set capture event");
        check(render->SetEventHandle(outputEvent), L"Set output event");
        ComPtr<IAudioCaptureClient> reader;
        ComPtr<IAudioRenderClient> writer;
        check(input->GetService(IID_PPV_ARGS(&reader)), L"Open capture stream");
        check(render->GetService(IID_PPV_ARGS(&writer)), L"Open playback stream");
        UINT32 bufferFrames = 0;
        check(render->GetBufferSize(&bufferFrames), L"Get output buffer");
        std::vector<float> scratch(std::size_t(bufferFrames) * 2);
        queue_.reset(
            std::min(std::size_t(8192), std::max(std::size_t(960), std::size_t(bufferFrames) * 2)));
        engine_.prepare(48000, controls_.read());
        BYTE *initial = nullptr;
        check(writer->GetBuffer(bufferFrames, &initial), L"Prime output");
        check(writer->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT), L"Prime output");
        StopClient stopInput{input.Get()}, stopOutput{render.Get()};
        Scheduler scheduler;
        check(input->Start(), L"Start capture");
        check(render->Start(), L"Start playback");
        running_ = true;
        starting_ = false;
        HANDLE events[] = {inputEvent.value, outputEvent.value};
        while (!stopping_) {
            const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 50);
            if (wait == WAIT_FAILED)
                throw AudioError{HRESULT_FROM_WIN32(GetLastError()), L"Wait for audio"};
            UINT32 packet = 0;
            check(reader->GetNextPacketSize(&packet), L"Read cable (device may have disconnected)");
            while (packet) {
                BYTE *bytes = nullptr;
                UINT32 frames = 0;
                DWORD status = 0;
                check(reader->GetBuffer(&bytes, &frames, &status, nullptr, nullptr),
                      L"Read capture buffer");
                queue_.push((status & AUDCLNT_BUFFERFLAGS_SILENT)
                                ? nullptr
                                : reinterpret_cast<float *>(bytes),
                            frames);
                check(reader->ReleaseBuffer(frames), L"Release capture buffer");
                check(reader->GetNextPacketSize(&packet), L"Read next capture packet");
            }
            UINT32 padding = 0;
            check(render->GetCurrentPadding(&padding),
                  L"Read output buffer (device may have disconnected)");
            if (padding > bufferFrames)
                throw AudioError{E_UNEXPECTED, L"Unexpected output buffer size"};
            const auto available = bufferFrames - padding;
            if (available) {
                queue_.pull(scratch.data(), available);
                engine_.process(scratch.data(), available, controls_.read());
                BYTE *bytes = nullptr;
                check(writer->GetBuffer(available, &bytes), L"Write playback buffer");
                memcpy(bytes, scratch.data(), std::size_t(available) * 8);
                check(writer->ReleaseBuffer(available, 0), L"Release playback buffer");
            }
        }
    } catch (const AudioError &error) {
        std::wostringstream message;
        message
            << error.action << L" (0x" << std::hex << unsigned(error.code)
            << L"). Select your physical output in Windows Sound settings to restore normal audio.";
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = message.str();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = L"Audio could not start. Restore your physical output in Windows Sound settings.";
    }
    running_ = false;
    starting_ = false;
}
