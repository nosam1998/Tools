#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioenginebaseapo.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include "Ids.h"
#include "Processor.h"
#include "Settings.h"

namespace {
using namespace drift::apo;
std::atomic<long> instances{0}, serverLocks{0};
HMODULE module = nullptr;
APO_REG_PROPERTIES registration() {
    APO_REG_PROPERTIES r{};
    r.clsid = classId;
    r.Flags = APO_FLAG(APO_FLAG_DEFAULT | APO_FLAG_INPLACE);
    wcscpy_s(r.szFriendlyName, L"Drift EQ endpoint effect");
    wcscpy_s(r.szCopyrightInfo, L"Drift EQ contributors. Apache-2.0.");
    r.u32MajorVersion = 0;
    r.u32MinorVersion = 3;
    r.u32MinInputConnections = r.u32MaxInputConnections = 1;
    r.u32MinOutputConnections = r.u32MaxOutputConnections = 1;
    r.u32MaxInstances = 0xffffffff;
    r.u32NumAPOInterfaces = 1;
    r.iidAPOInterfaceList[0] = __uuidof(IAudioProcessingObject);
    return r;
}
bool format(IAudioMediaType *type, UNCOMPRESSEDAUDIOFORMAT &f) {
    return type && SUCCEEDED(type->GetUncompressedAudioFormat(&f)) &&
           IsEqualGUID(f.guidFormatType, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) &&
           f.dwBytesPerSampleContainer == 4 && f.dwValidBitsPerSample == 32 &&
           f.dwSamplesPerFrame >= 1 && f.dwSamplesPerFrame <= 32 &&
           std::isfinite(f.fFramesPerSecond) && f.fFramesPerSecond >= 8000 &&
           f.fFramesPerSecond <= 192000;
}
bool same(const UNCOMPRESSEDAUDIOFORMAT &a, const UNCOMPRESSEDAUDIOFORMAT &b) {
    return a.dwSamplesPerFrame == b.dwSamplesPerFrame && a.fFramesPerSecond == b.fFramesPerSecond &&
           a.dwChannelMask == b.dwChannelMask;
}
class Apo final : public IAudioProcessingObject,
                  public IAudioProcessingObjectRT,
                  public IAudioProcessingObjectConfiguration,
                  public IAudioSystemEffects2 {
    std::atomic<ULONG> refs_{1};
    bool initialized_ = false, locked_ = false, discovery_ = false;
    UINT32 channels_ = 0, maxFrames_ = 0;
    double rate_ = 48000;
    SettingsWatcher settings_;
    Processor processor_;

  public:
    Apo() {
        ++instances;
    }
    ~Apo() {
        settings_.stop();
        --instances;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioProcessingObject))
            *out = static_cast<IAudioProcessingObject *>(this);
        else if (iid == __uuidof(IAudioProcessingObjectRT))
            *out = static_cast<IAudioProcessingObjectRT *>(this);
        else if (iid == __uuidof(IAudioProcessingObjectConfiguration))
            *out = static_cast<IAudioProcessingObjectConfiguration *>(this);
        else if (iid == __uuidof(IAudioSystemEffects) || iid == __uuidof(IAudioSystemEffects2))
            *out = static_cast<IAudioSystemEffects2 *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto refs = --refs_;
        if (!refs)
            delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE Initialize(UINT32 bytes, BYTE *data) override {
        if (initialized_)
            return APOERR_ALREADY_INITIALIZED;
        if ((bytes == 0) != (data == nullptr))
            return E_INVALIDARG;
        if (bytes && bytes != sizeof(APOInitSystemEffects) &&
            bytes != sizeof(APOInitSystemEffects2))
            return E_INVALIDARG;
        try {
            if (data) {
                const auto &init = *reinterpret_cast<APOInitSystemEffects *>(data);
                if (init.APOInit.cbSize != bytes || init.APOInit.clsid != classId)
                    return E_INVALIDARG;
                if (bytes == sizeof(APOInitSystemEffects2))
                    discovery_ = reinterpret_cast<APOInitSystemEffects2 *>(data)
                                     ->InitializeForDiscoveryOnly != FALSE;
                if (init.pAPOSystemEffectsProperties) {
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    if (SUCCEEDED(
                            init.pAPOSystemEffectsProperties->GetValue(initialSettingsKey, &v)) &&
                        v.vt == VT_UI4)
                        settings_.word = v.ulVal;
                    PropVariantClear(&v);
                }
                if (!discovery_ && init.pAPOEndpointProperties) {
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    std::wstring guid;
                    if (SUCCEEDED(
                            init.pAPOEndpointProperties->GetValue(PKEY_AudioEndpoint_GUID, &v)) &&
                        v.vt == VT_LPWSTR)
                        guid = normalizeGuid(v.pwszVal);
                    PropVariantClear(&v);
                    if (!guid.empty())
                        settings_.start(guid);
                }
            }
            initialized_ = true;
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }
    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES **out) override {
        if (!out)
            return E_POINTER;
        *out = static_cast<APO_REG_PROPERTIES *>(CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES)));
        if (!*out)
            return E_OUTOFMEMORY;
        **out = registration();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME *latency) override {
        if (!latency)
            return E_POINTER;
        *latency = 0;
        return S_OK; // no buffering or lookahead; EQ has frequency-dependent phase
    }
    HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32 *count) override {
        if (!count)
            return E_POINTER;
        *count = channels_;
        return channels_ ? S_OK : APOERR_NOT_INITIALIZED;
    }
    HRESULT supported(IAudioMediaType *opposite, IAudioMediaType *requested,
                      IAudioMediaType **out) {
        if (out)
            *out = nullptr;
        if (!requested)
            return E_POINTER;
        UNCOMPRESSEDAUDIOFORMAT a{}, b{};
        if (!format(requested, a) || (opposite && (!format(opposite, b) || !same(a, b))))
            return APOERR_FORMAT_NOT_SUPPORTED;
        if (out) {
            requested->AddRef();
            *out = requested;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType *opposite,
                                                     IAudioMediaType *requested,
                                                     IAudioMediaType **out) override {
        return supported(opposite, requested, out);
    }
    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType *opposite,
                                                      IAudioMediaType *requested,
                                                      IAudioMediaType **out) override {
        return supported(opposite, requested, out);
    }
    HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 ni, APO_CONNECTION_DESCRIPTOR **in, UINT32 no,
                                             APO_CONNECTION_DESCRIPTOR **out) override {
        if (!initialized_)
            return APOERR_NOT_INITIALIZED;
        if (locked_)
            return APOERR_APO_LOCKED;
        if (discovery_)
            return E_INVALIDARG;
        if (ni != 1 || no != 1)
            return APOERR_NUM_CONNECTIONS_INVALID;
        if (!in || !out || !in[0] || !out[0])
            return E_POINTER;
        if (in[0]->u32Signature != APO_CONNECTION_DESCRIPTOR_SIGNATURE ||
            out[0]->u32Signature != APO_CONNECTION_DESCRIPTOR_SIGNATURE)
            return E_INVALIDARG;
        if (!in[0]->u32MaxFrameCount || out[0]->u32MaxFrameCount < in[0]->u32MaxFrameCount)
            return APOERR_INVALID_OUTPUT_MAXFRAMECOUNT;
        UNCOMPRESSEDAUDIOFORMAT a{}, b{};
        if (!format(in[0]->pFormat, a) || !format(out[0]->pFormat, b) || !same(a, b))
            return APOERR_INVALID_CONNECTION_FORMAT;
        // Float frames are at most 128 bytes; bound arithmetic even for a malformed host.
        if (in[0]->u32MaxFrameCount > 1048576)
            return E_INVALIDARG;
        try {
            channels_ = a.dwSamplesPerFrame;
            rate_ = a.fFramesPerSecond;
            maxFrames_ = in[0]->u32MaxFrameCount;
            processor_.prepare(rate_, channels_, unpack(settings_.word.load()));
            locked_ = true;
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }
    HRESULT STDMETHODCALLTYPE UnlockForProcess() override {
        if (!locked_)
            return APOERR_ALREADY_UNLOCKED;
        locked_ = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        if (locked_)
            return APOERR_APO_LOCKED;
        if (!initialized_)
            return APOERR_NOT_INITIALIZED;
        try {
            if (channels_)
                processor_.prepare(rate_, channels_, unpack(settings_.word.load()));
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }
    void STDMETHODCALLTYPE APOProcess(UINT32 ni, APO_CONNECTION_PROPERTY **in, UINT32 no,
                                      APO_CONNECTION_PROPERTY **out) override {
        if (no != 1 || !out || !out[0])
            return;
        auto &dest = *out[0];
        // Copy metadata before modifying it: connections may alias in-place.
        const bool valid = ni == 1 && in && in[0];
        const UINT32 frames = valid ? in[0]->u32ValidFrameCount : 0;
        const auto flags = valid ? in[0]->u32BufferFlags : BUFFER_INVALID;
        const auto source = valid ? in[0]->pBuffer : 0;
        const auto signature = valid ? in[0]->u32Signature : 0;
        dest.u32BufferFlags = BUFFER_INVALID;
        dest.u32ValidFrameCount = 0;
        if (!locked_ || !valid || frames > maxFrames_ || !dest.pBuffer ||
            signature != APO_CONNECTION_PROPERTY_SIGNATURE ||
            dest.u32Signature != APO_CONNECTION_PROPERTY_SIGNATURE ||
            (flags != BUFFER_VALID && flags != BUFFER_SILENT) || (flags == BUFFER_VALID && !source))
            return;
        processor_.process(reinterpret_cast<const float *>(source),
                           reinterpret_cast<float *>(dest.pBuffer), frames, flags == BUFFER_SILENT,
                           unpack(settings_.word.load(std::memory_order_relaxed)));
        dest.u32ValidFrameCount = frames;
        dest.u32BufferFlags = BUFFER_VALID; // filtered silence can have an IIR tail
    }
    UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 frames) override {
        return frames;
    }
    UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 frames) override {
        return frames;
    }
    HRESULT STDMETHODCALLTYPE GetEffectsList(GUID **ids, UINT *count, HANDLE) override {
        if (!ids || !count)
            return E_POINTER;
        *ids = static_cast<GUID *>(CoTaskMemAlloc(sizeof(GUID)));
        *count = 0;
        if (!*ids)
            return E_OUTOFMEMORY;
        **ids = classId;
        *count = 1;
        return S_OK; // capability remains present while the user's effect is bypassed
    }
};
class Factory final : public IClassFactory {
    std::atomic<ULONG> refs_{1};

  public:
    Factory() {
        ++instances;
    }
    ~Factory() {
        --instances;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IClassFactory))
            return E_NOINTERFACE;
        *out = static_cast<IClassFactory *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto *apo = new (std::nothrow) Apo;
        if (!apo)
            return E_OUTOFMEMORY;
        const auto hr = apo->QueryInterface(iid, out);
        apo->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock)
            ++serverLocks;
        else if (serverLocks > 0)
            --serverLocks;
        return S_OK;
    }
};
} // namespace
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (clsid != drift::apo::classId)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto *factory = new (std::nothrow) Factory;
    if (!factory)
        return E_OUTOFMEMORY;
    const auto hr = factory->QueryInterface(iid, out);
    factory->Release();
    return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return instances == 0 && serverLocks == 0 ? S_OK : S_FALSE;
}
extern "C" HRESULT __stdcall DllRegisterServer() {
    wchar_t path[32768];
    if (!GetModuleFileNameW(module, path, 32768))
        return HRESULT_FROM_WIN32(GetLastError());
    const auto keyPath =
        std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + drift::apo::classIdText + L"\\InprocServer32";
    HKEY key = nullptr;
    auto error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr, 0,
                                 KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (error)
        return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<BYTE *>(path),
                           DWORD((wcslen(path) + 1) * sizeof(wchar_t)));
    if (!error)
        error = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                               reinterpret_cast<const BYTE *>(L"Both"), 10);
    RegCloseKey(key);
    if (error)
        return HRESULT_FROM_WIN32(error);
    const auto props = registration();
    const auto hr = RegisterAPO(&props);
    if (FAILED(hr)) {
        const auto clsidPath =
            std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + drift::apo::classIdText;
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidPath.c_str());
    }
    return hr;
}
extern "C" HRESULT __stdcall DllUnregisterServer() {
    const auto hr = UnregisterAPO(drift::apo::classId);
    const auto path = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + drift::apo::classIdText;
    const auto result = RegDeleteTreeW(HKEY_LOCAL_MACHINE, path.c_str());
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        return HRESULT_FROM_WIN32(result);
    return hr;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
