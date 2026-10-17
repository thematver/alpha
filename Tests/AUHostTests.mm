#import <Foundation/Foundation.h>
#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

// An independent AUv2 host: no JUCE headers, implementation or test utilities.
namespace {
constexpr double sampleRate = 48000.0;
constexpr UInt32 blockSize = 512;
constexpr UInt32 renderBlocks = 200;

constexpr OSType fourCC(char a, char b, char c, char d)
{
    return (static_cast<UInt32>(a) << 24) | (static_cast<UInt32>(b) << 16)
         | (static_cast<UInt32>(c) << 8) | static_cast<UInt32>(d);
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void checked(OSStatus status, const char* operation)
{
    if (status != noErr) {
        std::ostringstream message;
        message << operation << " (OSStatus " << status << ')';
        throw std::runtime_error(message.str());
    }
}

template <typename T>
struct CFHandle {
    T value = nullptr;
    ~CFHandle() { if (value != nullptr) CFRelease(value); }
    CFHandle() = default;
    CFHandle(const CFHandle&) = delete;
    CFHandle& operator=(const CFHandle&) = delete;
};

struct Unit {
    AudioUnit value = nullptr;
    bool initialized = false;

    explicit Unit(AudioComponent component)
    {
        const auto status = AudioComponentInstanceNew(component, &value);
        if (status != noErr && value != nullptr) {
            AudioComponentInstanceDispose(value);
            value = nullptr;
        }
        checked(status, "instantiate registered AU");
        require(value != nullptr, "instantiate returned a null AU");
    }

    ~Unit()
    {
        if (value != nullptr) {
            if (initialized) AudioUnitUninitialize(value);
            AudioComponentInstanceDispose(value);
        }
    }

    Unit(const Unit&) = delete;
    Unit& operator=(const Unit&) = delete;
};

struct ParameterInfo {
    AudioUnitParameterInfo value{};
    ~ParameterInfo()
    {
        if ((value.flags & kAudioUnitParameterFlag_CFNameRelease) != 0) {
            if ((value.flags & kAudioUnitParameterFlag_HasCFNameString) != 0
                && value.cfNameString != nullptr)
                CFRelease(value.cfNameString);
            if (value.unit == kAudioUnitParameterUnit_CustomUnit && value.unitName != nullptr)
                CFRelease(value.unitName);
        }
    }
};

float inputSample(double sample, UInt32 channel)
{
    const double frequency = channel == 0 ? 220.0 : 437.0;
    return static_cast<float>(0.2 * std::sin(2.0 * std::numbers::pi * frequency * sample / sampleRate));
}

struct Input {
    UInt32 channels = 2;
    std::array<std::array<float, blockSize>, 2> scratch{};
    std::uint64_t calls = 0;
    double nextSample = 0.0;
};

OSStatus supplyInput(void* refcon, AudioUnitRenderActionFlags*, const AudioTimeStamp* time,
                     UInt32 bus, UInt32 frames, AudioBufferList* data)
{
    auto& input = *static_cast<Input*>(refcon);
    if (bus != 0 || data == nullptr || data->mNumberBuffers != input.channels)
        return kAudioUnitErr_InvalidElement;
    if (frames > blockSize)
        return kAudioUnitErr_TooManyFramesToProcess;

    const double start = time != nullptr && (time->mFlags & kAudioTimeStampSampleTimeValid) != 0
                       ? time->mSampleTime : input.nextSample;
    for (UInt32 channel = 0; channel < input.channels; ++channel) {
        auto& buffer = data->mBuffers[channel];
        if (buffer.mNumberChannels != 1)
            return kAudioUnitErr_FormatNotSupported;
        if (buffer.mData == nullptr)
            buffer.mData = input.scratch[channel].data();
        else if (buffer.mDataByteSize < frames * sizeof(float))
            return kAudioUnitErr_InvalidPropertyValue;
        buffer.mDataByteSize = frames * sizeof(float);
        auto* samples = static_cast<float*>(buffer.mData);
        for (UInt32 frame = 0; frame < frames; ++frame)
            samples[frame] = inputSample(start + frame, channel);
    }
    ++input.calls;
    input.nextSample = start + frames;
    return noErr;
}

AudioStreamBasicDescription formatFor(UInt32 channels)
{
    AudioStreamBasicDescription format{};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked)
                        | static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
    format.mBytesPerPacket = sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float);
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 32;
    return format;
}

OSStatus configure(Unit& unit, Input& input)
{
    const auto format = formatFor(input.channels);
    auto status = AudioUnitSetProperty(unit.value, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input, 0, &format, sizeof(format));
    if (status != noErr) return status;
    status = AudioUnitSetProperty(unit.value, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Output, 0, &format, sizeof(format));
    if (status != noErr) return status;
    const UInt32 maximumFrames = blockSize;
    status = AudioUnitSetProperty(unit.value, kAudioUnitProperty_MaximumFramesPerSlice,
                                  kAudioUnitScope_Global, 0, &maximumFrames, sizeof(maximumFrames));
    if (status != noErr) return status;
    AURenderCallbackStruct callback{ supplyInput, &input };
    status = AudioUnitSetProperty(unit.value, kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, 0, &callback, sizeof(callback));
    if (status != noErr) return status;
    status = AudioUnitInitialize(unit.value);
    unit.initialized = status == noErr;
    return status;
}

void verifyFormat(AudioUnit unit, UInt32 channels)
{
    for (const auto scope : { kAudioUnitScope_Input, kAudioUnitScope_Output }) {
        AudioStreamBasicDescription format{};
        UInt32 size = sizeof(format);
        checked(AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, scope, 0, &format, &size),
                "read negotiated stream format");
        require(size == sizeof(format) && format.mSampleRate == sampleRate
                && format.mFormatID == kAudioFormatLinearPCM && format.mChannelsPerFrame == channels
                && format.mBitsPerChannel == 32 && format.mBytesPerFrame == sizeof(float)
                && (format.mFormatFlags & kAudioFormatFlagIsFloat) != 0
                && (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0,
                "AU changed the requested planar float32 stream format");
    }
    UInt32 maximumFrames = 0;
    UInt32 size = sizeof(maximumFrames);
    checked(AudioUnitGetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                 kAudioUnitScope_Global, 0, &maximumFrames, &size), "read maximum frames");
    require(maximumFrames >= blockSize, "AU maximum frames is smaller than 512");
}

struct RenderStats {
    double peak = 0.0;
    double squaredDifference = 0.0;
    std::uint64_t samples = 0;
    bool exactDry = true;
    double differenceRMS() const { return std::sqrt(squaredDifference / static_cast<double>(samples)); }
};

RenderStats render(AudioUnit unit, Input& input, UInt32 blocks, double startSample)
{
    struct TwoBuffers { UInt32 count; AudioBuffer buffers[2]; } storage{};
    static_assert(offsetof(TwoBuffers, buffers) == offsetof(AudioBufferList, mBuffers));
    auto* buffers = reinterpret_cast<AudioBufferList*>(&storage);
    std::array<std::array<float, blockSize>, 2> output{};
    RenderStats stats;
    const auto callsBefore = input.calls;

    for (UInt32 block = 0; block < blocks; ++block) {
        storage.count = input.channels;
        for (UInt32 channel = 0; channel < input.channels; ++channel) {
            output[channel].fill(std::numeric_limits<float>::quiet_NaN());
            storage.buffers[channel] = { 1, blockSize * sizeof(float), output[channel].data() };
        }
        AudioTimeStamp time{};
        time.mSampleTime = startSample + static_cast<double>(block) * blockSize;
        time.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags = 0;
        checked(AudioUnitRender(unit, &flags, &time, 0, blockSize, buffers), "AudioUnitRender");
        require(buffers->mNumberBuffers == input.channels, "render changed the planar buffer count");
        for (UInt32 channel = 0; channel < input.channels; ++channel) {
            const auto& buffer = buffers->mBuffers[channel];
            require(buffer.mNumberChannels == 1 && buffer.mData != nullptr
                    && buffer.mDataByteSize >= blockSize * sizeof(float), "render returned an invalid output buffer");
            const auto* samples = static_cast<const float*>(buffer.mData);
            for (UInt32 frame = 0; frame < blockSize; ++frame) {
                const float value = samples[frame];
                require(std::isfinite(value), "render produced NaN or infinity");
                const float dry = inputSample(time.mSampleTime + frame, channel);
                stats.peak = std::max(stats.peak, std::abs(static_cast<double>(value)));
                const double difference = static_cast<double>(value) - dry;
                stats.squaredDifference += difference * difference;
                stats.exactDry = stats.exactDry && value == dry;
                ++stats.samples;
            }
        }
    }
    require(input.calls > callsBefore, "AU rendered without requesting the input callback");
    require(stats.peak < 4.0, "render peak exceeded the <4 amplitude bound");
    return stats;
}

std::vector<AudioUnitParameterID> verifyParameters(AudioUnit unit)
{
    UInt32 bytes = 0;
    Boolean writable = false;
    checked(AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList,
                                     kAudioUnitScope_Global, 0, &bytes, &writable), "query parameter list");
    require(bytes % sizeof(AudioUnitParameterID) == 0 && bytes / sizeof(AudioUnitParameterID) == 1445,
            "registered AU must expose exactly 1445 global parameters");
    std::vector<AudioUnitParameterID> ids(bytes / sizeof(AudioUnitParameterID));
    const auto expectedBytes = bytes;
    checked(AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global,
                                 0, ids.data(), &bytes), "read parameter list");
    require(bytes == expectedBytes, "parameter list changed while reading");
    require(std::unordered_set<AudioUnitParameterID>(ids.begin(), ids.end()).size() == ids.size(),
            "AU exposed duplicate parameter IDs");
    for (const auto id : ids) {
        ParameterInfo info;
        bytes = sizeof(info.value);
        checked(AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                                     id, &info.value, &bytes), "read parameter metadata using actual AU ID");
        require(std::isfinite(info.value.minValue) && std::isfinite(info.value.maxValue)
                && info.value.minValue <= info.value.maxValue, "parameter has an invalid range");
        AudioUnitParameterValue value = 0;
        checked(AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &value), "read AU parameter");
        require(std::isfinite(value), "parameter has a non-finite value");
    }
    std::cout << "PASS 1445 unique AU parameters and metadata\n";
    return ids;
}

void verifyFactoryPresets(AudioUnit unit)
{
    CFHandle<CFArrayRef> presets;
    UInt32 bytes = sizeof(presets.value);
    checked(AudioUnitGetProperty(unit, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global,
                                 0, &presets.value, &bytes), "read factory preset property");
    require(presets.value != nullptr && CFGetTypeID(presets.value) == CFArrayGetTypeID()
            && CFArrayGetCount(presets.value) == 17, "registered AU must expose exactly 17 factory presets");
    std::unordered_set<SInt32> numbers;
    for (CFIndex index = 0; index < CFArrayGetCount(presets.value); ++index) {
        const auto* preset = static_cast<const AUPreset*>(CFArrayGetValueAtIndex(presets.value, index));
        require(preset != nullptr && preset->presetNumber >= 0 && numbers.insert(preset->presetNumber).second
                && preset->presetName != nullptr && CFGetTypeID(preset->presetName) == CFStringGetTypeID()
                && CFStringGetLength(preset->presetName) > 0, "factory preset has invalid number or name");
    }
    std::cout << "PASS 17 named factory presets\n";
}

void verifyStateRoundtrip(AudioUnit unit, AudioUnitParameterID id)
{
    ParameterInfo info;
    UInt32 bytes = sizeof(info.value);
    checked(AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                                 id, &info.value, &bytes), "query first AU parameter range");
    const auto low = info.value.minValue;
    const auto high = info.value.maxValue;
    require(high > low && (info.value.flags & kAudioUnitParameterFlag_IsWritable) != 0,
            "first listed AU parameter is not adjustable");
    AudioUnitParameterValue original = 0;
    checked(AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &original), "read original parameter");
    const auto chosen = low + (high - low) * 0.73f;
    checked(AudioUnitSetParameter(unit, id, kAudioUnitScope_Global, 0, chosen, 0), "set saved parameter value");
    AudioUnitParameterValue saved = 0;
    checked(AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &saved), "read quantized saved value");

    CFHandle<CFPropertyListRef> state;
    bytes = sizeof(state.value);
    checked(AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global,
                                 0, &state.value, &bytes), "save AU ClassInfo");
    require(state.value != nullptr && CFGetTypeID(state.value) == CFDictionaryGetTypeID(),
            "ClassInfo must be a dictionary");
    CFHandle<CFErrorRef> error;
    CFHandle<CFDataRef> serialized;
    serialized.value = CFPropertyListCreateData(kCFAllocatorDefault, state.value,
                                               kCFPropertyListBinaryFormat_v1_0, 0, &error.value);
    require(serialized.value != nullptr, "ClassInfo cannot be serialized as a property list");
    CFHandle<CFPropertyListRef> restoredState;
    CFHandle<CFErrorRef> parseError;
    restoredState.value = CFPropertyListCreateWithData(kCFAllocatorDefault, serialized.value,
                                                      kCFPropertyListImmutable, nullptr, &parseError.value);
    require(restoredState.value != nullptr && CFGetTypeID(restoredState.value) == CFDictionaryGetTypeID(),
            "serialized ClassInfo did not roundtrip to a dictionary");

    // Endpoints also work for quantized, boolean and indexed parameters.
    const auto alternate = std::abs(saved - low) > std::abs(saved - high) ? low : high;
    checked(AudioUnitSetParameter(unit, id, kAudioUnitScope_Global, 0, alternate, 0), "change parameter before restore");
    AudioUnitParameterValue changed = 0;
    checked(AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &changed), "read changed parameter");
    const double tolerance = std::max(1.0e-6, static_cast<double>(high - low) * 1.0e-5);
    require(std::abs(static_cast<double>(changed) - saved) > tolerance, "test parameter did not change before restore");
    checked(AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global,
                                 0, &restoredState.value, sizeof(restoredState.value)), "restore AU ClassInfo");
    AudioUnitParameterValue restored = 0;
    checked(AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &restored), "read restored parameter");
    require(std::abs(static_cast<double>(restored) - saved) <= tolerance,
            "ClassInfo did not preserve the first listed parameter");
    checked(AudioUnitSetParameter(unit, id, kAudioUnitScope_Global, 0, original, 0), "restore original parameter");
    std::cout << "PASS serialized ClassInfo parameter roundtrip (AU ID " << id << ")\n";
}

void verifyBypass(AudioUnit unit, Input& input)
{
    UInt32 bypass = 1;
    checked(AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global,
                                 0, &bypass, sizeof(bypass)), "enable AU host bypass");
    bypass = 0;
    UInt32 bytes = sizeof(bypass);
    checked(AudioUnitGetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global,
                                 0, &bypass, &bytes), "read AU host bypass");
    require(bypass == 1, "AU bypass property did not retain its value");
    const auto stats = render(unit, input, 8, static_cast<double>(renderBlocks) * blockSize);
    require(stats.exactDry, "host bypass is not sample-exact dry passthrough");
    bypass = 0;
    checked(AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global,
                                 0, &bypass, sizeof(bypass)), "disable AU host bypass");
    std::cout << "PASS AU host bypass is sample-exact\n";
}

int run(const char* bundlePath)
{
    AudioComponentDescription description{};
    description.componentType = fourCC('a', 'u', 'f', 'x');
    description.componentSubType = fourCC('A', 'l', 'f', 'a');
    description.componentManufacturer = fourCC('A', 'l', 'p', 'h');
    CFHandle<CFBundleRef> bundle;
    AudioComponent component=nullptr;
    if(bundlePath!=nullptr) {
        CFHandle<CFURLRef> url;
        url.value=CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,reinterpret_cast<const UInt8*>(bundlePath),static_cast<CFIndex>(std::strlen(bundlePath)),true);
        bundle.value=CFBundleCreate(kCFAllocatorDefault,url.value);
        require(bundle.value!=nullptr&&CFBundleLoadExecutable(bundle.value),"load local AU bundle");
        auto factory=reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle.value,CFSTR("JuceAUFactory")));
        require(factory!=nullptr,"resolve local AU factory");
        component=AudioComponentRegister(&description,CFSTR("Alpha Audio: alpha local test"),0x00020000,factory);
    } else component=AudioComponentFindNext(nullptr,&description);
    require(component != nullptr,bundlePath?"cannot register local test AU (system AudioComponent service access is required)":"registered AU aufx/Alfa/Alph was not found; install alpha first");
    Input input; // Callback storage must outlive its AudioUnit, including uninitialization.
    Unit unit(component);
    std::cout << "PASS aufx/Alfa/Alph instantiates\n";
    const auto ids = verifyParameters(unit.value);
    verifyFactoryPresets(unit.value);

    checked(configure(unit, input), "configure and initialize stereo 48 kHz / 512 frames");
    verifyFormat(unit.value, 2);
    const auto stats = render(unit.value, input, renderBlocks, 0.0);
    require(stats.peak > 1.0e-7, "default FX produced only silence");
    require(stats.differenceRMS() > 1.0e-7, "default FX did not change the dry input");
    std::cout << "PASS stereo 48 kHz: 200 x 512 frames, peak=" << stats.peak
              << ", wet-dry RMS=" << stats.differenceRMS() << '\n';
    verifyStateRoundtrip(unit.value, ids.front());
    verifyBypass(unit.value, input);

    // Mono is optional. An accepted format must nevertheless render valid audio.
    Input monoInput;
    monoInput.channels = 1;
    Unit mono(component);
    const auto monoStatus = configure(mono, monoInput);
    if (monoStatus == noErr) {
        verifyFormat(mono.value, 1);
        const auto monoStats = render(mono.value, monoInput, 16, 0.0);
        require(monoStats.peak > 1.0e-7, "mono FX produced only silence");
        std::cout << "PASS mono 48 kHz render\n";
    } else {
        std::cout << "SKIP optional mono configuration (OSStatus " << monoStatus << ")\n";
    }
    std::cout << "PASS independent AudioUnit host checks\n";
    return 0;
}
} // namespace

int main(int argc,char** argv)
{
    @autoreleasepool {
        try {
            return run(argc>1?argv[1]:nullptr);
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << error.what() << '\n';
            return 1;
        }
    }
}
