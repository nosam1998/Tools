#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioenginebaseapo.h>
#include <propsys.h>
#include <propvarutil.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "Ids.h"
#include "Processor.h"
using Microsoft::WRL::ComPtr;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void check(HRESULT hr, const char *message) {
    require(SUCCEEDED(hr), message);
}
using GetClass = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
using CanUnload = HRESULT(WINAPI *)();

// A format provider implementing the SDK contract keeps the harness independent
// of the optional Visual Studio ATL package. The tested APO is the real DLL.
class MediaType final : public IAudioMediaType {
    std::atomic<ULONG> refs_{1};
    UNCOMPRESSEDAUDIOFORMAT format_;
    WAVEFORMATEXTENSIBLE wave_{};

  public:
    explicit MediaType(const UNCOMPRESSEDAUDIOFORMAT &f) : format_(f) {
        wave_.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wave_.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wave_.Format.nChannels = WORD(f.dwSamplesPerFrame);
        wave_.Format.nSamplesPerSec = DWORD(f.fFramesPerSecond);
        wave_.Format.wBitsPerSample = WORD(f.dwBytesPerSampleContainer * 8);
        wave_.Format.nBlockAlign = WORD(f.dwSamplesPerFrame * f.dwBytesPerSampleContainer);
        wave_.Format.nAvgBytesPerSec = wave_.Format.nSamplesPerSec * wave_.Format.nBlockAlign;
        wave_.Samples.wValidBitsPerSample = WORD(f.dwValidBitsPerSample);
        wave_.dwChannelMask = f.dwChannelMask;
        wave_.SubFormat = f.guidFormatType;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IAudioMediaType))
            return E_NOINTERFACE;
        *out = static_cast<IAudioMediaType *>(this);
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
    HRESULT STDMETHODCALLTYPE IsCompressedFormat(BOOL *compressed) override {
        if (!compressed)
            return E_POINTER;
        *compressed = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsEqual(IAudioMediaType *other, DWORD *flags) override {
        if (!other || !flags)
            return E_POINTER;
        *flags = 0;
        const auto *f = other->GetAudioFormat();
        return f && f->cbSize == wave_.Format.cbSize && std::memcmp(f, &wave_, sizeof(wave_)) == 0
                   ? S_OK
                   : S_FALSE;
    }
    const WAVEFORMATEX *STDMETHODCALLTYPE GetAudioFormat() override {
        return &wave_.Format;
    }
    HRESULT STDMETHODCALLTYPE GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT *f) override {
        if (!f)
            return E_POINTER;
        *f = format_;
        return S_OK;
    }
};
HRESULT makeMediaType(const UNCOMPRESSEDAUDIOFORMAT *f, IAudioMediaType **out) {
    *out = new MediaType(*f);
    return S_OK;
}
GetClass getClass;
struct Instance {
    ComPtr<IAudioProcessingObject> apo;
    ComPtr<IAudioProcessingObjectRT> rt;
    ComPtr<IAudioProcessingObjectConfiguration> config;
    ComPtr<IAudioMediaType> media;
    Instance(unsigned channels, double rate, bool enabled) {
        ComPtr<IClassFactory> factory;
        check(getClass(drift::apo::classId, IID_PPV_ARGS(&factory)), "class factory");
        check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&apo)), "create APO");
        check(apo.As(&rt), "RT interface");
        check(apo.As(&config), "configuration interface");
        require(config->LockForProcess(0, nullptr, 0, nullptr) == APOERR_NOT_INITIALIZED,
                "lock before initialization");
        ComPtr<IPropertyStore> properties;
        check(PSCreateMemoryPropertyStore(IID_PPV_ARGS(&properties)), "memory properties");
        drift::Parameters p{1.4f, 0.8f, 0.8f, enabled, false};
        PROPVARIANT v{};
        v.vt = VT_UI4;
        v.ulVal = drift::apo::pack(p);
        check(properties->SetValue(drift::apo::initialSettingsKey, v), "set initial settings");
        APOInitSystemEffects2 init{};
        init.APOInit.cbSize = sizeof(init);
        init.APOInit.clsid = drift::apo::classId;
        init.pAPOSystemEffectsProperties = properties.Get();
        check(apo->Initialize(sizeof(init), reinterpret_cast<BYTE *>(&init)), "initialize APO");
        require(apo->Initialize(0, nullptr) == APOERR_ALREADY_INITIALIZED, "double initialization");
        UNCOMPRESSEDAUDIOFORMAT f{};
        f.guidFormatType = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        f.dwSamplesPerFrame = channels;
        f.dwBytesPerSampleContainer = 4;
        f.dwValidBitsPerSample = 32;
        f.fFramesPerSecond = rate;
        check(makeMediaType(&f, &media), "create format");
        check(apo->IsInputFormatSupported(nullptr, media.Get(), nullptr), "input format");
        check(apo->IsOutputFormatSupported(media.Get(), media.Get(), nullptr), "output format");
        f.dwSamplesPerFrame = channels == 2 ? 1 : 2;
        ComPtr<IAudioMediaType> other;
        check(makeMediaType(&f, &other), "other format");
        require(FAILED(apo->IsInputFormatSupported(other.Get(), media.Get(), nullptr)),
                "reject channel mismatch");
        f.dwSamplesPerFrame = channels;
        f.guidFormatType = KSDATAFORMAT_SUBTYPE_PCM;
        f.dwBytesPerSampleContainer = 2;
        f.dwValidBitsPerSample = 16;
        other.Reset();
        check(makeMediaType(&f, &other), "PCM format");
        require(FAILED(apo->IsInputFormatSupported(nullptr, other.Get(), nullptr)),
                "reject non-float format");
        APO_REG_PROPERTIES *reg = nullptr;
        check(apo->GetRegistrationProperties(&reg), "registration");
        require(reg && reg->clsid == drift::apo::classId && (reg->Flags & APO_FLAG_INPLACE),
                "registration data");
        CoTaskMemFree(reg);
        HNSTIME latency = -1;
        check(apo->GetLatency(&latency), "latency");
        require(latency == 0, "no buffering latency");
        ComPtr<IAudioSystemEffects2> effects;
        check(apo.As(&effects), "system effects");
        GUID *ids = nullptr;
        UINT count = 0;
        check(effects->GetEffectsList(&ids, &count, nullptr), "effect list");
        require(count == 1 && ids && ids[0] == drift::apo::classId, "effect identity");
        CoTaskMemFree(ids);
        APO_CONNECTION_DESCRIPTOR in{APO_CONNECTION_BUFFER_TYPE_EXTERNAL, 0, 256, media.Get(),
                                     APO_CONNECTION_DESCRIPTOR_SIGNATURE};
        auto out = in;
        auto *ip = &in;
        auto *op = &out;
        require(config->LockForProcess(0, &ip, 1, &op) == APOERR_NUM_CONNECTIONS_INVALID,
                "connection count validation");
        out.u32MaxFrameCount = 255;
        require(config->LockForProcess(1, &ip, 1, &op) == APOERR_INVALID_OUTPUT_MAXFRAMECOUNT,
                "output capacity validation");
        out.u32MaxFrameCount = 256;
        check(config->LockForProcess(1, &ip, 1, &op), "lock formats");
        require(config->LockForProcess(1, &ip, 1, &op) == APOERR_APO_LOCKED, "double lock");
        require(apo->Reset() == APOERR_APO_LOCKED, "reset while processing forbidden");
        UINT n = 0;
        check(apo->GetInputChannelCount(&n), "channel count");
        require(n == channels, "negotiated channel count");
        require(rt->CalcInputFrames(79) == 79 && rt->CalcOutputFrames(79) == 79,
                "one-to-one frames");
    }
    ~Instance() {
        if (config)
            config->UnlockForProcess();
    }
    void process(float *in, float *out, UINT frames, APO_BUFFER_FLAGS flag = BUFFER_VALID,
                 bool aliasProperty = false) {
        APO_CONNECTION_PROPERTY input{reinterpret_cast<UINT_PTR>(in), frames, flag,
                                      APO_CONNECTION_PROPERTY_SIGNATURE};
        APO_CONNECTION_PROPERTY output{reinterpret_cast<UINT_PTR>(out), 0, BUFFER_INVALID,
                                       APO_CONNECTION_PROPERTY_SIGNATURE};
        auto *ip = &input;
        auto *op = aliasProperty ? &input : &output;
        rt->APOProcess(1, &ip, 1, &op);
        require(op->u32ValidFrameCount == frames && op->u32BufferFlags == BUFFER_VALID,
                "output metadata");
    }
};
void processing(unsigned channels, double rate) {
    Instance separate(channels, rate, true), inplace(channels, rate, true),
        bypass(channels, rate, false);
    const unsigned frames = 127;
    std::vector<float> source(frames * channels), copy(source.size() + 8, 123),
        output(source.size() + 8, 123), dry(source.size());
    double changed = 0;
    for (unsigned block = 0; block < 45; ++block) {
        for (unsigned i = 0; i < frames; ++i)
            for (unsigned c = 0; c < channels; ++c)
                source[i * channels + c] = 0.5f * float(std::sin((i + block * frames) * 0.071));
        std::memcpy(copy.data() + 4, source.data(), source.size() * sizeof(float));
        separate.process(source.data(), output.data() + 4, frames);
        inplace.process(copy.data() + 4, copy.data() + 4, frames, BUFFER_VALID, true);
        bypass.process(source.data(), dry.data(), frames);
        require(std::memcmp(source.data(), dry.data(), source.size() * sizeof(float)) == 0,
                "disabled bit-exact passthrough");
        for (std::size_t i = 0; i < source.size(); ++i) {
            require(std::isfinite(output[i + 4]) && std::abs(output[i + 4]) <= 0.501f,
                    "finite bounded output");
            require(output[i + 4] == copy[i + 4], "in-place/out-of-place match");
            require(output[i + 4] == output[(i / channels) * channels + 4],
                    "linked channel modulation");
            changed += std::abs(output[i + 4] - source[i]);
        }
        require(output[0] == 123 && output.back() == 123 && copy[0] == 123 && copy.back() == 123,
                "buffer guards");
    }
    require(changed > 1, "enabled DSP actually changes audio");
    // BUFFER_SILENT may point at uninitialized memory, or have no data pointer.
    for (unsigned i = 0; i < 24; ++i)
        separate.process(nullptr, output.data() + 4, frames, BUFFER_SILENT);
    for (std::size_t i = 0; i < source.size(); ++i)
        require(std::abs(output[i + 4]) < 1e-10f, "silent input and tail decay");
    APO_CONNECTION_PROPERTY bad{reinterpret_cast<UINT_PTR>(source.data()), 257, BUFFER_VALID,
                                APO_CONNECTION_PROPERTY_SIGNATURE};
    APO_CONNECTION_PROPERTY dest{reinterpret_cast<UINT_PTR>(output.data() + 4), 0, BUFFER_VALID,
                                 APO_CONNECTION_PROPERTY_SIGNATURE};
    auto *ip = &bad;
    auto *op = &dest;
    separate.rt->APOProcess(1, &ip, 1, &op);
    require(dest.u32BufferFlags == BUFFER_INVALID && dest.u32ValidFrameCount == 0,
            "oversized buffer rejected");
    bad.u32ValidFrameCount = frames;
    bad.u32BufferFlags = BUFFER_INVALID;
    separate.rt->APOProcess(1, &ip, 1, &op);
    require(dest.u32BufferFlags == BUFFER_INVALID && dest.u32ValidFrameCount == 0,
            "invalid input propagated");
}
void controls() {
    auto p = drift::preset(1);
    p.enabled = false;
    auto q = drift::apo::unpack(drift::apo::pack(p));
    require(q.depth == p.depth && q.seconds == p.seconds && q.volume == p.volume && !q.enabled,
            "settings round trip");
    require(!drift::apo::unpack(0).enabled && !drift::apo::unpack(0xffffffff).enabled,
            "unknown settings bypass");
    for (unsigned word : {0xd310001fu, 0xd3100000u, 0xd31fe000u})
        require(!drift::apo::unpack(word).enabled, "invalid fields bypass");
    drift::apo::Processor processor;
    p.enabled = true;
    processor.prepare(48000, 2, p);
    std::vector<float> source(12000, 0.4f), output(source.size());
    processor.process(source.data(), output.data(), 6000, false, p);
    p.enabled = false;
    processor.process(source.data(), output.data(), 6000, false, p);
    float maxStep = 0;
    for (std::size_t i = 2; i < output.size(); i += 2)
        maxStep = std::max(maxStep, std::abs(output[i] - output[i - 2]));
    require(maxStep < 0.001f, "smooth enable/disable transition");
    processor.process(source.data(), output.data(), 6000, false, p);
    require(std::memcmp(source.data(), output.data(), source.size() * sizeof(float)) == 0,
            "full passthrough after disable");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    HMODULE dll = nullptr;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        require(argc == 2, "DLL path required");
        dll = LoadLibraryExW(argv[1], nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        require(dll != nullptr, "load DLL without registering or installing it");
        getClass = reinterpret_cast<GetClass>(GetProcAddress(dll, "DllGetClassObject"));
        auto canUnload = reinterpret_cast<CanUnload>(GetProcAddress(dll, "DllCanUnloadNow"));
        require(getClass && canUnload, "COM exports");
        require(canUnload() == S_OK, "initial unload state");
        void *out = reinterpret_cast<void *>(1);
        require(getClass(GUID_NULL, __uuidof(IClassFactory), &out) == CLASS_E_CLASSNOTAVAILABLE &&
                    out == nullptr,
                "unknown class rejection");
        for (unsigned channels : {1u, 2u, 6u, 8u, 32u})
            processing(channels, 48000);
        processing(2, 16000);
        processing(2, 96000);
        controls();
        {
            ComPtr<IClassFactory> factory;
            check(getClass(drift::apo::classId, IID_PPV_ARGS(&factory)), "factory lifecycle");
            check(factory->LockServer(TRUE), "server lock");
            require(canUnload() == S_FALSE, "locked module remains loaded");
            check(factory->LockServer(FALSE), "server unlock");
            ComPtr<IAudioProcessingObject> defaultApo;
            check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&defaultApo)), "default APO");
            require(defaultApo->Initialize(12, nullptr) == E_INVALIDARG,
                    "initialization pointer validation");
            check(defaultApo->Initialize(0, nullptr), "standalone initialization");
            ComPtr<IUnknown> identity;
            ComPtr<IAudioProcessingObjectRT> rt;
            check(defaultApo.As(&identity), "IUnknown identity");
            check(defaultApo.As(&rt), "RT identity");
            ComPtr<IUnknown> rtIdentity;
            check(rt.As(&rtIdentity), "RT IUnknown identity");
            require(identity.Get() == rtIdentity.Get(), "COM identity across interfaces");
        }
        require(canUnload() == S_OK, "all COM objects released");
        FreeLibrary(dll);
        CoUninitialize();
        std::cout << "PASS: real APO DLL activation, COM lifecycle, formats, buffer contracts, "
                     "mono/stereo/surround, silence, bypass, ramps, and settings\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << " (Windows error " << GetLastError() << ")\n";
        if (dll)
            FreeLibrary(dll);
        CoUninitialize();
        return 1;
    }
}
