#include "TapAudio.h"
#include "AudioQueue.h"
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>
#include <array>
#include <stdexcept>
#include <unistd.h>

namespace {
AudioObjectPropertyAddress
address(AudioObjectPropertySelector selector,
        AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return {selector, scope, kAudioObjectPropertyElementMain};
}
void check(OSStatus status, const char *operation) {
    if (status != noErr)
        throw std::runtime_error(std::string(operation) + " (Core Audio " + std::to_string(status) +
                                 ")");
}
template <class T>
T property(AudioObjectID object, AudioObjectPropertySelector selector,
           AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    T value{};
    UInt32 size = sizeof(value);
    auto a = address(selector, scope);
    check(AudioObjectGetPropertyData(object, &a, 0, nullptr, &size, &value), "Read audio property");
    return value;
}
NSString *stringProperty(AudioObjectID object, AudioObjectPropertySelector selector) {
    CFStringRef value = property<CFStringRef>(object, selector);
    return CFBridgingRelease(value);
}
std::string utf8(NSString *text) {
    return text ? std::string(text.UTF8String) : std::string();
}
} // namespace
AudioObjectID macDefaultOutput() {
    try {
        return property<AudioObjectID>(kAudioObjectSystemObject,
                                       kAudioHardwarePropertyDefaultOutputDevice);
    } catch (...) {
        return 0;
    }
}
std::vector<MacDevice> macOutputs() {
    std::vector<MacDevice> result;
    auto a = address(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr)
        return result;
    std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size,
                                   devices.data()) != noErr)
        return result;
    for (const auto id : devices)
        try {
            if (property<UInt32>(id, kAudioDevicePropertyTransportType) ==
                kAudioDeviceTransportTypeAggregate)
                continue;
            auto streams = address(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput);
            UInt32 bytes = 0;
            if (AudioObjectGetPropertyDataSize(id, &streams, 0, nullptr, &bytes) != noErr ||
                bytes == 0)
                continue;
            result.push_back({id, utf8(stringProperty(id, kAudioDevicePropertyDeviceUID)),
                              utf8(stringProperty(id, kAudioObjectPropertyName))});
        } catch (...) {
        }
    return result;
}
struct TapAudio::Impl {
    explicit Impl(drift::Controls &c) : controls(c) {}
    drift::Controls &controls;
    drift::Engine engine;
    drift::AudioQueue queue;
    AudioObjectID tap = 0, aggregate = 0, output = 0;
    AudioDeviceIOProcID io = nullptr;
    AudioUnit unit = nullptr;
    AudioStreamBasicDescription format{};
    double outputRate = 0;
    std::atomic<bool> active{false}, formatFault{false};
    std::atomic<std::uint64_t> captureCalls{0}, renderCalls{0};
    std::uint64_t previousCapture = 0, previousRender = 0;
    unsigned stalled = 0;
    static OSStatus capture(AudioDeviceID, const AudioTimeStamp *, const AudioBufferList *input,
                            const AudioTimeStamp *, AudioBufferList *, const AudioTimeStamp *,
                            void *context) {
        auto &self = *static_cast<Impl *>(context);
        self.captureCalls.fetch_add(1, std::memory_order_relaxed);
        if (!input || input->mNumberBuffers == 0)
            return noErr;
        const bool packed = input->mNumberBuffers == 1 && input->mBuffers[0].mNumberChannels == 2;
        const bool planar = input->mNumberBuffers == 2 && input->mBuffers[0].mNumberChannels == 1 &&
                            input->mBuffers[1].mNumberChannels == 1;
        if (!packed && !planar) {
            self.formatFault = true;
            return noErr;
        }
        if (!input->mBuffers[0].mData || (planar && !input->mBuffers[1].mData))
            return noErr;
        const auto frames = input->mBuffers[0].mDataByteSize / (packed ? 8 : 4);
        if (planar && input->mBuffers[1].mDataByteSize != frames * 4) {
            self.formatFault = true;
            return noErr;
        }
        if (packed)
            self.queue.push(static_cast<const float *>(input->mBuffers[0].mData), frames);
        else {
            std::array<float, 512> scratch{};
            const auto *l = static_cast<const float *>(input->mBuffers[0].mData);
            const auto *r = static_cast<const float *>(input->mBuffers[1].mData);
            for (UInt32 start = 0; start < frames; start += 256) {
                const auto count = std::min(UInt32(256), frames - start);
                for (UInt32 i = 0; i < count; ++i) {
                    scratch[i * 2] = l[start + i];
                    scratch[i * 2 + 1] = r[start + i];
                }
                self.queue.push(scratch.data(), count);
            }
        }
        return noErr;
    }
    static OSStatus render(void *context, AudioUnitRenderActionFlags *, const AudioTimeStamp *,
                           UInt32, UInt32 frames, AudioBufferList *output) {
        auto &self = *static_cast<Impl *>(context);
        self.renderCalls.fetch_add(1, std::memory_order_relaxed);
        if (!output)
            return noErr;
        for (UInt32 i = 0; i < output->mNumberBuffers; ++i)
            if (output->mBuffers[i].mData)
                memset(output->mBuffers[i].mData, 0, output->mBuffers[i].mDataByteSize);
        if (output->mNumberBuffers != 1 || output->mBuffers[0].mNumberChannels != 2 ||
            !output->mBuffers[0].mData || output->mBuffers[0].mDataByteSize < frames * 8) {
            self.formatFault = true;
            return noErr;
        }
        auto *data = static_cast<float *>(output->mBuffers[0].mData);
        self.queue.pull(data, frames);
        self.engine.process(data, frames, self.controls.read());
        return noErr;
    }
    void stop() {
        active = false;
        // Release the muted tap first so source apps recover normal playback.
        if (aggregate && io)
            AudioDeviceStop(aggregate, io);
        if (aggregate && io)
            AudioDeviceDestroyIOProcID(aggregate, io);
        io = nullptr;
        if (aggregate)
            AudioHardwareDestroyAggregateDevice(aggregate);
        aggregate = 0;
        if (tap)
            AudioHardwareDestroyProcessTap(tap);
        tap = 0;
        if (unit) {
            AudioOutputUnitStop(unit);
            AudioUnitUninitialize(unit);
            AudioComponentInstanceDispose(unit);
            unit = nullptr;
        }
        output = 0;
    }
};
TapAudio::TapAudio(drift::Controls &controls) : impl_(std::make_unique<Impl>(controls)) {}
TapAudio::~TapAudio() {
    stop();
}
void TapAudio::stop() {
    impl_->stop();
}
bool TapAudio::running() const {
    return impl_->active;
}
std::uint64_t TapAudio::underruns() const {
    return impl_->queue.underruns.load();
}
std::uint64_t TapAudio::overruns() const {
    return impl_->queue.overruns.load();
}
bool TapAudio::start(AudioObjectID output, std::string &error) {
    stop();
    auto &s = *impl_;
    error.clear();
    try {
        if (!output)
            throw std::runtime_error("Select speakers or headphones first.");
        s.output = output;
        // Create the output unit before resolving this process's audio object ID.
        AudioComponentDescription component{};
        component.componentType = kAudioUnitType_Output;
        component.componentSubType = kAudioUnitSubType_HALOutput;
        component.componentManufacturer = kAudioUnitManufacturer_Apple;
        const auto found = AudioComponentFindNext(nullptr, &component);
        if (!found)
            throw std::runtime_error("Audio output component unavailable.");
        check(AudioComponentInstanceNew(found, &s.unit), "Create output unit");
        check(AudioUnitSetProperty(s.unit, kAudioOutputUnitProperty_CurrentDevice,
                                   kAudioUnitScope_Global, 0, &output, sizeof(output)),
              "Select output device");
        pid_t pid = getpid();
        AudioObjectID ownProcess = 0;
        UInt32 bytes = sizeof(ownProcess);
        auto processAddress = address(kAudioHardwarePropertyTranslatePIDToProcessObject);
        check(AudioObjectGetPropertyData(kAudioObjectSystemObject, &processAddress, sizeof(pid),
                                         &pid, &bytes, &ownProcess),
              "Resolve Drift audio process");
        if (!ownProcess)
            throw std::runtime_error(
                "Could not exclude Drift from capture; routing was not started.");
        CATapDescription *description =
            [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[ @(ownProcess) ]];
        description.name = @"Drift EQ system audio";
        [description setPrivate:YES];
        description.muteBehavior = CATapMutedWhenTapped;
        check(AudioHardwareCreateProcessTap(description, &s.tap),
              "Create system audio tap (allow audio capture in System Settings)");
        s.format = property<AudioStreamBasicDescription>(s.tap, kAudioTapPropertyFormat);
        if (s.format.mFormatID != kAudioFormatLinearPCM ||
            !(s.format.mFormatFlags & kAudioFormatFlagIsFloat) || s.format.mBitsPerChannel != 32 ||
            s.format.mChannelsPerFrame != 2)
            throw std::runtime_error("The system tap did not provide stereo float audio.");
        NSString *tapUID = stringProperty(s.tap, kAudioTapPropertyUID);
        NSDictionary *aggregate = @{
            @kAudioAggregateDeviceNameKey : @"Drift EQ private tap",
            @kAudioAggregateDeviceUIDKey : NSUUID.UUID.UUIDString,
            @kAudioAggregateDeviceIsPrivateKey : @YES,
            @kAudioAggregateDeviceTapAutoStartKey : @YES,
            @kAudioAggregateDeviceTapListKey : @[
                @{@kAudioSubTapUIDKey : tapUID,
                  @kAudioSubTapDriftCompensationKey : @YES}
            ]
        };
        check(AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggregate, &s.aggregate),
              "Create private audio route");
        AudioStreamBasicDescription client{};
        client.mSampleRate = s.format.mSampleRate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
        client.mBytesPerPacket = 8;
        client.mFramesPerPacket = 1;
        client.mBytesPerFrame = 8;
        client.mChannelsPerFrame = 2;
        client.mBitsPerChannel = 32;
        check(AudioUnitSetProperty(s.unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
                                   0, &client, sizeof(client)),
              "Set stereo output format");
        AURenderCallbackStruct callback{Impl::render, &s};
        check(AudioUnitSetProperty(s.unit, kAudioUnitProperty_SetRenderCallback,
                                   kAudioUnitScope_Input, 0, &callback, sizeof(callback)),
              "Set audio callback");
        check(AudioUnitInitialize(s.unit), "Initialize output");
        s.outputRate = property<Float64>(output, kAudioDevicePropertyNominalSampleRate);
        s.engine.prepare(client.mSampleRate, s.controls.read());
        s.queue.reset(std::size_t(client.mSampleRate * 0.04));
        s.formatFault = false;
        s.captureCalls = 0;
        s.renderCalls = 0;
        s.previousCapture = 0;
        s.previousRender = 0;
        s.stalled = 0;
        check(AudioDeviceCreateIOProcID(s.aggregate, Impl::capture, &s, &s.io),
              "Prepare tap capture");
        // Output starts before muting the source applications. Failure unwinds all resources.
        check(AudioOutputUnitStart(s.unit), "Start output");
        check(AudioDeviceStart(s.aggregate, s.io), "Start system capture");
        s.active = true;
        return true;
    } catch (const std::exception &exception) {
        error = exception.what();
        s.stop();
        return false;
    }
}
bool TapAudio::healthy(std::string &reason) {
    auto &s = *impl_;
    if (!s.active)
        return true;
    try {
        if (s.formatFault)
            throw std::runtime_error("Audio format changed unexpectedly.");
        if (!property<UInt32>(s.output, kAudioDevicePropertyDeviceIsAlive))
            throw std::runtime_error("The output device disconnected.");
        if (property<Float64>(s.output, kAudioDevicePropertyNominalSampleRate) != s.outputRate)
            throw std::runtime_error("Output sample rate changed.");
        const auto now = property<AudioStreamBasicDescription>(s.tap, kAudioTapPropertyFormat);
        if (now.mSampleRate != s.format.mSampleRate || now.mFormatFlags != s.format.mFormatFlags ||
            now.mChannelsPerFrame != s.format.mChannelsPerFrame ||
            now.mBitsPerChannel != s.format.mBitsPerChannel)
            throw std::runtime_error("System audio format changed.");
        const auto capture = s.captureCalls.load(), render = s.renderCalls.load();
        s.stalled =
            (capture == s.previousCapture || render == s.previousRender) ? s.stalled + 1 : 0;
        s.previousCapture = capture;
        s.previousRender = render;
        if (s.stalled >= 3)
            throw std::runtime_error(
                "Audio callbacks stopped. Check capture permission and the output device.");
        return true;
    } catch (const std::exception &e) {
        reason = e.what();
        s.stop();
        return false;
    }
}
