#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostSlicePreparation.h"
#include "SamplePoolProgress.h"
#include <cmath>
#include <set>
#include <map>
#include <juce_cryptography/juce_cryptography.h>

// REAPER-specific VST3 extension, following JUCE ReaperEmbeddedViewPluginDemo.
// This header is available in the JUCE 8 VST3 target.
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wnon-virtual-dtor")
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

using namespace lsampler;

// Separate visibility from actual automation activity. Hiding a REAPER lane
// leaves its envelope ACTIVE and locks the plugin's control in Read mode.
// REAPER's documented GetSetEnvelopeInfo_String("ACTIVE") can disable it while
// preserving the user-recorded points; "VISIBLE" merely affects the UI.
namespace lsampler_reaper {
using namespace Steinberg;
class IReaperHostApplication : public FUnknown
{
public:
    virtual void* PLUGIN_API getReaperApi(CStringA name) = 0;
    virtual void* PLUGIN_API getReaperParent(uint32 type) = 0;
    virtual void* PLUGIN_API reaperExtended(uint32 call, void*, void*, void*) = 0;
    static const FUID iid;
};
// Official REAPER VST3 host-extension IID (reaper_vst3_interfaces.h).
// DECLARE_CLASS_IID must precede DEF_CLASS_IID, otherwise the VST3 SDK
// cannot resolve the generated IReaperHostApplication_iid symbol on MSVC.
DECLARE_CLASS_IID (IReaperHostApplication, 0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C)
DEF_CLASS_IID (IReaperHostApplication)
}

class LSamplerReaperHostExtension final : public juce::VST3ClientExtensions
{
public:
    ~LSamplerReaperHostExtension() override
    {
        if (host != nullptr) host->release();
    }
    void setIHostApplication(Steinberg::FUnknown* application) override
    {
        if (host != nullptr) { host->release(); host = nullptr; }
        if (application == nullptr) return;
        void* result = nullptr;
        if (application->queryInterface(lsampler_reaper::IReaperHostApplication::iid, &result)
            == Steinberg::kResultOk)
            host = static_cast<lsampler_reaper::IReaperHostApplication*>(result);
    }
    bool available() const noexcept { return host != nullptr; }

    int getActive(const LSampler24AudioProcessor& processor, juce::AudioProcessorParameter* param) const
    {
        const auto fx = findFocusedFx(processor, param);
        if (!fx) return -1;
        const auto getEnvelope = getApi<void* (*)(void*, int, int, bool)>("GetFXEnvelope");
        const auto attribute = getApi<bool (*)(void*, const char*, char*, bool)>("GetSetEnvelopeInfo_String");
        if (!getEnvelope || !attribute) return -1;
        auto* envelope = getEnvelope(fx.track, fx.index, fx.parameter, false);
        if (envelope == nullptr) return 0; // Not created: it cannot automate.
        char active[32] = {};
        if (!attribute(envelope, "ACTIVE", active, false)) return -1;
        return active[0] == '1' ? 1 : active[0] == '0' ? 0 : -1;
    }

    bool toggle(LSampler24AudioProcessor& processor, juce::AudioProcessorParameter* param)
    {
        const auto fx = findFocusedFx(processor, param);
        if (!fx) return false;
        const auto getEnvelope = getApi<void* (*)(void*, int, int, bool)>("GetFXEnvelope");
        const auto attribute = getApi<bool (*)(void*, const char*, char*, bool)>("GetSetEnvelopeInfo_String");
        if (!getEnvelope || !attribute) return false;
        // Check existence/ACTIVE first; enabling a missing envelope creates it.
        void* env = getEnvelope(fx.track, fx.index, fx.parameter, false);
        bool wasActive = false;
        if (env != nullptr)
        {
            char active[32] = {};
            if (!attribute(env, "ACTIVE", active, false)) return false;
            if (active[0] != '0' && active[0] != '1') return false;
            wasActive = active[0] == '1';
        }
        if (!wasActive && env == nullptr)
            env = getEnvelope(fx.track, fx.index, fx.parameter, true);
        if (env == nullptr) return false;
        auto set = [attribute, env] (const char* name, const char* value)
        {
            char text[2] { value[0], 0 };
            return attribute(env, name, text, true);
        };
        // ACTIVE must succeed before touching VISIBLE. An invisible, active
        // envelope must never be mistaken for a disabled one.
        if (!set("ACTIVE", wasActive ? "0" : "1")) return false;
        if (wasActive)
        {
            set("ARM", "0");
            set("VISIBLE", "0");
        }
        else
        {
            set("VISIBLE", "1");
            set("ARM", "1");
        }
        if (auto adjust = getApi<void (*)(bool)>("TrackList_AdjustWindows")) adjust(false);
        if (auto update = getApi<void (*)()>("UpdateArrange")) update();
        return true;
    }
private:
    template <class T> T getApi(const char* name) const
    {
        return host != nullptr ? reinterpret_cast<T>(host->getReaperApi(name)) : nullptr;
    }
    struct FocusedFx { void* track = nullptr; int index = -1; int parameter = -1;
        explicit operator bool() const { return track != nullptr && index >= 0 && parameter >= 0; } };
    FocusedFx findFocusedFx(const LSampler24AudioProcessor& processor,
                            juce::AudioProcessorParameter* param) const
    {
        if (host == nullptr || param == nullptr) return {};
        // Never address another instance when several LSampler-24 FX are open.
        // The host supplies the owning track for THIS VST3 instance.
        auto* owner = host->getReaperParent(1);
        if (owner == nullptr) return {};
        const auto focused = getApi<bool (*)(int, int*, int*, int*, int*, int*)>("GetTouchedOrFocusedFX");
        const auto getTrack = getApi<void* (*)(void*, int)>("GetTrack");
        const auto getMaster = getApi<void* (*)(void*)>("GetMasterTrack");
        const auto getParamName = getApi<bool (*)(void*, int, int, char*, int)>("TrackFX_GetParamName");
        if (!focused || !getTrack || !getMaster || !getParamName) return {};
        int trackIdx = -2, itemIdx = -2, takeIdx = -2, fxIdx = -1, ignored = -1;
        if (!focused(1, &trackIdx, &itemIdx, &takeIdx, &fxIdx, &ignored)
            || itemIdx != -1 || fxIdx < 0) return {};
        void* focusedTrack = trackIdx == -1 ? getMaster(nullptr) : getTrack(nullptr, trackIdx);
        if (focusedTrack == nullptr || focusedTrack != owner) return {};
        const int parameter = processor.getParameters().indexOf(param);
        if (parameter < 0) return {};
        char name[512] = {};
        if (!getParamName(owner, fxIdx, parameter, name, int(sizeof(name)))) return {};
        // Guard the parameter index against host-specific reordering.
        if (!juce::String::fromUTF8(name).trim().equalsIgnoreCase(param->getName(512).trim())) return {};
        return { owner, fxIdx, parameter };
    }
    lsampler_reaper::IReaperHostApplication* host = nullptr;
};

juce::VST3ClientExtensions* LSampler24AudioProcessor::getVST3ClientExtensions()
{
    return reaperHostExtension.get();
}
bool LSampler24AudioProcessor::hasReaperEnvelopeApi() const noexcept
{
    return reaperHostExtension != nullptr && reaperHostExtension->available();
}
int LSampler24AudioProcessor::getReaperEnvelopeActive(juce::AudioProcessorParameter* p) const
{
    return reaperHostExtension != nullptr ? reaperHostExtension->getActive(*this, p) : -1;
}
bool LSampler24AudioProcessor::toggleReaperEnvelopeActive(juce::AudioProcessorParameter* p)
{
    return reaperHostExtension != nullptr && reaperHostExtension->toggle(*this, p);
}


namespace {

static juce::File settingsFileForLibrary()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Lo-lo78").getChildFile("LSampler-24");
    return dir.getChildFile("settings.xml");
}

static juce::File configuredLibraryRoot(const juce::File& overrideRoot)
{
    if (overrideRoot.getFullPathName().isNotEmpty())
        return overrideRoot;
    auto fallback = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LSampler-24");
    auto file = settingsFileForLibrary();
    if (!file.existsAsFile()) return fallback;
    juce::XmlDocument doc(file);
    auto xml = doc.getDocumentElement();
    if (xml == nullptr || !xml->hasTagName("LSampler24Settings")) return fallback;
    auto configured = juce::File(xml->getStringAttribute("libraryRoot"));
    if (configured.getFullPathName().isEmpty()) return fallback;

    // Keep the configured path even when an external drive is temporarily
    // unavailable. Falling back silently to Documents could make load/save or
    // import/export operations use the wrong Library without the user noticing.
    // Availability is checked by the editor immediately before Library I/O.
    return configured;
}
// Only destructively edited RAM audio needs an inline fallback in host state.
// Slot/bank saving already materialises this buffer and clears its modified flag.
// IEEE float samples use JUCE's little-endian stream encoding, with no quantisation.
juce::ValueTree storeEditedAudio(const SharedSample& sample) {
    juce::ValueTree tree("EditedAudio");
    const int channels=sample.audio.getNumChannels(),frames=sample.audio.getNumSamples();
    tree.setProperty("encoding","float32-le-planar",nullptr);
    tree.setProperty("channels",channels,nullptr);tree.setProperty("frames",frames,nullptr);
    tree.setProperty("sampleRate",sample.sourceSampleRate,nullptr);
    juce::MemoryOutputStream stream;
    for(int ch=0;ch<channels;++ch)for(int i=0;i<frames;++i)stream.writeFloat(sample.audio.getSample(ch,i));
    tree.setProperty("data",stream.getMemoryBlock().toBase64Encoding(),nullptr);return tree;
}
std::shared_ptr<SharedSample> restoreEditedAudio(const juce::ValueTree& tree,const juce::File& source,juce::String& error) {
    const int channels=int(tree.getProperty("channels",0)),frames=int(tree.getProperty("frames",0));
    const double rate=double(tree.getProperty("sampleRate",0));
    if(tree.getProperty("encoding").toString()!="float32-le-planar"||channels<1||channels>2||frames<1||!std::isfinite(rate)||rate<=0) {
        error="Invalid edited slot audio header";return {};
    }
    juce::MemoryBlock bytes;
    const auto required=uint64_t(channels)*uint64_t(frames)*sizeof(float);
    if(!bytes.fromBase64Encoding(tree.getProperty("data").toString())||uint64_t(bytes.getSize())!=required) {
        error="Invalid edited slot audio data";return {};
    }
    auto sample=std::make_shared<SharedSample>();sample->audio.setSize(channels,frames);
    sample->sourceSampleRate=rate;sample->sourceFile=source;
    juce::MemoryInputStream stream(bytes,false);double peak=0;
    for(int ch=0;ch<channels;++ch) {
        double sum=0;
        for(int i=0;i<frames;++i) {
            const float x=stream.readFloat();
            if(!std::isfinite(x)){error="Non-finite edited slot audio";return {};}
            sample->audio.setSample(ch,i,x);sum+=x;peak=std::max(peak,std::abs(double(x)));
        }
        sample->dc[size_t(ch)]=sum/frames;
    }
    if(channels==1)sample->dc[1]=sample->dc[0];sample->peak=peak;return sample;
}

// One unit per main sample (or empty slot), plus each referenced Sample Set
// alternate. The same units are used for a standalone slot and the bank total.
int slotLoadUnits(const juce::ValueTree& slot)
{
    int units = 1;
    const auto set = slot.getChildWithName("SampleSet");
    for (int i = 0; i < set.getNumChildren(); ++i)
    {
        const auto entry = set.getChild(i);
        if (int(entry.getProperty("index", i)) != 0
            && entry.getProperty("sampleReference").toString().isNotEmpty())
            ++units;
    }
    return units;
}
}


LSampler24AudioProcessor::LSampler24AudioProcessor(const juce::File& libraryRootOverride)
    : AudioProcessor([] {
        BusesProperties buses;
        buses = buses.withOutput("Output", juce::AudioChannelSet::stereo(), true);
        for (int i = 1; i <= 24; ++i)
            buses = buses.withOutput("Out " + juce::String(2*i+1) + "/" + juce::String(2*i+2), juce::AudioChannelSet::stereo(), false);
        return buses;
      }()), library(configuredLibraryRoot(libraryRootOverride))
{
    static_assert(std::atomic<int>::is_always_lock_free);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    createHostParameters();
    reaperHostExtension = std::make_unique<LSamplerReaperHostExtension>();
    markAudioStateDirty();
}

LSampler24AudioProcessor::~LSampler24AudioProcessor()
{
    cancelPendingUpdate();
    shuttingDown.store(true, std::memory_order_release);
    // Finish the running disk operation before any processor-owned state is freed.
    // Closing only the editor never waits for this worker.
    while (!fileWorker.removeAllJobs(false, 1000)) {}
    voicePool.allNotesOff();
    libraryPreviewVoicePool.allNotesOff();
}

bool LSampler24AudioProcessor::startFileTask(FileTask task, std::function<void(FileTaskResult)> completion)
{
    bool expected = false;
    if (shuttingDown.load() || !fileTaskRunning.compare_exchange_strong(expected, true)) return false;
    fileTaskProgress.store(0.0, std::memory_order_release);
    fileWorker.addJob(std::function<void()>([this, task = std::move(task), completion = std::move(completion)]() mutable {
        FileTaskResult result;
        try { result = task(*this); }
        catch (const std::exception& e) { result.message = "File operation failed: " + juce::String(e.what()); }
        catch (...) { result.message = "File operation failed"; }
        if (result.message.isEmpty()) result.message = result.ok ? "File operation completed" : "File operation stopped";
        { const juce::ScopedLock lock(fileResultLock); lastFileTaskResult = result; }
        fileTaskProgress.store(1.0, std::memory_order_release);
        fileTaskRunning.store(false, std::memory_order_release);
        if (!shuttingDown.load(std::memory_order_acquire))
            juce::MessageManager::callAsync([completion = std::move(completion), result]() mutable { completion(result); });
    }));
    return true;
}

void LSampler24AudioProcessor::prepareToPlay(double sampleRate, int)
{
    voicePool.prepare(sampleRate);
    libraryPreviewVoicePool.prepare(sampleRate);
    libraryPreviewVoicePool.setStates(&libraryPreviewStates);
    outputGlueEnvelope = 0.0;
    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        preparedSampleRate = sampleRate > 0 ? sampleRate : 44100;
        markAudioStateDirty();
    }
    syncAudioStateFromSlots();
}

void LSampler24AudioProcessor::releaseResources()
{
    voicePool.allNotesOff();
    libraryPreviewVoicePool.allNotesOff();
    libraryPreviewPlaying = false;
    libraryPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
    previewPlaying = false;
    previewPlayingSlot = -1;
    previewStartSlot.store(-1, std::memory_order_release);
    slicePreviewKind.store(0);slicePreviewCommand.store(-1);
    importPreviewPlaying = false;
    importPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
}

bool LSampler24AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
        if (!layouts.outputBuses[i].isDisabled() && layouts.outputBuses[i] != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void LSampler24AudioProcessor::updateThresholdWindow(int slotIndex)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    auto& slot = slots[size_t(slotIndex)];
    if (!slot.sample || slot.sample->audio.getNumSamples() < 2) {
        slot.thresholdStartFrame = slot.thresholdEndFrame = 0;
        return;
    }
    const auto w = lsampler::calculateThresholdWindow(slot.parameters, slot.sample.get());
    slot.thresholdStartFrame = w.start;
    slot.thresholdEndFrame = w.end;
}

void LSampler24AudioProcessor::markAudioStateDirty()
{
    const juce::ScopedLock lock(stateLock);
    publishHostValuesLocked(!restoringHostState);
    uiRevision.fetch_add(1, std::memory_order_release);
    // This function is called only by control/state threads. No audio-thread lock,
    // shared_ptr update, allocation, or destruction is needed to consume a state.
    // Called by control/state code while its caller owns stateLock. The initial
    // constructor call happens before the processor is visible to another thread.
    auto& target = snapshots[size_t(writerSnapshot)];
    for (int i = 0; i < slotCount; ++i) {
        const auto idx = size_t(i);
        auto& slot = slots[idx];
        auto& state = target.states[idx];
        const auto publishOwner = [&](int sampleIndex, const std::shared_ptr<SharedSample>& owner)
        {
            auto& published = target.owners[idx][size_t(sampleIndex)];
            if (published && published != owner) retiredSamples.push_back(published);
            published = owner;
        };
        publishOwner(0, slot.sample);
        state = prepareSlotAudioState(slot.parameters, slot.sample.get(), preparedSampleRate, ++nextRevision,
                                      slot.thresholdStartFrame, slot.thresholdEndFrame);
        state.sampleSet[0] = slot.sample.get();
        state.playback[0] = prepareSamplePlaybackState(slot.parameters, slot.sample.get(), preparedSampleRate,
                                                       slot.thresholdStartFrame, slot.thresholdEndFrame);
        for (int sampleIndex = 1; sampleIndex < sampleSetSize; ++sampleIndex)
        {
            const auto& owner = slot.alternateSamples[size_t(sampleIndex - 1)];
            publishOwner(sampleIndex, owner);
            state.sampleSet[size_t(sampleIndex)] = owner.get();
            state.playback[size_t(sampleIndex)] = prepareSamplePlaybackState(slot.parameters, owner.get(), preparedSampleRate);
        }
        for (int sampleIndex = 0; sampleIndex < sampleSetSize; ++sampleIndex)
        {
            state.sampleVelocityLow[size_t(sampleIndex)] = static_cast<uint8_t>(juce::jlimit(1, 127, slot.sampleVelocityLow[size_t(sampleIndex)]));
            state.sampleVelocityHigh[size_t(sampleIndex)] = static_cast<uint8_t>(juce::jlimit(1, 127, slot.sampleVelocityHigh[size_t(sampleIndex)]));
        }
        state.variationMode = juce::jlimit<int>(variationOff, variationShuffleNoRepeat, slot.variationMode);
        state.slice = prepareSliceAudio(slot.slice,state.length);
    }
    writerSnapshot = middleSnapshot.exchange(writerSnapshot | 4, std::memory_order_acq_rel) & 3;
    // Sample references held by voices are intrusive counters: deletion always occurs here.
    retiredSamples.erase(std::remove_if(retiredSamples.begin(), retiredSamples.end(), [](const auto& sample) {
        return sample->voiceReferences.load(std::memory_order_acquire) == 0;
    }), retiredSamples.end());
}

void LSampler24AudioProcessor::syncAudioStateFromSlots()
{
    if ((middleSnapshot.load(std::memory_order_acquire) & 4) != 0) {
        readerSnapshot = middleSnapshot.exchange(readerSnapshot, std::memory_order_acq_rel) & 3;
        updateAutomatedAudio(true);
    } else {
        updateAutomatedAudio(false);
    }
}

void LSampler24AudioProcessor::applyOutputStage(juce::AudioBuffer<float>& buffer, const std::array<int, 25>& routes) noexcept
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || buffer.getNumChannels() <= 0) return;

    std::array<int, 25> leftChannels {};
    std::array<int, 25> rightChannels {};
    int busCount = 0;
    for (int bus = 0; bus < static_cast<int>(routes.size()); ++bus)
    {
        const int left = routes[size_t(bus)];
        if (left < 0 || left + 1 >= buffer.getNumChannels()) continue;
        leftChannels[size_t(busCount)] = left;
        rightChannels[size_t(busCount)] = left + 1;
        ++busCount;
    }
    if (busCount == 0) return;

    const bool stageOn = getGlobalOutputParameter(GlobalP::output_stage) >= 0.5;
    const double masterDb = getGlobalOutputParameter(GlobalP::master_output_gain);
    const float masterGain = masterDb <= -119.9 ? 0.0f : float(std::pow(10.0, masterDb / 20.0));
    const float glueAmount = float(juce::jlimit(0.0, 1.0, getGlobalOutputParameter(GlobalP::bus_glue) * 0.01));
    const float driveAmount = float(juce::jlimit(0.0, 1.0, getGlobalOutputParameter(GlobalP::bus_soft_drive) * 0.01));
    const float ceiling = float(juce::jlimit(0.1, 1.0, getGlobalOutputParameter(GlobalP::output_ceiling)));
    const double attackSamples = juce::jmax(1.0, 0.003 * preparedSampleRate);
    const double releaseSamples = juce::jmax(1.0, 0.020 * preparedSampleRate);

    std::array<float*, 50> channels {};
    for (int bus = 0; bus < busCount; ++bus)
    {
        channels[size_t(bus * 2)] = buffer.getWritePointer(leftChannels[size_t(bus)]);
        channels[size_t(bus * 2 + 1)] = buffer.getWritePointer(rightChannels[size_t(bus)]);
    }

    // JSFX legacy fallback when Output Stage is Off: Main 1/2 only.
    if (!stageOn)
    {
        auto* mainL = channels[0];
        auto* mainR = channels[1];
        for (int i = 0; i < numSamples; ++i)
        {
            float l = !std::isnan(mainL[i]) ? mainL[i] : 0.0f;
            float r = !std::isnan(mainR[i]) ? mainR[i] : 0.0f;
            l *= masterGain; r *= masterGain;
            const float peak = juce::jmax(std::abs(l), std::abs(r));
            if (peak > 0.98f)
            {
                const float g = 0.98f / peak;
                l *= g; r *= g;
            }
            mainL[i] = juce::jlimit(-64.0f, 64.0f, l);
            mainR[i] = juce::jlimit(-64.0f, 64.0f, r);
        }
        // The JSFX only guards active multichannel pins when present. Keep their
        // audio untouched in fallback mode, apart from non-finite/safety cleanup.
        for (int bus = 1; bus < busCount; ++bus)
            for (int c = 0; c < 2; ++c)
            {
                auto* d = channels[size_t(bus * 2 + c)];
                for (int i = 0; i < numSamples; ++i)
                {
                    float v = !std::isnan(d[i]) ? d[i] : 0.0f;
                    d[i] = juce::jlimit(-64.0f, 64.0f, v);
                }
            }
        return;
    }

    const int channelCount = busCount * 2;
    const float softDriveG = 1.0f + driveAmount * 2.75f;
    const float softDriveC = 1.0f / (1.0f + driveAmount * 1.15f);
    const float allOutputGain = masterGain * 1.2f;

    for (int i = 0; i < numSamples; ++i)
    {
        // NaN/non-finite guard before the shared detector, matching the JSFX intent.
        float busL = 0.0f, busR = 0.0f;
        for (int bus = 0; bus < busCount; ++bus)
        {
            auto* l = channels[size_t(bus * 2)];
            auto* r = channels[size_t(bus * 2 + 1)];
            if (std::isnan(l[i])) l[i] = 0.0f;
            if (std::isnan(r[i])) r[i] = 0.0f;
            busL += l[i];
            busR += r[i];
        }

        const double glueIn = juce::jmax(std::abs(double(busL)), std::abs(double(busR)));
        outputGlueEnvelope += (glueIn - outputGlueEnvelope)
                            / (outputGlueEnvelope < glueIn ? attackSamples : releaseSamples);

        double glueGain = 1.0;
        constexpr double glueThreshold = 0.6;
        constexpr double glueRatio = 2.0;
        if (outputGlueEnvelope > glueThreshold)
        {
            const double over = outputGlueEnvelope - glueThreshold;
            glueGain = (glueThreshold + over / glueRatio) / outputGlueEnvelope;
        }
        float stageGain = float(1.0 + (glueGain - 1.0) * glueAmount);
        stageGain *= 1.0f + glueAmount * 0.06f;

        for (int c = 0; c < channelCount; ++c)
            channels[size_t(c)][i] *= stageGain;

        if (driveAmount > 0.000001f)
            for (int c = 0; c < channelCount; ++c)
            {
                float& v = channels[size_t(c)][i];
                v = (v * softDriveG) / (1.0f + std::abs(v) * driveAmount * 2.75f) * softDriveC;
            }

        float limiterPeak = 0.0f;
        for (int c = 0; c < channelCount; ++c)
        {
            float& v = channels[size_t(c)][i];
            v *= allOutputGain;
            limiterPeak = juce::jmax(limiterPeak, std::abs(v));
        }

        if (limiterPeak > ceiling)
        {
            const float limiterTarget = ceiling + (limiterPeak - ceiling) * 0.15f;
            const float limiterGain = limiterTarget / limiterPeak;
            for (int c = 0; c < channelCount; ++c)
                channels[size_t(c)][i] *= limiterGain;
        }

        for (int c = 0; c < channelCount; ++c)
            channels[size_t(c)][i] = juce::jlimit(-64.0f, 64.0f, channels[size_t(c)][i]);
    }
}

void LSampler24AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    syncAudioStateFromSlots();
    if (resetOutputEnvelope.load(std::memory_order_acquire) && resetOutputEnvelope.exchange(false)) outputGlueEnvelope = 0.0;
    if ((libraryPreviewMiddle.load(std::memory_order_acquire) & 4) != 0) {
        // Stop old voices before handing their snapshot back to the publisher.
        libraryPreviewVoicePool.stopPreviewVoices();
        libraryPreviewReader = libraryPreviewMiddle.exchange(libraryPreviewReader, std::memory_order_acq_rel) & 3;
        libraryPreviewStates[0] = (*libraryPreviewSnapshots)[size_t(libraryPreviewReader)].state;
        libraryPreviewVoicePool.setStates(&libraryPreviewStates);
        libraryPreviewPlaying = false;
        libraryPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
    }
    // Only sleep when no musical work or control command can be lost. MIDI CC,
    // sustain, bend, every preview command and new preview data wake this block.
    if (midi.isEmpty() && voicePool.activeVoiceCount() == 0
        && libraryPreviewVoicePool.activeVoiceCount() == 0 && !importPreviewPlaying
        && !previewPlaying && !libraryPreviewPlaying && slicePreviewKind.load() == 0
        && stopVoicesMask.load() == 0 && slicePreviewCommand.load() < 0
        && !previewToggleRequested.load() && previewStartSlot.load() < 0
        && !previewStopRequested.load() && !previewRestartRequested.load() && !previewAuditionRequested.load()
        && !previewSampleSetRequested.load()
        && !libraryPreviewToggleRequested.load() && !libraryPreviewStopRequested.load()
        && !importPreviewToggleRequested.load() && !importPreviewStopRequested.load()
        && !importPreviewSeekRequested.load() && (importPreviewMiddle.load() & 4) == 0)
    {
        // Same zero-input release equation as the output stage, evaluated once
        // per silent block. The output has no delay/reverb tail to truncate.
        if (outputGlueEnvelope > 0.0 && getGlobalOutputParameter(GlobalP::output_stage) >= 0.5) {
            const double release = juce::jmax(1.0, 0.020 * preparedSampleRate);
            outputGlueEnvelope *= std::pow(1.0 - 1.0 / release, buffer.getNumSamples());
            if (outputGlueEnvelope < 1.0e-20) outputGlueEnvelope = 0.0;
        }
        return;
    }
    if ((importPreviewMiddle.load(std::memory_order_acquire) & 4) != 0)
    {
        importPreviewReader = importPreviewMiddle.exchange(importPreviewReader, std::memory_order_acq_rel) & 3;
        const auto& p = importPreviewSnapshots[static_cast<size_t>(importPreviewReader)];
        if (p.revision != importPreviewSeenRevision)
        {
            importPreviewSeenRevision = p.revision;
            importPreviewPosition = 0.0;
            importPreviewPositionSeconds.store(0.0, std::memory_order_relaxed);
            importPreviewPlaying = false;
            importPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
        }
    }
    const auto& importPreview = importPreviewSnapshots[static_cast<size_t>(importPreviewReader)];
    if (importPreviewStopRequested.exchange(false, std::memory_order_acq_rel))
    {
        importPreviewPlaying = false;
        importPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
    }
    if (importPreviewSeekRequested.exchange(false, std::memory_order_acq_rel))
    {
        if (importPreview.sample != nullptr)
        {
            const double rate = importPreview.sample->sourceSampleRate > 0.0 ? importPreview.sample->sourceSampleRate : preparedSampleRate;
            const double seconds = juce::jlimit(0.0, importPreviewLengthSeconds.load(std::memory_order_relaxed), importPreviewSeekSeconds.load(std::memory_order_relaxed));
            importPreviewPosition = seconds * rate;
            importPreviewPositionSeconds.store(seconds, std::memory_order_relaxed);
        }
    }
    if (importPreviewToggleRequested.exchange(false, std::memory_order_acq_rel))
    {
        if (importPreview.sample != nullptr)
        {
            if (importPreviewPlaying)
                importPreviewPlaying = false;
            else
            {
                if (importPreviewPosition >= importPreview.sample->audio.getNumSamples() - 1) importPreviewPosition = 0.0;
                importPreviewPlaying = true;
            }
            importPreviewPlayingAtomic.store(importPreviewPlaying, std::memory_order_relaxed);
        }
    }
    const auto& audio = automatedAudio;
    const auto slotHasAudio = [](const lsampler::SlotAudioState& state) noexcept
    {
        for (auto* sample : state.sampleSet) if (sample != nullptr) return true;
        return false;
    };
    const auto previewMidiNoteForSlot = [](const lsampler::SlotAudioState& state) noexcept
    {
        // Preview the Slot as it is actually mapped on the keyboard. Original
        // Pitch is the tuning root, not necessarily a playable key. If it sits
        // outside Low/High Key, use the nearest mapped key so audition pitch
        // matches what MIDI playback of the Slot produces.
        const int low = juce::jlimit(0, 127, juce::roundToInt(state.params[P::low]));
        const int high = juce::jlimit(0, 127, juce::roundToInt(state.params[P::high]));
        const int lo = juce::jmin(low, high);
        const int hi = juce::jmax(low, high);
        const int root = juce::jlimit(0, 127, juce::roundToInt(state.params[P::root]));
        return juce::jlimit(lo, hi, root);
    };

    const auto stop = stopVoicesMask.exchange(0, std::memory_order_acq_rel);
    for (int i = 0; i < slotCount; ++i) if ((stop & (1u << i)) != 0) voicePool.stopSlotVoices(i);
    previewPlaying = voicePool.hasPreviewVoices();
    if (!previewPlaying) { previewPlayingSlot = -1; previewPlayingSampleIndex = -1; }

    if (previewStopRequested.exchange(false, std::memory_order_acq_rel)) {
        voicePool.stopPreviewVoices();
        previewPlaying = false;
        previewPlayingSlot = -1;
        previewPlayingSampleIndex = -1;
    }

    if (previewRestartRequested.exchange(false, std::memory_order_acq_rel)) {
        const int slot = juce::jlimit(0, slotCount - 1, previewTargetSlot.load());
        const bool shouldRestart = previewPlaying && previewPlayingSlot == slot;
        if (shouldRestart) {
            voicePool.stopPreviewVoices(slot);
            if (slotHasAudio(audio[size_t(slot)])) {
                const int sampleIndex = previewPlayingSampleIndex;
                const int root = previewMidiNoteForSlot(audio[size_t(slot)]);
                voicePool.noteOn(slot, root, 1.0f, 0, true, -1.0, -1, false, sampleIndex);
                previewPlaying = true;
                previewPlayingSlot = slot;
                previewPlayingSampleIndex = sampleIndex;
            } else {
                previewPlaying = false;
                previewPlayingSlot = -1;
            }
        }
    }

    if (previewAuditionRequested.exchange(false, std::memory_order_acq_rel)) {
        const int slot = juce::jlimit(0, slotCount - 1, previewTargetSlot.load());
        const bool shouldRestart = previewPlaying && previewPlayingSlot == slot;
        if (shouldRestart) {
            voicePool.stopPreviewVoices(slot);
            if (slotHasAudio(audio[size_t(slot)])) {
                const double startPercent = previewAuditionStartPercent.load(std::memory_order_relaxed);
                voicePool.noteOn(slot, previewMidiNoteForSlot(audio[size_t(slot)]), 1.0f, 0, true, startPercent);
                previewPlaying = true;
                previewPlayingSlot = slot;
                previewPlayingSampleIndex = -1;
            } else {
                previewPlaying = false;
                previewPlayingSlot = -1;
            }
        }
    }

    if (previewSampleSetRequested.exchange(false, std::memory_order_acq_rel)) {
        const int slot = juce::jlimit(0, slotCount - 1, previewTargetSlot.load(std::memory_order_relaxed));
        const int sampleIndex = juce::jlimit(0, sampleSetSize - 1, previewTargetSampleIndex.load(std::memory_order_relaxed));
        // This command is an explicit restart, not a toggle. The GUI owns
        // Preview On/Off independently of voice lifetime and uses Stop to switch off.
        voicePool.stopPreviewVoices();
        previewPlaying = false;
        previewPlayingSlot = -1;
        previewPlayingSampleIndex = -1;
        if (audio[size_t(slot)].sampleSet[size_t(sampleIndex)] != nullptr) {
            const int root = previewMidiNoteForSlot(audio[size_t(slot)]);
            voicePool.noteOn(slot, root, 1.0f, 0, true, -1.0, -1, false, sampleIndex);
            previewPlaying = voicePool.hasPreviewVoices(slot);
            previewPlayingSlot = previewPlaying ? slot : -1;
            previewPlayingSampleIndex = previewPlaying ? sampleIndex : -1;
        }
    }

    if (libraryPreviewStopRequested.exchange(false, std::memory_order_acq_rel))
    {
        libraryPreviewVoicePool.stopPreviewVoices();
        libraryPreviewPlaying = false;
        libraryPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
    }
    if (libraryPreviewToggleRequested.exchange(false, std::memory_order_acq_rel))
    {
        if (libraryPreviewPlaying)
        {
            libraryPreviewVoicePool.stopPreviewVoices();
            libraryPreviewPlaying = false;
        }
        else if (libraryPreviewStates[0].sample != nullptr)
        {
            libraryPreviewVoicePool.stopPreviewVoices();
            libraryPreviewVoicePool.noteOn(0, previewMidiNoteForSlot(libraryPreviewStates[0]), 1.0f, 0, true);
            libraryPreviewPlaying = true;
        }
        libraryPreviewPlayingAtomic.store(libraryPreviewPlaying, std::memory_order_relaxed);
    }

    double bpm = 120;
    if (auto* playHead = getPlayHead()) if (auto position = playHead->getPosition())
        if (auto tempo = position->getBpm()) if (std::isfinite(*tempo) && *tempo > 0) bpm = *tempo;
    voicePool.setTempo(bpm);
    libraryPreviewVoicePool.setTempo(bpm);
    std::array<int, 25> routes;
    routes.fill(-1);
    for (int i = 0; i < getBusCount(false) && i < int(routes.size()); ++i)
        if (auto* bus = getBus(false, i); bus != nullptr && bus->isEnabled())
            routes[size_t(i)] = bus->getChannelIndexInProcessBlockBuffer(0);
    voicePool.setOutputRoutes(routes);
    libraryPreviewVoicePool.setOutputRoutes(routes);

    const int requestedPreviewSlot = previewStartSlot.exchange(-1, std::memory_order_acq_rel);
    if (requestedPreviewSlot >= 0) {
        // This is always Start, never Toggle. Stop the former Slot and audition
        // the exact Slot selected in the grid using its mapped MIDI pitch.
        voicePool.stopPreviewVoices();
        previewPlaying = false;
        previewPlayingSlot = -1;
        previewPlayingSampleIndex = -1;
        const int slot = juce::jlimit(0, slotCount - 1, requestedPreviewSlot);
        if (slotHasAudio(audio[size_t(slot)])) {
            voicePool.noteOn(slot, previewMidiNoteForSlot(audio[size_t(slot)]), 1.0f, 0, true);
            previewPlaying = voicePool.hasPreviewVoices(slot);
            previewPlayingSlot = previewPlaying ? slot : -1;
        }
    }

    if (previewToggleRequested.exchange(false, std::memory_order_acq_rel)) {
        if (previewPlaying) { voicePool.stopPreviewVoices(); previewPlaying = false; previewPlayingSlot = -1; previewPlayingSampleIndex = -1; }
        else {
            voicePool.stopPreviewVoices();
            const int slot = juce::jlimit(0, slotCount-1, previewTargetSlot.load());
            if (slotHasAudio(audio[size_t(slot)])) {
                voicePool.noteOn(slot, previewMidiNoteForSlot(audio[size_t(slot)]), 1.0f, 0, true);
                previewPlaying = true; previewPlayingSlot = slot; previewPlayingSampleIndex = -1;
            }
        }
    }
    // Process MIDI at its sample offset; getMessage() can allocate for SysEx, so
    // inspect the short MIDI bytes directly and ignore non-performance messages.
    const int sliceCommand=slicePreviewCommand.exchange(-1,std::memory_order_acq_rel);
    if(slicePreviewKind.load()!=0&&!voicePool.hasPreviewVoices(slicePreviewSlot))slicePreviewKind.store(0);
    if(sliceCommand>=0) {
        const int kind=sliceCommand&7,slot=(sliceCommand>>3)&31,item=(sliceCommand>>8)&127;
        const bool toggle=(sliceCommand&(1<<15))!=0;
        const bool stopping=kind==0||(toggle&&slicePreviewKind.load()==kind&&slicePreviewSlot==slot
                                      &&(kind==1||kind==3||kind==5||slicePreviewItem==item));
        voicePool.stopPreviewVoices();previewPlaying=false;previewPlayingSlot=-1;previewPlayingSampleIndex=-1;slicePreviewKind.store(0);
        if(!stopping&&slot<slotCount&&slotHasAudio(audio[size_t(slot)])) {
            // 1 normal slot preview (same Slice path as a MIDI note),
            // 2 physical slice, 3 forced sequencer, 4 programmed step,
            // 5 normal Slice Mode preview starting from the selected timeline step.
            // Kind 5 changes only the start head; it does not force Sequencer mode.
            const int request=kind==1?-1:kind==2?-1000-item:kind==3?-2000-item:kind==5?-3000-item:item;
            voicePool.noteOn(slot,juce::jlimit(0,127,juce::roundToInt(audio[size_t(slot)].params[P::root])),1.0f,0,true,-1.0,request);
            slicePreviewSlot=slot;slicePreviewItem=item;slicePreviewKind.store(kind);
            previewPlaying=voicePool.hasPreviewVoices(slot);previewPlayingSlot=previewPlaying?slot:-1;previewPlayingSampleIndex=-1;
        }
    }

    int offset = 0;
    for (const auto event : midi) {
        const int time = juce::jlimit(offset, buffer.getNumSamples(), event.samplePosition);
        voicePool.render(buffer, offset, time-offset); offset = time;
        if (event.numBytes < 1) continue;
        const auto* d = event.data;
        const int command = d[0] & 0xf0, channel = d[0] & 0x0f;
        if (event.numBytes >= 3 && command == 0x90 && d[2] != 0) {
            const int note = d[1] & 127, velocity = d[2] & 127;
            voicePool.choke(note);

            // Match the final LBPMCaptureSample.jsfx Slice MIDI Map priority.
            // First search all mapped slots for an assigned, unmuted step.
            // A pad address is independent of the slot Low/High and velocity
            // zones and plays at the slot root (handled by sliceMidiPad).
            int mappedSlot = -1, mappedStep = -1;
            for (int i = 0; i < slotCount && mappedSlot < 0; ++i) {
                const auto& s = audio[size_t(i)];
                if(!s.sampleSet[0] || s.slice.state[SliceG::midiMap]==0) continue;
                for(int step=0; step<s.slice.count; ++step) {
                    const auto& programmed=s.slice.state.steps[size_t(step)];
                    if(programmed[SliceP::midiNote]==note && programmed[SliceP::mute]==0) {
                        mappedSlot=i; mappedStep=step; break;
                    }
                }
            }
            if(mappedSlot>=0) {
                voicePool.noteOn(mappedSlot,note,float(velocity)/127.0f,channel,false,-1.0,mappedStep,true);
            } else {
                // Only when no Slice MIDI pad owns the note do normal slots use
                // their key/velocity zones. Mapped slots are excluded here.
                for (int i = 0; i < slotCount; ++i) {
                    const auto& s = audio[size_t(i)]; const auto& p = s.params;
                    if(!slotHasAudio(s) || s.slice.state[SliceG::midiMap]!=0) continue;
                    if(velocity<p[P::velocity_low]||velocity>p[P::velocity_high])continue;
                    if(note>=p[P::low]&&note<=p[P::high])
                        voicePool.noteOn(i,note,float(velocity)/127.0f,channel);
                }
            }
        } else if (event.numBytes >= 3 && (command == 0x80 || (command == 0x90 && d[2] == 0)))
            voicePool.noteOff(d[1] & 127, channel);
        else if (event.numBytes >= 3 && command == 0xb0)
            voicePool.controller(channel, d[1] & 127, d[2] & 127);
        else if (event.numBytes >= 3 && command == 0xe0)
            voicePool.pitchBend(channel, (int(d[1] & 127) | (int(d[2] & 127) << 7)) - 8192);
    }
    voicePool.render(buffer, offset, buffer.getNumSamples()-offset);
    libraryPreviewVoicePool.render(buffer, 0, buffer.getNumSamples());
    if (libraryPreviewPlaying && !libraryPreviewVoicePool.hasPreviewVoices(0))
    {
        libraryPreviewPlaying = false;
        libraryPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
    }

    // F3-style import browser preview: a lightweight direct source audition.
    // It never changes slot state and performs no optional slot DSP.
    if (importPreviewPlaying && importPreview.sample != nullptr && buffer.getNumChannels() > 0)
    {
        const auto& src = importPreview.sample->audio;
        const int n = src.getNumSamples();
        const int channels = src.getNumChannels();
        const double sourceRate = importPreview.sample->sourceSampleRate > 0.0 ? importPreview.sample->sourceSampleRate : preparedSampleRate;
        const double inc = sourceRate / juce::jmax(1.0, preparedSampleRate);
        const bool loopEnabled = importPreviewLoopEnabled.load(std::memory_order_acquire);
        const double loopStart = juce::jlimit(0.0, double(n > 0 ? n - 1 : 0),
            importPreviewLoopStartSeconds.load(std::memory_order_relaxed) * sourceRate);
        const double loopEnd = juce::jlimit(loopStart, double(n),
            importPreviewLoopEndSeconds.load(std::memory_order_relaxed) * sourceRate);
        const bool validLoop = loopEnabled && loopEnd > loopStart + 1.0;
        auto* outL = buffer.getWritePointer(0);
        auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (validLoop && importPreviewPosition >= loopEnd)
                importPreviewPosition = loopStart;
            if (importPreviewPosition >= n - 1)
            {
                if (validLoop)
                    importPreviewPosition = loopStart;
                else
                {
                    importPreviewPosition = juce::jmax(0.0, double(n - 1));
                    importPreviewPlaying = false;
                    importPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
                    break;
                }
            }
            const int a = juce::jlimit(0, n - 1, int(importPreviewPosition));
            const int b = juce::jmin(n - 1, a + 1);
            const float frac = float(importPreviewPosition - a);
            auto read = [&](int ch) noexcept
            {
                const auto* d = src.getReadPointer(juce::jmin(ch, channels - 1));
                return d[a] + (d[b] - d[a]) * frac;
            };
            const float l = read(0), r = channels > 1 ? read(1) : l;
            outL[i] += l; outR[i] += r;
            importPreviewPosition += inc;
        }
        importPreviewPositionSeconds.store(importPreviewPosition / juce::jmax(1.0, sourceRate), std::memory_order_relaxed);
    }

    applyOutputStage(buffer, routes);
    // Tap the final main stereo bus without changing a sample or DSP state.
    for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel) {
        const float peak = buffer.getMagnitude(channel, 0, buffer.getNumSamples());
        auto& meter = visualPeaks[size_t(channel)];
        float old = meter.load(std::memory_order_relaxed);
        while (peak > old && !meter.compare_exchange_weak(old, peak, std::memory_order_relaxed)) {}
    }
    midi.clear();
    if (previewPlaying && !voicePool.hasPreviewVoices(previewPlayingSlot)) { previewPlaying = false; previewPlayingSlot = -1; previewPlayingSampleIndex = -1; }
}

juce::AudioProcessorEditor* LSampler24AudioProcessor::createEditor()
{
    return new LSampler24AudioProcessorEditor(*this);
}

void LSampler24AudioProcessor::setCurrentSlot(int slotIndex)
{
    currentSlot = juce::jlimit(0, slotCount - 1, slotIndex);
    uiRevision.fetch_add(1, std::memory_order_release);
}

juce::String LSampler24AudioProcessor::getSlotLabel(int slotIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const auto& s = slots[static_cast<size_t>(slotIndex)];
    auto name = s.slotName.isNotEmpty() ? s.slotName : s.sampleFile.getFileNameWithoutExtension();
    if (name.isEmpty()) for (const auto& f : s.alternateSampleFiles) if (f.getFullPathName().isNotEmpty()) { name = f.getFileNameWithoutExtension(); break; }
    return "Slot " + juce::String(slotIndex + 1) + ", " + (name.isNotEmpty() ? name : "empty");
}

juce::String LSampler24AudioProcessor::getSlotName(int slotIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const auto& s = slots[static_cast<size_t>(slotIndex)];
    if (s.slotName.isNotEmpty()) return s.slotName;
    if (s.sampleFile.getFullPathName().isNotEmpty()) return s.sampleFile.getFileNameWithoutExtension();
    for (const auto& f : s.alternateSampleFiles) if (f.getFullPathName().isNotEmpty()) return f.getFileNameWithoutExtension();
    return {};
}

void LSampler24AudioProcessor::setSlotName(int slotIndex, const juce::String& name)
{
    uiRevision.fetch_add(1, std::memory_order_release);
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const auto clean = name.trim();
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    slots[static_cast<size_t>(slotIndex)].slotName = clean;
}

bool LSampler24AudioProcessor::loadSample(const juce::File& file, juce::String& error)
{
    return loadSampleToSlot(file, getCurrentSlot(), error);
}

bool LSampler24AudioProcessor::loadSampleToSlot(const juce::File& file, int slotIndex, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    auto loaded = lsampler::loadSampleWithProgress(file, error,
        isFileTaskRunning() ? lsampler::SampleLoadProgress([this](double fraction) {
            setFileTaskProgress(fraction);
        }) : lsampler::SampleLoadProgress{});
    if (!loaded)
    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        slots[static_cast<size_t>(slotIndex)].status = error;
        return false;
    }

    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        auto& s = slots[static_cast<size_t>(slotIndex)];
        s.slice = SliceState{};
        s.sample = std::move(loaded);
        s.sampleFile = file;
        s.alternateSamples = {};
        s.alternateSampleFiles = {};
        s.sampleVelocityLow.fill(1);
        s.sampleVelocityHigh.fill(127);
        s.variationMode = variationOff;
        s.slotName = file.getFileNameWithoutExtension();
        s.presetFile = {};
        s.sampleAudioModified = false;
        s.status = "Loaded: " + file.getFileName();
        updateThresholdWindow(slotIndex);
        markAudioStateDirty();
    }
    return true;
}


juce::String LSampler24AudioProcessor::variationModeName(int mode)
{
    switch (juce::jlimit<int>(variationOff, variationShuffleNoRepeat, mode))
    {
        case variationRoundRobin: return "Round Robin";
        case variationRandom: return "Random";
        case variationRandomNoRepeat: return "Random No Repeat";
        case variationShuffleNoRepeat: return "Shuffle No Repeat";
        default: return "Off";
    }
}

bool LSampler24AudioProcessor::loadSampleSetEntryToSlot(const juce::File& file, int slotIndex, int sampleIndex, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    sampleIndex = juce::jlimit(0, sampleSetSize - 1, sampleIndex);
    auto loaded = lsampler::loadSampleWithProgress(file, error,
        isFileTaskRunning() ? lsampler::SampleLoadProgress([this](double fraction) {
            setFileTaskProgress(fraction);
        }) : lsampler::SampleLoadProgress{});
    if (!loaded) return false;

    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& slot = slots[size_t(slotIndex)];
    if (sampleIndex == 0)
    {
        slot.sample = std::move(loaded);
        slot.sampleFile = file;
        slot.sampleAudioModified = false;
        slot.slice = SliceState{};
        slot.slotName = slot.slotName.isNotEmpty() ? slot.slotName : file.getFileNameWithoutExtension();
        updateThresholdWindow(slotIndex);
    }
    else
    {
        slot.alternateSamples[size_t(sampleIndex - 1)] = std::move(loaded);
        slot.alternateSampleFiles[size_t(sampleIndex - 1)] = file;
        if (slot.slotName.isEmpty() && !slot.sample) slot.slotName = file.getFileNameWithoutExtension();
    }
    slot.presetFile = {};
    slot.status = "Loaded Sample " + juce::String(sampleIndex + 1) + ": " + file.getFileName();
    markAudioStateDirty();
    return true;
}


bool LSampler24AudioProcessor::loadSampleSetEntryFromSlotPreset(const juce::File& presetFile, int slotIndex, int sampleIndex, juce::String& error)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }

    auto reference = tree.getProperty("sampleReference").toString();
    auto hash = tree.getProperty("sampleHash").toString();
    if (reference.isEmpty())
        reference = tree.getProperty("samplePath").toString();

    if (reference.isEmpty())
    {
        const auto sampleSet = tree.getChildWithName("SampleSet");
        if (sampleSet.isValid())
            for (int i = 0; i < sampleSet.getNumChildren(); ++i)
            {
                const auto entry = sampleSet.getChild(i);
                if (int(entry.getProperty("index", i)) != 0) continue;
                reference = entry.getProperty("sampleReference").toString();
                hash = entry.getProperty("sampleHash").toString();
                break;
            }
    }

    if (reference.isEmpty())
    {
        error = "Selected slot has no sample";
        return false;
    }

    const auto sampleFile = library.resolveSampleReference(reference, hash);
    if (!sampleFile.existsAsFile())
    {
        error = "Sample missing: " + juce::File(reference).getFileName();
        return false;
    }

    return loadSampleSetEntryToSlot(sampleFile, slotIndex, sampleIndex, error);
}

void LSampler24AudioProcessor::clearSampleSetEntry(int slotIndex, int sampleIndex)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    sampleIndex = juce::jlimit(0, sampleSetSize - 1, sampleIndex);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& slot = slots[size_t(slotIndex)];
    if (sampleIndex == 0)
    {
        slot.sample.reset();
        slot.sampleFile = {};
        slot.sampleAudioModified = false;
        slot.thresholdStartFrame = slot.thresholdEndFrame = 0;
        slot.slice = SliceState{};
    }
    else
    {
        slot.alternateSamples[size_t(sampleIndex - 1)].reset();
        slot.alternateSampleFiles[size_t(sampleIndex - 1)] = {};
    }
    slot.presetFile = {};
    slot.status = "Sample " + juce::String(sampleIndex + 1) + " cleared";
    markAudioStateDirty();
    stopVoicesMask.fetch_or(1u << slotIndex, std::memory_order_release);
}

LSampler24AudioProcessor::SampleSetEntryInfo LSampler24AudioProcessor::getSampleSetEntry(int slotIndex, int sampleIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    sampleIndex = juce::jlimit(0, sampleSetSize - 1, sampleIndex);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const auto& slot = slots[size_t(slotIndex)];
    SampleSetEntryInfo info;
    if (sampleIndex == 0)
    {
        info.loaded = slot.sample != nullptr;
        info.file = slot.sampleFile;
        info.name = slot.sampleFile.getFileNameWithoutExtension();
        if (info.name.isEmpty() && slot.sample) info.name = slot.sample->sourceFile.getFileNameWithoutExtension();
    }
    else
    {
        info.loaded = slot.alternateSamples[size_t(sampleIndex - 1)] != nullptr;
        info.file = slot.alternateSampleFiles[size_t(sampleIndex - 1)];
        info.name = info.file.getFileNameWithoutExtension();
        if (info.name.isEmpty() && slot.alternateSamples[size_t(sampleIndex - 1)])
            info.name = slot.alternateSamples[size_t(sampleIndex - 1)]->sourceFile.getFileNameWithoutExtension();
    }
    info.velocityLow = slot.sampleVelocityLow[size_t(sampleIndex)];
    info.velocityHigh = slot.sampleVelocityHigh[size_t(sampleIndex)];
    return info;
}

juce::File LSampler24AudioProcessor::getSampleSetEntryFile(int slotIndex, int sampleIndex) const
{
    return getSampleSetEntry(slotIndex, sampleIndex).file;
}

void LSampler24AudioProcessor::setSampleSetVelocityRange(int slotIndex, int sampleIndex, int low, int high)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    sampleIndex = juce::jlimit(0, sampleSetSize - 1, sampleIndex);
    low = juce::jlimit(1, 127, low);
    high = juce::jlimit(1, 127, high);
    if (low > high) std::swap(low, high);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& slot = slots[size_t(slotIndex)];
    slot.sampleVelocityLow[size_t(sampleIndex)] = low;
    slot.sampleVelocityHigh[size_t(sampleIndex)] = high;
    slot.presetFile = {};
    markAudioStateDirty();
}

int LSampler24AudioProcessor::getVariationMode(int slotIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[size_t(slotIndex)].variationMode;
}

void LSampler24AudioProcessor::setVariationMode(int slotIndex, int mode)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    mode = juce::jlimit<int>(variationOff, variationShuffleNoRepeat, mode);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& slot = slots[size_t(slotIndex)];
    slot.variationMode = mode;
    slot.presetFile = {};
    markAudioStateDirty();
}

bool LSampler24AudioProcessor::isSlotOccupied(int slotIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const auto& slot = slots[static_cast<size_t>(slotIndex)];
    if (slot.sample != nullptr) return true;
    return std::any_of(slot.alternateSamples.begin(), slot.alternateSamples.end(), [](const auto& sample) { return sample != nullptr; });
}

bool LSampler24AudioProcessor::importSampleToSlot(const juce::File& file, int slotIndex, double startSeconds, double endSeconds, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded) return false;

    const double sourceRate = loaded->sourceSampleRate > 0.0 ? loaded->sourceSampleRate : preparedSampleRate;
    const double totalSeconds = loaded->audio.getNumSamples() / juce::jmax(1.0, sourceRate);
    const bool hasSlice = endSeconds > startSeconds + 0.0005 && totalSeconds > 0.0;

    if (hasSlice)
    {
        startSeconds = juce::jlimit(0.0, totalSeconds, startSeconds);
        endSeconds = juce::jlimit(startSeconds + 0.0005, totalSeconds, endSeconds);

        const int totalSamples = loaded->audio.getNumSamples();
        const int firstSample = juce::jlimit(0, totalSamples - 2,
            static_cast<int>(std::floor(startSeconds * sourceRate)));
        const int lastSample = juce::jlimit(firstSample + 1, totalSamples,
            static_cast<int>(std::ceil(endSeconds * sourceRate)));
        const int sliceSamples = lastSample - firstSample;

        auto sliced = std::make_shared<SharedSample>();
        sliced->audio.setSize(loaded->audio.getNumChannels(), sliceSamples, false, false, true);
        for (int ch = 0; ch < loaded->audio.getNumChannels(); ++ch)
            sliced->audio.copyFrom(ch, 0, loaded->audio, ch, firstSample, sliceSamples);
        sliced->sourceSampleRate = loaded->sourceSampleRate;
        sliced->sourceFile = loaded->sourceFile;

        double peak = 0.0;
        std::array<double, 2> dc {};
        for (int ch = 0; ch < sliced->audio.getNumChannels(); ++ch)
        {
            const auto* data = sliced->audio.getReadPointer(ch);
            double sum = 0.0;
            for (int i = 0; i < sliceSamples; ++i)
            {
                peak = juce::jmax(peak, std::abs(double(data[i])));
                sum += data[i];
            }
            if (ch < 2) dc[size_t(ch)] = sliceSamples > 0 ? sum / sliceSamples : 0.0;
        }
        if (sliced->audio.getNumChannels() == 1) dc[1] = dc[0];
        sliced->peak = peak;
        sliced->dc = dc;
        loaded = std::move(sliced);
    }

    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        auto& slot = slots[static_cast<size_t>(slotIndex)];
        slot.sample = std::move(loaded);
        slot.sampleFile = file;
        slot.alternateSamples = {};
        slot.alternateSampleFiles = {};
        slot.sampleVelocityLow.fill(1);
        slot.sampleVelocityHigh.fill(127);
        slot.variationMode = variationOff;
        slot.slotName = file.getFileNameWithoutExtension();
        slot.presetFile = {};
        slot.sampleAudioModified = hasSlice;
        slot.slice = SliceState{};
        slot.parameters = lsampler::SlotParameters{};
        slot.parameters[lsampler::P::root] = 60.0;
        slot.parameters[lsampler::P::low] = 0.0;
        slot.parameters[lsampler::P::high] = 127.0;
        // Alt+O slices are already real trimmed audio when they reach a slot.
        // Slot editing therefore always begins from a clean full 0..100 % window.
        slot.parameters[lsampler::P::sample_start] = 0.0;
        slot.parameters[lsampler::P::sample_end] = 100.0;
        slot.parameters[lsampler::P::sample_play_start] = 0.0;
        slot.status = "Loaded: " + file.getFileName();
        updateThresholdWindow(slotIndex);
        markAudioStateDirty();
    }
    return true;
}

bool LSampler24AudioProcessor::prepareImportPreview(const juce::File& file, juce::String& error)
{
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded) return false;
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& target = importPreviewSnapshots[static_cast<size_t>(importPreviewWriter)];
    target.owner = std::move(loaded);
    target.sample = target.owner.get();
    target.revision = ++importPreviewRevision;
    const double rate = target.sample->sourceSampleRate > 0.0 ? target.sample->sourceSampleRate : preparedSampleRate;
    importPreviewLengthSeconds.store(target.sample->audio.getNumSamples() / juce::jmax(1.0, rate), std::memory_order_relaxed);
    importPreviewWriter = importPreviewMiddle.exchange(importPreviewWriter | 4, std::memory_order_acq_rel) & 3;
    importPreviewSeekSeconds.store(0.0, std::memory_order_relaxed);
    importPreviewSeekRequested.store(true, std::memory_order_release);
    importPreviewLoopEnabled.store(false, std::memory_order_release);
    importPreviewStopRequested.store(true, std::memory_order_release);
    return true;
}

int LSampler24AudioProcessor::getLowKey() const noexcept { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); return int(slots[size_t(currentSlot.load())].parameters[P::low]); }
int LSampler24AudioProcessor::getHighKey() const noexcept { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); return int(slots[size_t(currentSlot.load())].parameters[P::high]); }
int LSampler24AudioProcessor::getRootNote() const noexcept { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); return int(slots[size_t(currentSlot.load())].parameters[P::root]); }
float LSampler24AudioProcessor::getVolume() const noexcept { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); return float(std::pow(10.0,slots[size_t(currentSlot.load())].parameters[P::input_gain]/20.0)); }
void LSampler24AudioProcessor::setLowKey(int n) { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); slots[size_t(currentSlot.load())].parameters[P::low]=juce::jlimit(0,127,n); markAudioStateDirty(); }
void LSampler24AudioProcessor::setHighKey(int n) { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); slots[size_t(currentSlot.load())].parameters[P::high]=juce::jlimit(0,127,n); markAudioStateDirty(); }
void LSampler24AudioProcessor::setRootNote(int n) {
    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        slots[size_t(currentSlot.load())].parameters[P::root]=juce::jlimit(0,127,n);
        markAudioStateDirty();
    }
    requestPreviewRestartIfPlaying();
}
void LSampler24AudioProcessor::setVolume(float v) { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); slots[size_t(currentSlot.load())].parameters[P::input_gain]=v>0?20*std::log10(juce::jlimit(.000001f,1.0f,v)):-120; markAudioStateDirty(); }

double LSampler24AudioProcessor::getGlobalOutputParameter(lsampler::GlobalP parameter) const noexcept
{
    return hostParametersReady ? hostGlobals[size_t(parameter)]->load()
        : globalOutputParameters[size_t(parameter)].load(std::memory_order_relaxed);
}

void LSampler24AudioProcessor::setGlobalOutputParameter(lsampler::GlobalP parameter, double value) noexcept
{
    const auto& d = lsampler::globalParameters[size_t(parameter)];
    if (hostParametersReady) {
        auto* host = hostGlobals[size_t(parameter)];
        value = lsampler::sanitise(d,value);
        host->storeReal(value);
        notifyHostControl(host);
    }
    globalOutputParameters[size_t(parameter)].store(lsampler::sanitise(d, value), std::memory_order_relaxed);
    uiRevision.fetch_add(1, std::memory_order_release);
}

double LSampler24AudioProcessor::getSlotParameter(int index, int loop) const {
    const auto& e = grid[size_t(juce::jlimit(0,int(grid.size())-1,index))];
    if (e.global >= 0)
        return getGlobalOutputParameter(static_cast<GlobalP>(e.global));
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[size_t(currentSlot.load())].parameters.get(e,juce::jlimit(0,9,loop));
}
void LSampler24AudioProcessor::setSlotParameter(int index,double value,int loop) {
    const auto& e=grid[size_t(juce::jlimit(0,int(grid.size())-1,index))];
    if (e.action!=Action::none) { if(value>=.5)applyZeroCrossing(e.action==Action::loopZero,loop);return; }
    if (e.global >= 0) {
        setGlobalOutputParameter(static_cast<GlobalP>(e.global), value);
        return;
    }
    const bool rootChanged = e.parameter == int(P::root);
    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        auto& p=slots[size_t(currentSlot.load())].parameters;
        p.set(e,juce::jlimit(0,9,loop),value);
        if (e.parameter == int(P::sample_start) || e.parameter == int(P::sample_end))
            p[P::sample_play_start] = juce::jlimit(p[P::sample_start], std::max(p[P::sample_start], p[P::sample_end]), p[P::sample_play_start]);
        if (e.parameter == int(P::sample_start) || e.parameter == int(P::sample_end)
            || e.parameter == int(P::start_threshold) || e.parameter == int(P::end_threshold))
            updateThresholdWindow(currentSlot.load());
        for (int i=0;i<2;++i) {
            const P sync=i?P::lfo2_bpm_sync:P::lfo1_bpm_sync, rate=i?P::lfo2_rate:P::lfo1_rate;
            if (p[sync]!=0 && (e.parameter==int(sync)||e.parameter==int(rate)))
                p[rate]=std::clamp(std::round(p[rate]*8)/8,.125,512.0);
        }
        markAudioStateDirty();
    }
    if (rootChanged) requestPreviewRestartIfPlaying();
}
void LSampler24AudioProcessor::resetSlotParameter(int index,int loop) {
    if (index<0 || index>=int(grid.size())) return;
    setSlotParameter(index,descriptor(grid[size_t(index)]).initial,loop);
}
double LSampler24AudioProcessor::getSamplePlayStart() const {
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[size_t(currentSlot.load())].parameters[P::sample_play_start];
}
double LSampler24AudioProcessor::getSampleWindowStart() const {
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[size_t(currentSlot.load())].parameters[P::sample_start];
}
double LSampler24AudioProcessor::getSampleWindowEnd() const {
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[size_t(currentSlot.load())].parameters[P::sample_end];
}
void LSampler24AudioProcessor::setSamplePlayStart(double value) {
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& p = slots[size_t(currentSlot.load())].parameters;
    // Sample Play Start is an absolute position in the source sample.  The
    // editable Sample Start/End window is therefore also its legal scrub range.
    p[P::sample_play_start] = juce::jlimit(p[P::sample_start], std::max(p[P::sample_start], p[P::sample_end]), value);
    markAudioStateDirty();
}
void LSampler24AudioProcessor::requestSampleBoundaryAudition(bool endBoundary, bool latchPlayStart)
{
    double startPercent = 0.0;
    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        auto& slot = slots[size_t(currentSlot.load())];
        if (!slot.sample || slot.sample->audio.getNumSamples() < 2)
            return;

        auto& p = slot.parameters;
        const int total = slot.sample->audio.getNumSamples();
        if (!endBoundary) {
            // Always latch Start audition to the newly edited Start. If Start Threshold
            // moves the effective playback start later, the voice clamps to that frame.
            startPercent = p[P::sample_start];
        } else {
            const int effectiveStart = juce::jlimit(0, total - 2, slot.thresholdStartFrame);
            const int effectiveEnd = juce::jlimit(effectiveStart + 1, total, slot.thresholdEndFrame);
            const double sourceRate = slot.sample->sourceSampleRate > 1.0 ? slot.sample->sourceSampleRate : preparedSampleRate;
            const int choice = juce::jlimit(0, 12, juce::roundToInt(p[P::end_preview_length]));
            static constexpr double fixedMs[] { 0, 50, 100, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 4000, 5000 };
            double previewMs = fixedMs[choice];
            if (choice == 0) {
                const double activeMs = 1000.0 * (effectiveEnd - effectiveStart) / juce::jmax(1.0, sourceRate);
                previewMs = juce::jlimit(100.0, 2000.0, activeMs * 0.20);
            }
            const int tailFrames = juce::jmax(1, int(std::round(previewMs * .001 * sourceRate)));
            const int previewStartFrame = juce::jmax(effectiveStart, effectiveEnd - tailFrames);
            startPercent = 100.0 * previewStartFrame / juce::jmax(1, total);
        }
        // Only direct Sample Start/End edits move the persistent manual scrub
        // anchor. Threshold and End Preview Length changes audition the result
        // but must not silently relocate Ctrl+Left/Right.
        if(latchPlayStart)
            p[P::sample_play_start] = juce::jlimit(p[P::sample_start], std::max(p[P::sample_start], p[P::sample_end]), startPercent);
    }
    if(latchPlayStart) markAudioStateDirty();
    requestPreviewAuditionFromPercent(startPercent);
}

void LSampler24AudioProcessor::applyZeroCrossing(bool loopWindow,int loopIndex) {
    // A native UI command: no audio-thread scan, bridge, pulse or request/result state.
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& slot=slots[size_t(currentSlot.load())]; if(!slot.sample || slot.sample->audio.getNumSamples()<2)return;
    auto& p=slot.parameters; const auto& a=slot.sample->audio;
    const int count=a.getNumSamples();
    const int first=loopWindow?std::clamp(int(count*p[P::sample_start]*.01),0,count-2):0;
    const int last=loopWindow?std::clamp(int(count*p[P::sample_end]*.01),first+1,count):count;
    auto snap=[&](double pct) {
        const int target=std::clamp(first+int(std::round((last-first)*pct*.01)),first,last-1);
        auto crosses=[&](int at) {return sampleCrossesZero(a,at,first,last);};
        int found=target;
        for(int distance=0;distance<last-first;++distance) {
            if(crosses(target-distance)){found=target-distance;break;}
            if(crosses(target+distance)){found=target+distance;break;}
        }
        return 100.0*(found-first)/(last-first);
    };
    if(loopWindow) {
        auto& l=p.loops[size_t(juce::jlimit(0,9,loopIndex))];
        l[size_t(L::start)]=snap(l[size_t(L::start)]);l[size_t(L::end)]=snap(l[size_t(L::end)]);
    } else {
        // Sample-window Zero Crossing is a destructive trim command. Snap the
        // selected window to zero crossings, keep only that audio, then make
        // the resulting sample the new full 0..100 % window.
        const double snappedStart = snap(p[P::sample_start]);
        const double snappedEnd = snap(p[P::sample_end]);
        const int startSample = std::clamp(int(std::floor(count * snappedStart * .01)), 0, count - 2);
        const int endSample = std::clamp(int(std::ceil(count * snappedEnd * .01)), startSample + 1, count);
        const int newCount = endSample - startSample;

        auto trimmed = std::make_shared<SharedSample>();
        trimmed->audio.setSize(a.getNumChannels(), newCount, false, false, true);
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            trimmed->audio.copyFrom(ch, 0, a, ch, startSample, newCount);
        trimmed->sourceSampleRate = slot.sample->sourceSampleRate;
        trimmed->sourceFile = slot.sample->sourceFile;

        double peak = 0.0;
        std::array<double, 2> dc {};
        for (int ch = 0; ch < trimmed->audio.getNumChannels(); ++ch) {
            const auto* data = trimmed->audio.getReadPointer(ch);
            double sum = 0.0;
            for (int i = 0; i < newCount; ++i) {
                peak = juce::jmax(peak, std::abs(double(data[i])));
                sum += data[i];
            }
            if (ch < 2) dc[size_t(ch)] = newCount > 0 ? sum / newCount : 0.0;
        }
        trimmed->peak = peak;
        trimmed->dc = dc;
        // Rebase existing internal boundaries to the newly trimmed buffer.
        const double oldStart=std::clamp(int(std::floor(count*p[P::sample_start]*.01)),0,count-2);
        const double oldLength=std::clamp(int(std::floor(count*p[P::sample_end]*.01)),int(oldStart)+1,count)-oldStart;
        for(int i=1;i<slot.slice.division();++i)
            slot.slice.boundaries[size_t(i)]=(oldStart+oldLength*slot.slice.boundaries[size_t(i)]-startSample)/newCount;
        slot.slice.sanitise();
        slot.sample = std::move(trimmed);
        slot.sampleAudioModified = true;
        p[P::sample_start] = 0.0;
        p[P::sample_end] = 100.0;
        p[P::sample_play_start] = 0.0;
        updateThresholdWindow(currentSlot.load());
    }
    markAudioStateDirty();
}

bool LSampler24AudioProcessor::copyCurrentSlot()
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    slotClipboard = slots[static_cast<size_t>(currentSlot.load())];
    slotClipboardHasData = true;
    return true;
}

bool LSampler24AudioProcessor::cutCurrentSlot()
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    slotClipboard = slots[static_cast<size_t>(currentSlot.load())];
    slotClipboardHasData = true;
    slots[static_cast<size_t>(currentSlot.load())] = SlotState{};
    markAudioStateDirty();
    stopVoicesMask.fetch_or(1u << currentSlot.load(std::memory_order_relaxed), std::memory_order_release);
    return true;
}

bool LSampler24AudioProcessor::pasteCurrentSlot()
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    if (!slotClipboardHasData)
        return false;
    slots[static_cast<size_t>(currentSlot.load())] = slotClipboard;
    markAudioStateDirty();
    stopVoicesMask.fetch_or(1u << currentSlot.load(std::memory_order_relaxed), std::memory_order_release);
    return true;
}

void LSampler24AudioProcessor::clearCurrentSlot()
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    slots[static_cast<size_t>(currentSlot.load())] = SlotState{};
    markAudioStateDirty();
    stopVoicesMask.fetch_or(1u << currentSlot.load(std::memory_order_relaxed), std::memory_order_release);
}

void LSampler24AudioProcessor::clearBank()
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    for (auto& slot : slots)
        slot = SlotState{};
    currentBankName.clear();
    markAudioStateDirty();
    stopVoicesMask.fetch_or(0xffffffu, std::memory_order_release);
}

LSampler24AudioProcessor::BankMacroSnapshot LSampler24AudioProcessor::getBankMacroSnapshot() const
{
    BankMacroSnapshot result;
    const juce::ScopedLock lock(stateLock);
    absorbHostValuesLocked();
    for (int slot = 0; slot < slotCount; ++slot)
    {
        const auto& source = slots[size_t(slot)];
        bool occupied = source.sample != nullptr;
        for (const auto& other : source.alternateSamples)
            occupied = occupied || other != nullptr;
        result.occupied[size_t(slot)] = occupied;
        if (occupied)
            result.values[size_t(slot)] = source.parameters.values;
    }
    return result;
}

void LSampler24AudioProcessor::applyBankMacro(int gridIndex, const BankMacroSnapshot& reference,
                                              double value, bool uniform)
{
    if (!juce::isPositiveAndBelow(gridIndex, int(grid.size()))) return;
    const auto& entry = grid[size_t(gridIndex)];
    // Only ordinary per-slot parameters are eligible; never modify sample
    // boundaries, loops, actions, globals, or the MIDI key/velocity layout.
    if (entry.parameter < 0 || entry.loop >= 0 || entry.global >= 0 || entry.action != Action::none)
        return;
    const auto& d = descriptor(entry);
    const juce::ScopedLock lock(stateLock);
    absorbHostValuesLocked();
    bool changed = false;
    for (int slot = 0; slot < slotCount; ++slot)
    {
        if (!reference.occupied[size_t(slot)]) continue;
        auto& parameters = slots[size_t(slot)].parameters;
        const double original = reference.values[size_t(slot)][size_t(entry.parameter)];
        const double next = sanitise(d, uniform ? value : original + value);
        if (parameters.values[size_t(entry.parameter)] != next)
        {
            parameters.set(entry, 0, next);
            // Preserve the existing BPM-sync quantisation contract.
            for (int i = 0; i < 2; ++i)
            {
                const P sync = i ? P::lfo2_bpm_sync : P::lfo1_bpm_sync;
                const P rate = i ? P::lfo2_rate : P::lfo1_rate;
                if (parameters[sync] != 0 &&
                    (entry.parameter == int(sync) || entry.parameter == int(rate)))
                    parameters[rate] = std::clamp(std::round(parameters[rate] * 8.0) / 8.0, .125, 512.0);
            }
            changed = true;
        }
    }
    if (changed)
        markAudioStateDirty(); // one snapshot and host publication for all 24 slots
}

double LSampler24AudioProcessor::getBankMacroOffset(int parameter) const noexcept
{
    if (!juce::isPositiveAndBelow(parameter, lsampler::parameterCount)) return 0.0;
    if (auto* p = hostBankMacros[size_t(parameter)]) return p->load();
    return 0.0;
}

void LSampler24AudioProcessor::setBankMacroOffset(int parameter, double offset) noexcept
{
    if (!juce::isPositiveAndBelow(parameter, lsampler::parameterCount)) return;
    if (auto* p = hostBankMacros[size_t(parameter)]) {
        // This method is called by the editor, not by an audio callback.
        if (p->load() != offset) {
            p->storeReal(offset);
            notifyHostControl(p);
        }
    }
}

void LSampler24AudioProcessor::saveBankMacroState(juce::ValueTree& parent) const
{
    juce::ValueTree macros("BankMacros");
    for (int i = 0; i < lsampler::parameterCount; ++i)
        if (auto* parameter = hostBankMacros[size_t(i)])
            macros.setProperty(lsampler::parameters[size_t(i)].key, parameter->load(), nullptr);
    parent.addChild(macros, -1, nullptr); // after all 24 Slot children
}

void LSampler24AudioProcessor::restoreBankMacroState(const juce::ValueTree& parent)
{
    const auto macros = parent.getChildWithName("BankMacros");
    // Old bank and session formats have no macros: reset to neutral, rather
    // than leaking the offsets from whichever bank was loaded previously.
    for (int i = 0; i < lsampler::parameterCount; ++i)
        if (auto* parameter = hostBankMacros[size_t(i)]) {
            const auto key = lsampler::parameters[size_t(i)].key;
            parameter->storeReal(macros.isValid() && macros.hasProperty(key)
                ? double(macros.getProperty(key)) : 0.0);
        }
}

bool LSampler24AudioProcessor::slotHasSample(int slotIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const auto& slot = slots[static_cast<size_t>(slotIndex)];
    if (slot.sample) return true;
    for (const auto& sample : slot.alternateSamples) if (sample) return true;
    return false;
}

int LSampler24AudioProcessor::getSlotGridPosition(int /*slotIndex*/) const noexcept
{
    // The parameter-grid cursor is an editor-wide position, not a per-slot
    // property.  Changing Slot therefore keeps the same parameter selected.
    return slotGridPosition.load(std::memory_order_relaxed);
}

void LSampler24AudioProcessor::setSlotGridPosition(int /*slotIndex*/, int gridIndex) noexcept
{
    slotGridPosition.store(juce::jmax(0, gridIndex), std::memory_order_relaxed);
}

juce::File LSampler24AudioProcessor::getCurrentSampleFile() const
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[static_cast<size_t>(currentSlot.load())].sampleFile;
}

juce::File LSampler24AudioProcessor::getCurrentSlotPresetFile() const
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[static_cast<size_t>(currentSlot.load())].presetFile;
}

juce::String LSampler24AudioProcessor::getCurrentSamplePropertiesText() const
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const int slotIndex = juce::jlimit(0, slotCount - 1, currentSlot.load());
    const auto& slot = slots[size_t(slotIndex)];
    if (!slot.sample)
        return "Slot: " + juce::String(slotIndex + 1) + "\nSample: Empty";

    const auto& sample = *slot.sample;
    const auto file = slot.sampleFile.existsAsFile() ? slot.sampleFile : sample.sourceFile;
    const int frames = sample.audio.getNumSamples();
    const int channels = sample.audio.getNumChannels();
    const double rate = sample.sourceSampleRate > 0.0 ? sample.sourceSampleRate : preparedSampleRate;
    const double seconds = frames / juce::jmax(1.0, rate);
    const double peakDb = juce::Decibels::gainToDecibels(juce::jmax(1.0e-12, sample.peak), -120.0);
    const double startPercent = slot.parameters[lsampler::P::sample_start];
    const double endPercent = slot.parameters[lsampler::P::sample_end];
    const double effectiveStartMs = 1000.0 * slot.thresholdStartFrame / juce::jmax(1.0, rate);
    const double effectiveEndMs = 1000.0 * slot.thresholdEndFrame / juce::jmax(1.0, rate);

    juce::String out;
    out << "Name: " << (slot.slotName.isNotEmpty() ? slot.slotName : file.getFileNameWithoutExtension()) << "\n";
    out << "Audio file: " << (file.existsAsFile() ? file.getFileName() : juce::String("RAM only")) << "\n";
    out << "Format: " << (file.existsAsFile() ? file.getFileExtension().trimCharactersAtStart(".").toUpperCase() : juce::String("RAM")) << "\n";
    out << "Sample rate: " << juce::String(rate, 0) << " Hz\n";
    out << "Channels: " << channels << (channels == 1 ? " Mono" : channels == 2 ? " Stereo" : "") << "\n";
    out << "Frames: " << frames << "\n";
    out << "Duration: " << juce::String(seconds, 3) << " seconds\n";
    if (file.existsAsFile()) out << "File size: " << juce::File::descriptionOfSizeInBytes(file.getSize()) << "\n";
    out << "Peak: " << juce::String(peakDb, 2) << " dBFS\n";
    out << "DC left: " << juce::String(sample.dc[0], 6) << "\n";
    if (channels > 1) out << "DC right: " << juce::String(sample.dc[1], 6) << "\n";
    out << "Sample Start: " << juce::String(startPercent, 3) << " percent\n";
    out << "Sample End: " << juce::String(endPercent, 3) << " percent\n";
    out << "Effective Start: frame " << slot.thresholdStartFrame << ", " << juce::String(effectiveStartMs, 3) << " ms\n";
    out << "Effective End: frame " << slot.thresholdEndFrame << ", " << juce::String(effectiveEndMs, 3) << " ms\n";
    out << "RAM audio modified: " << (slot.sampleAudioModified ? "Yes" : "No") << "\n";
    out << "Path: " << (file.existsAsFile() ? file.getFullPathName() : juce::String("Not available"));
    return out;
}

juce::String LSampler24AudioProcessor::getCurrentSlotPropertiesText() const
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const int slotIndex = juce::jlimit(0, slotCount - 1, currentSlot.load());
    const auto& slot = slots[size_t(slotIndex)];
    const auto preset = slot.presetFile;
    const int low = juce::roundToInt(slot.parameters[lsampler::P::low]);
    const int high = juce::roundToInt(slot.parameters[lsampler::P::high]);
    const int root = juce::roundToInt(slot.parameters[lsampler::P::root]);
    const int velLow = juce::roundToInt(slot.parameters[lsampler::P::velocity_low]);
    const int velHigh = juce::roundToInt(slot.parameters[lsampler::P::velocity_high]);
    const bool poly = slot.parameters[lsampler::P::polyphony] >= 0.5;
    const bool replace = slot.parameters[lsampler::P::same_note_replace] >= 0.5;
    const int route = juce::roundToInt(slot.parameters[lsampler::P::output_route]);
    const auto midiName = [](int note)
    {
        return juce::String(note) + " " + juce::MidiMessage::getMidiNoteName(note, true, true, 4);
    };
    const auto routeText = [route]()
    {
        if (route <= 0) return juce::String("Main 1/2");
        const int first = 1 + route * 2;
        return juce::String("Out ") + juce::String(first) + "/" + juce::String(first + 1);
    };

    juce::String out;
    out << "Slot: " << (slotIndex + 1) << "\n";
    out << "Slot name: " << (slot.slotName.isNotEmpty() ? slot.slotName : juce::String("Empty")) << "\n";
    out << "Voice mode: " << (poly ? "Poly" : "Mono") << "\n";
    out << "Same Note Replace: " << (replace ? "On" : "Off") << "\n";
    out << "Key range: " << midiName(low) << " to " << midiName(high) << "\n";
    out << "Original Pitch: " << midiName(root) << "\n";
    out << "Velocity range: " << velLow << " to " << velHigh << "\n";
    out << "Output: " << routeText() << "\n";
    out << "Slice division: " << slot.slice.division() << "\n";
    out << "Configuration file: " << (preset.existsAsFile() ? preset.getFileName() : juce::String("Unsaved / direct sample")) << "\n";
    out << "Configuration format: LSampler-24 Slot (.lsampler-24-s)\n";
    if (preset.existsAsFile())
    {
        out << "Configuration size: " << juce::File::descriptionOfSizeInBytes(preset.getSize()) << "\n";
        out << "Configuration modified: " << preset.getLastModificationTime().toString(true, true, true, true) << "\n";
        out << "Configuration path: " << preset.getFullPathName() << "\n";
    }
    else
    {
        out << "Configuration path: No .lsampler-24-s file associated\n";
    }
    const auto sampleFile = slot.sampleFile;
    out << "Sample reference: " << (sampleFile.existsAsFile() ? library.makeSampleReference(sampleFile) : juce::String("None"));
    return out;
}

juce::String LSampler24AudioProcessor::getSampleStatus() const
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[static_cast<size_t>(currentSlot.load())].status;
}

juce::ValueTree LSampler24AudioProcessor::makeSlotState(int slotIndex, const juce::String& type) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    SlotState s;
    { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); s = slots[static_cast<size_t>(slotIndex)]; }
    juce::ValueTree tree(type);
    tree.setProperty("format", "LSampler-24 Slot", nullptr);
    tree.setProperty("formatVersion", 5, nullptr);
    tree.setProperty("index", slotIndex, nullptr);
    tree.setProperty("slotName", s.slotName.isNotEmpty() ? s.slotName : s.sampleFile.getFileNameWithoutExtension(), nullptr);
    tree.setProperty("sampleReference", library.makeSampleReference(s.sampleFile), nullptr);
    tree.setProperty("sampleHash", library.makeSampleHash(s.sampleFile), nullptr);
    // Keep version-2 aliases for the existing state contract. Canonical values live in Parameters.
    tree.setProperty("lowKey", s.parameters[P::low], nullptr);
    tree.setProperty("highKey", s.parameters[P::high], nullptr);
    tree.setProperty("rootNote", s.parameters[P::root], nullptr);
    tree.setProperty("volume", std::pow(10.0,s.parameters[P::input_gain]/20.0), nullptr);
    juce::ValueTree values("Parameters");
    for(int i=0;i<parameterCount;++i) values.setProperty(parameters[size_t(i)].key,s.parameters.values[size_t(i)],nullptr);
    tree.addChild(values,-1,nullptr);
    juce::ValueTree loops("Loops");
    for(int j=0;j<loopCount;++j) {
        juce::ValueTree loop("Loop");loop.setProperty("index",j,nullptr);
        for(int i=0;i<loopParameterCount;++i) loop.setProperty(loopParameters[size_t(i)].key,s.parameters.loops[size_t(j)][size_t(i)],nullptr);
        loops.addChild(loop,-1,nullptr);
    }
    tree.addChild(loops,-1,nullptr);
    juce::ValueTree sampleSet("SampleSet");
    sampleSet.setProperty("variationMode", s.variationMode, nullptr);
    for(int sampleIndex=0;sampleIndex<sampleSetSize;++sampleIndex) {
        juce::ValueTree entry("Sample");
        entry.setProperty("index",sampleIndex,nullptr);
        entry.setProperty("velocityLow",s.sampleVelocityLow[size_t(sampleIndex)],nullptr);
        entry.setProperty("velocityHigh",s.sampleVelocityHigh[size_t(sampleIndex)],nullptr);
        const auto file=sampleIndex==0?s.sampleFile:s.alternateSampleFiles[size_t(sampleIndex-1)];
        const bool loaded=sampleIndex==0?bool(s.sample):bool(s.alternateSamples[size_t(sampleIndex-1)]);
        if(loaded||file.getFullPathName().isNotEmpty()) {
            entry.setProperty("sampleReference",library.makeSampleReference(file),nullptr);
            entry.setProperty("sampleHash",library.makeSampleHash(file),nullptr);
        }
        sampleSet.addChild(entry,-1,nullptr);
    }
    tree.addChild(sampleSet,-1,nullptr);
    tree.addChild(s.slice.toTree(),-1,nullptr);
    if(s.sampleAudioModified&&s.sample)tree.addChild(storeEditedAudio(*s.sample),-1,nullptr);
    return tree;
}

bool LSampler24AudioProcessor::restoreSlotState(int slotIndex, const juce::ValueTree& tree,
                                                juce::String& error, std::function<void(double)> progress)
{
    if (!tree.isValid())
    {
        error = "Invalid preset";
        return false;
    }

    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const int lowKey = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("lowKey", 0)));
    const int highKey = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("highKey", 127)));
    const int rootNote = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("rootNote", 60)));
    const float volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(tree.getProperty("volume", 1.0)));

    const auto sampleSetTree = tree.getChildWithName("SampleSet");
    const int unitCount = slotLoadUnits(tree);
    int completedUnits = 0;
    auto reportPart = [&](double fraction)
    {
        if (progress)
            progress(juce::jlimit(0.0, 1.0,
                (double(completedUnits) + juce::jlimit(0.0, 1.0, fraction)) / double(unitCount)));
    };
    auto finishPart = [&]() { ++completedUnits; reportPart(0.0); };
    reportPart(0.0);
    auto reference = tree.getProperty("sampleReference").toString();
    auto sampleHash = tree.getProperty("sampleHash").toString();
    if (reference.isEmpty())
        reference = tree.getProperty("samplePath").toString();
    if(reference.isEmpty()&&sampleSetTree.isValid())
        for(int i=0;i<sampleSetTree.getNumChildren();++i) {
            const auto entry=sampleSetTree.getChild(i);
            if(int(entry.getProperty("index",i))==0) {
                reference=entry.getProperty("sampleReference").toString();
                sampleHash=entry.getProperty("sampleHash").toString();
                break;
            }
        }

    const juce::File file = library.resolveSampleReference(reference, sampleHash);
    std::shared_ptr<SharedSample> loaded;
    juce::String statusText = "No sample loaded";
    bool ok = true;

    const auto editedAudio=tree.getChildWithName("EditedAudio");
    if(editedAudio.isValid()) {
        loaded=restoreEditedAudio(editedAudio,file,error);ok=bool(loaded);
        statusText=ok?"Loaded edited slot audio":error;
    }
    else if (file.getFullPathName().isNotEmpty())
    {
        if (!file.existsAsFile())
        {
            statusText = "Sample missing: " + file.getFileName();
            error = statusText;
            ok = false;
        }
        else
        {
            loaded = lsampler::loadSampleWithProgress(file, error,
                progress ? lsampler::SampleLoadProgress([&](double fraction) { reportPart(fraction); })
                         : lsampler::SampleLoadProgress{});
            if (!loaded)
            {
                statusText = error;
                ok = false;
            }
            else
            {
                statusText = "Loaded: " + file.getFileName();
            }
        }
    }
    finishPart();

    {
        SlotState slot;
        slot.parameters = SlotParameters{};
        slot.parameters[P::low] = lowKey;
        slot.parameters[P::high] = highKey;
        slot.parameters[P::root] = rootNote;
        slot.parameters[P::input_gain] = volume>0?20*std::log10(volume):-120;
        if(auto values=tree.getChildWithName("Parameters"); values.isValid())
            for(int i=0;i<parameterCount;++i) {
                const auto& d=parameters[size_t(i)];
                if(values.hasProperty(d.key)) slot.parameters.values[size_t(i)]=sanitise(d,double(values.getProperty(d.key)));
            }
        const auto loops=tree.getChildWithName("Loops");
        for(int j=0;j<loops.getNumChildren();++j) {
            const auto loop=loops.getChild(j);const int index=int(loop.getProperty("index",j));
            if(index<0||index>=loopCount)continue;
            for(int i=0;i<loopParameterCount;++i) {
                const auto& d=loopParameters[size_t(i)];
                slot.parameters.loops[size_t(index)][size_t(i)]=sanitise(d,double(loop.getProperty(d.key,d.initial)));
            }
        }
        slot.slice = SliceState::fromTree(tree.getChildWithName("Slice"));
        if(sampleSetTree.isValid()) {
            slot.variationMode=juce::jlimit<int>(variationOff,variationShuffleNoRepeat,int(sampleSetTree.getProperty("variationMode",variationOff)));
            for(int i=0;i<sampleSetTree.getNumChildren();++i) {
                const auto entry=sampleSetTree.getChild(i);
                const int sampleIndex=juce::jlimit(0,sampleSetSize-1,int(entry.getProperty("index",i)));
                int lo=juce::jlimit(1,127,int(entry.getProperty("velocityLow",1)));
                int hi=juce::jlimit(1,127,int(entry.getProperty("velocityHigh",127)));
                if(lo>hi)std::swap(lo,hi);
                slot.sampleVelocityLow[size_t(sampleIndex)]=lo;
                slot.sampleVelocityHigh[size_t(sampleIndex)]=hi;
                if(sampleIndex==0)continue;
                const auto ref=entry.getProperty("sampleReference").toString();
                const auto hash=entry.getProperty("sampleHash").toString();
                if(ref.isEmpty())continue;
                const auto altFile=library.resolveSampleReference(ref,hash);
                if(!altFile.existsAsFile()) {
                    if(error.isEmpty())error="Sample Set file missing: "+juce::File(ref).getFileName();
                    ok=false;finishPart();continue;
                }
                juce::String altError;
                auto alt=lsampler::loadSampleWithProgress(altFile,altError,
                    progress ? lsampler::SampleLoadProgress([&](double fraction) { reportPart(fraction); })
                             : lsampler::SampleLoadProgress{});
                if(!alt) {if(error.isEmpty())error=altError;ok=false;finishPart();continue;}
                slot.alternateSamples[size_t(sampleIndex-1)]=std::move(alt);
                slot.alternateSampleFiles[size_t(sampleIndex-1)]=altFile;
                finishPart();
            }
        }
        slot.sample = std::move(loaded);
        slot.sampleFile = file;
        slot.slotName = tree.getProperty("slotName").toString().trim();
        slot.sampleAudioModified = editedAudio.isValid()&&bool(slot.sample);
        slot.status = statusText;
        const auto window = lsampler::calculateThresholdWindow(slot.parameters, slot.sample.get());
        slot.thresholdStartFrame = window.start; slot.thresholdEndFrame = window.end;
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        slots[static_cast<size_t>(slotIndex)] = std::move(slot);
        const juce::ScopedValueSetter<bool> restoring(restoringHostState, true);
        // Force every restored value, even one equal to the previous control mirror.
        publishHostValuesLocked(false, slotIndex);
        markAudioStateDirty();
    }

    return ok;
}

bool LSampler24AudioProcessor::materialiseSlotSample(int slotIndex, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    juce::File source;
    std::shared_ptr<SharedSample> currentSample;
    bool audioModified = false;
    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        const auto& slot = slots[static_cast<size_t>(slotIndex)];
        source = slot.sampleFile;
        currentSample = slot.sample;
        audioModified = slot.sampleAudioModified;
    }

    if (!currentSample && source.getFullPathName().isEmpty())
        return true;

    // Destructive edits (Zero Crossing trim and Alt+O slices) live in the
    // in-memory buffer. Before a Slot preset is written, persist that exact
    // buffer as its own Library WAV so reloading cannot fall back to the
    // original untrimmed source file.
    if (audioModified && currentSample)
    {
        const auto baseName = source.getFileNameWithoutExtension();
        auto local = library.materialiseEditedSample(currentSample->audio,
                                                     currentSample->sourceSampleRate,
                                                     baseName, error);
        if (local.getFullPathName().isEmpty())
            return false;

        auto aliased = SamplePool::instance().aliasFile(local, currentSample);
        if (!aliased)
        {
            error = "Could not register edited sample in the sample pool";
            return false;
        }

        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        auto& slot = slots[static_cast<size_t>(slotIndex)];
        if (slot.sample != currentSample || slot.sampleFile != source) {
            error = "Slot changed during save; please save again"; return false;
        }
        slot.sample = std::move(aliased);
        slot.sampleFile = local;
        slot.sampleAudioModified = false;
        slot.status = "Loaded: " + local.getFileName();
        markAudioStateDirty();
        return true;
    }

    if (source.getFullPathName().isEmpty())
        return true;

    auto local = library.materialiseSample(source, error);
    if (local.getFullPathName().isEmpty())
        return false;

    if (local != source)
    {
        auto loaded = currentSample ? SamplePool::instance().aliasFile(local, currentSample)
                                    : SamplePool::instance().load(local, error);
        if (!loaded) return false;

        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        auto& slot = slots[static_cast<size_t>(slotIndex)];
        if (slot.sample != currentSample || slot.sampleFile != source) {
            error = "Slot changed during save; please save again"; return false;
        }
        slot.sample = std::move(loaded);
        slot.sampleFile = local;
        slot.sampleAudioModified = false;
        slot.status = "Loaded: " + local.getFileName();
        markAudioStateDirty();
    }
    return true;
}

bool LSampler24AudioProcessor::materialiseSampleSetExtras(int slotIndex, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    for (int sampleIndex = 1; sampleIndex < sampleSetSize; ++sampleIndex)
    {
        juce::File source;
        std::shared_ptr<SharedSample> currentSample;
        {
            const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
            const auto& slot = slots[size_t(slotIndex)];
            source = slot.alternateSampleFiles[size_t(sampleIndex - 1)];
            currentSample = slot.alternateSamples[size_t(sampleIndex - 1)];
        }
        if (!currentSample && source.getFullPathName().isEmpty()) continue;
        if (source.getFullPathName().isEmpty()) continue;
        auto local = library.materialiseSample(source, error);
        if (local.getFullPathName().isEmpty()) return false;
        if (local == source) continue;
        auto loaded = currentSample ? SamplePool::instance().aliasFile(local, currentSample)
                                    : SamplePool::instance().load(local, error);
        if (!loaded) return false;
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        auto& slot = slots[size_t(slotIndex)];
        if (slot.alternateSamples[size_t(sampleIndex - 1)] != currentSample
            || slot.alternateSampleFiles[size_t(sampleIndex - 1)] != source)
        {
            error = "Sample Set changed during save; please save again";
            return false;
        }
        slot.alternateSamples[size_t(sampleIndex - 1)] = std::move(loaded);
        slot.alternateSampleFiles[size_t(sampleIndex - 1)] = local;
        markAudioStateDirty();
    }
    return true;
}

bool LSampler24AudioProcessor::writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const
{
    auto xml = tree.createXml();
    if (!xml || !xml->writeTo(file))
    {
        error = "Could not write preset";
        return false;
    }
    return true;
}

juce::ValueTree LSampler24AudioProcessor::readPreset(const juce::File& file, juce::String& error) const
{
    auto xml = juce::XmlDocument::parse(file);
    if (!xml)
    {
        error = "Could not read preset";
        return {};
    }
    return juce::ValueTree::fromXml(*xml);
}

bool LSampler24AudioProcessor::saveSlotPreset(const juce::File& presetFile, juce::String& error)
{
    return saveSlotPresetAt(presetFile, getCurrentSlot(), error);
}

bool LSampler24AudioProcessor::saveSlotPresetAt(const juce::File& presetFile, int slotIndex, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    if (!materialiseSlotSample(slotIndex, error))
        return false;
    if (!materialiseSampleSetExtras(slotIndex, error))
        return false;
    if (!writePreset(presetFile, makeSlotState(slotIndex, "LSampler24Slot"), error))
        return false;
    { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); slots[size_t(slotIndex)].presetFile = presetFile; }
    return true;
}

bool LSampler24AudioProcessor::loadSlotPreset(const juce::File& presetFile, juce::String& error)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }
    const int slotIndex = currentSlot.load(std::memory_order_relaxed);
    if (!tree.hasProperty("slotName") || tree.getProperty("slotName").toString().trim().isEmpty())
    {
        auto legacyName = presetFile.getFileNameWithoutExtension();
        if (legacyName.startsWithIgnoreCase("Slot_")) legacyName = legacyName.substring(5);
        tree.setProperty("slotName", legacyName, nullptr);
    }
    const bool ok = restoreSlotState(slotIndex, tree, error,
        isFileTaskRunning() ? std::function<void(double)>([this](double f) { setFileTaskProgress(f); })
                            : std::function<void(double)>{});
    if (ok)
    {
        juce::File actual;
        { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); slots[size_t(slotIndex)].presetFile = presetFile; actual = slots[size_t(slotIndex)].sampleFile; }
        if (actual.existsAsFile())
        {
            tree.setProperty("sampleReference", library.makeSampleReference(actual), nullptr);
            tree.setProperty("sampleHash", library.makeSampleHash(actual), nullptr);
            juce::String ignored; writePreset(presetFile, tree, ignored);
        }
    }
    return ok;
}

bool LSampler24AudioProcessor::loadSlotPresetToSlot(const juce::File& presetFile,
                                                    int slotIndex, juce::String& error,
                                                    std::function<void(double)> progress)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    if (!tree.hasProperty("slotName") || tree.getProperty("slotName").toString().trim().isEmpty())
    {
        auto legacyName = presetFile.getFileNameWithoutExtension();
        if (legacyName.startsWithIgnoreCase("Slot_")) legacyName = legacyName.substring(5);
        tree.setProperty("slotName", legacyName, nullptr);
    }
    if (!progress && isFileTaskRunning())
        progress = [this](double f) { setFileTaskProgress(f); };
    const bool ok = restoreSlotState(slotIndex, tree, error, std::move(progress));
    if (ok)
    {
        juce::File actual;
        { const juce::ScopedLock lock(stateLock); absorbHostValuesLocked(); slots[size_t(slotIndex)].presetFile = presetFile; actual = slots[size_t(slotIndex)].sampleFile; }
        if (actual.existsAsFile())
        {
            tree.setProperty("sampleReference", library.makeSampleReference(actual), nullptr);
            tree.setProperty("sampleHash", library.makeSampleHash(actual), nullptr);
            juce::String ignored; writePreset(presetFile, tree, ignored);
        }
    }
    return ok;
}

bool LSampler24AudioProcessor::prepareLibrarySlotPreview(const juce::File& presetFile, juce::String& error)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }

    lsampler::SlotParameters params;
    params[P::low] = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("lowKey", 0)));
    params[P::high] = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("highKey", 127)));
    params[P::root] = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("rootNote", 60)));
    const float volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(tree.getProperty("volume", 1.0)));
    params[P::input_gain] = volume > 0 ? 20 * std::log10(volume) : -120;
    if (auto values = tree.getChildWithName("Parameters"); values.isValid())
        for (int i = 0; i < parameterCount; ++i)
        {
            const auto& d = parameters[size_t(i)];
            if (values.hasProperty(d.key)) params.values[size_t(i)] = sanitise(d, double(values.getProperty(d.key)));
        }
    const auto loops = tree.getChildWithName("Loops");
    for (int j = 0; j < loops.getNumChildren(); ++j)
    {
        const auto loop = loops.getChild(j);
        const int index = int(loop.getProperty("index", j));
        if (index < 0 || index >= loopCount) continue;
        for (int i = 0; i < loopParameterCount; ++i)
        {
            const auto& d = loopParameters[size_t(i)];
            params.loops[size_t(index)][size_t(i)] = sanitise(d, double(loop.getProperty(d.key, d.initial)));
        }
    }

    auto reference = tree.getProperty("sampleReference").toString();
    if (reference.isEmpty()) reference = tree.getProperty("samplePath").toString();
    const auto sampleHash = tree.getProperty("sampleHash").toString();
    const auto file = library.resolveSampleReference(reference, sampleHash);
    if (!file.existsAsFile())
    {
        error = "Sample missing: " + file.getFileName();
        return false;
    }
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded) return false;

    // Browsing an old recipe upgrades it in place. If the WAV was manually
    // moved below Library/Samples, store its current path and stable hash.
    tree.setProperty("sampleReference", library.makeSampleReference(file), nullptr);
    tree.setProperty("sampleHash", library.makeSampleHash(file), nullptr);
    { juce::String ignored; writePreset(presetFile, tree, ignored); }

    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& target = (*libraryPreviewSnapshots)[size_t(libraryPreviewWriter)];
    target.owner = std::move(loaded);
    target.state = lsampler::prepareSlotAudioState(params, target.owner.get(), preparedSampleRate, ++nextRevision);
    target.state.slice = prepareSliceAudio(SliceState::fromTree(tree.getChildWithName("Slice")), target.state.length);
    libraryPreviewWriter = libraryPreviewMiddle.exchange(libraryPreviewWriter | 4, std::memory_order_acq_rel) & 3;
    return true;
}

bool LSampler24AudioProcessor::saveBankPreset(const juce::File& presetFile, juce::String& error)
{
    for (int i = 0; i < slotCount; ++i)
    {
        if (!materialiseSlotSample(i, error)) return false;
        if (!materialiseSampleSetExtras(i, error)) return false;
    }

    juce::ValueTree bank("LSampler24Bank");
    bank.setProperty("format", "LSampler-24 Bank", nullptr);
    bank.setProperty("formatVersion", 5, nullptr);
    bank.setProperty("slotCount", slotCount, nullptr);
    bank.setProperty("currentSlot", currentSlot.load(), nullptr);
    for (int i = 0; i < globalParameterCount; ++i)
        bank.setProperty(globalParameters[size_t(i)].key,
                         getGlobalOutputParameter(static_cast<GlobalP>(i)), nullptr);

    for (int i = 0; i < slotCount; ++i)
        bank.addChild(makeSlotState(i, "Slot"), -1, nullptr);
    saveBankMacroState(bank);

    if (!writePreset(presetFile, bank, error)) return false;
    { const juce::ScopedLock lock(stateLock); currentBankName = presetFile.getFileNameWithoutExtension(); }
    return true;
}

juce::String LSampler24AudioProcessor::getCurrentBankName() const
{
    const juce::ScopedLock lock(stateLock);
    return currentBankName;
}

bool LSampler24AudioProcessor::loadBankPreset(const juce::File& presetFile, juce::String& error)
{
    auto bank = readPreset(presetFile, error);
    if (!bank.hasType("LSampler24Bank") || bank.getNumChildren() == 0)
    {
        error = "Not an LSampler-24 bank preset";
        return false;
    }

    bool ok = true;
    juce::String firstError;
    const int count = juce::jmin(slotCount, bank.getNumChildren());
    int totalUnits = 0;
    for (int i = 0; i < count; ++i)
        totalUnits += slotLoadUnits(bank.getChild(i));
    totalUnits = juce::jmax(1, totalUnits);
    int completedUnits = 0;
    for (int i = 0; i < count; ++i)
    {
        juce::String slotError;
        const auto slotTree = bank.getChild(i);
        const int units = slotLoadUnits(slotTree);
        const int before = completedUnits;
        const auto reportSlot = [this, before, units, totalUnits](double fraction) {
            setFileTaskProgress((double(before) + double(units) * fraction) / double(totalUnits));
        };
        if (!restoreSlotState(i, slotTree, slotError, reportSlot))
        {
            ok = false;
            if (firstError.isEmpty()) firstError = slotError;
        }
        completedUnits += units;
        setFileTaskProgress(double(completedUnits) / double(totalUnits));
    }
    {
        const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
        for (int i = count; i < slotCount; ++i)
            slots[static_cast<size_t>(i)] = SlotState{};
    }

    currentSlot = juce::jlimit(0, slotCount - 1, static_cast<int>(bank.getProperty("currentSlot", 0)));
    for (int i = 0; i < globalParameterCount; ++i)
        if (bank.hasProperty(globalParameters[size_t(i)].key))
            setGlobalOutputParameter(static_cast<GlobalP>(i), double(bank.getProperty(globalParameters[size_t(i)].key)));
    restoreBankMacroState(bank);
    resetOutputEnvelope.store(true, std::memory_order_release);
    markAudioStateDirty();
    if (ok)
    {
        const juce::ScopedLock lock(stateLock);
        currentBankName = presetFile.getFileNameWithoutExtension();
    }
    error = firstError;
    return ok;
}


bool LSampler24AudioProcessor::importFilesToLibrary(const juce::Array<juce::File>& sourceFiles,
                                                     int& importedSlots,
                                                     int& skippedFiles,
                                                     juce::String& error)
{
    importedSlots = 0;
    skippedFiles = 0;
    error.clear();

    bool foundSupportedAudio = false;
    int progressIndex = 0;
    const int progressTotal = juce::jmax(1, sourceFiles.size());
    for (const auto& source : sourceFiles)
    {
        setFileTaskProgress(double(progressIndex++) / double(progressTotal));
        if (shouldStopFileTask()) { error = "Import stopped"; return false; }
        if (!source.existsAsFile() || !SamplePool::instance().canReadFile(source))
            continue;

        foundSupportedAudio = true;
        juce::String localError;
        auto localSample = library.materialiseSample(source, localError);
        if (localSample.getFullPathName().isEmpty())
        {
            ++skippedFiles;
            continue;
        }

        lsampler::SlotParameters defaults;
        juce::ValueTree tree("LSampler24Slot");
        tree.setProperty("format", "LSampler-24 Slot", nullptr);
        tree.setProperty("formatVersion", 3, nullptr);
        tree.setProperty("index", 0, nullptr);
        tree.setProperty("sampleReference", library.makeSampleReference(localSample), nullptr);
        tree.setProperty("sampleHash", library.makeSampleHash(localSample), nullptr);
        tree.setProperty("lowKey", defaults[P::low], nullptr);
        tree.setProperty("highKey", defaults[P::high], nullptr);
        tree.setProperty("rootNote", defaults[P::root], nullptr);
        tree.setProperty("volume", std::pow(10.0, defaults[P::input_gain] / 20.0), nullptr);

        juce::ValueTree values("Parameters");
        for (int i = 0; i < parameterCount; ++i)
            values.setProperty(parameters[size_t(i)].key, defaults.values[size_t(i)], nullptr);
        tree.addChild(values, -1, nullptr);

        juce::ValueTree loops("Loops");
        for (int j = 0; j < loopCount; ++j)
        {
            juce::ValueTree loop("Loop");
            loop.setProperty("index", j, nullptr);
            for (int i = 0; i < loopParameterCount; ++i)
                loop.setProperty(loopParameters[size_t(i)].key, defaults.loops[size_t(j)][size_t(i)], nullptr);
            loops.addChild(loop, -1, nullptr);
        }
        tree.addChild(loops, -1, nullptr);

        auto cleanBase = juce::File::createLegalFileName(source.getFileNameWithoutExtension()).trim();
        if (cleanBase.isEmpty())
            cleanBase = "Sample";

        auto presetFile = library.slots().getChildFile("Slot_" + cleanBase + LibraryManager::slotExtension);
        if (presetFile.existsAsFile())
            presetFile = library.slots().getNonexistentChildFile("Slot_" + cleanBase,
                                                                  LibraryManager::slotExtension,
                                                                  false);

        if (!writePreset(presetFile, tree, localError))
        {
            ++skippedFiles;
            continue;
        }
        ++importedSlots;
    }

    if (!foundSupportedAudio)
    {
        error = "No supported audio files found";
        return false;
    }
    if (importedSlots == 0)
    {
        error = "No audio files could be imported";
        return false;
    }
    return true;
}

bool LSampler24AudioProcessor::importFolderToLibrary(const juce::File& sourceFolder,
                                                      int& importedSlots,
                                                      int& skippedFiles,
                                                      juce::String& error)
{
    importedSlots = 0;
    skippedFiles = 0;
    error.clear();
    if (!sourceFolder.isDirectory())
    {
        error = "Import folder not found";
        return false;
    }

    juce::Array<juce::File> files;
    sourceFolder.findChildFiles(files, juce::File::findFiles, true);
    bool foundSupportedAudio = false;
    int progressIndex = 0;
    const int progressTotal = juce::jmax(1, files.size());

    auto collectionName = juce::File::createLegalFileName(sourceFolder.getFileName()).trim();
    if (collectionName.isEmpty())
        collectionName = "Imported";

    const auto slotCollectionRoot = library.slots().getChildFile(collectionName);
    const auto sampleCollectionRoot = library.samples().getChildFile(collectionName);
    slotCollectionRoot.createDirectory();
    sampleCollectionRoot.createDirectory();

    for (const auto& source : files)
    {
        setFileTaskProgress(double(progressIndex++) / double(progressTotal));
        if (shouldStopFileTask()) { error = "Import stopped"; return false; }
        if (!source.existsAsFile() || !SamplePool::instance().canReadFile(source))
            continue;

        foundSupportedAudio = true;
        juce::String localError;
        auto relativeFile = source.getRelativePathFrom(sourceFolder).replaceCharacter('\\', '/');
        auto sampleRelative = collectionName + "/" + relativeFile;
        auto localSample = library.materialiseSampleAtRelativePath(source, sampleRelative, localError);
        if (localSample.getFullPathName().isEmpty())
        {
            ++skippedFiles;
            continue;
        }

        lsampler::SlotParameters defaults;
        juce::ValueTree tree("LSampler24Slot");
        tree.setProperty("format", "LSampler-24 Slot", nullptr);
        tree.setProperty("formatVersion", 3, nullptr);
        tree.setProperty("index", 0, nullptr);
        tree.setProperty("sampleReference", library.makeSampleReference(localSample), nullptr);
        tree.setProperty("sampleHash", library.makeSampleHash(localSample), nullptr);
        tree.setProperty("lowKey", defaults[P::low], nullptr);
        tree.setProperty("highKey", defaults[P::high], nullptr);
        tree.setProperty("rootNote", defaults[P::root], nullptr);
        tree.setProperty("volume", std::pow(10.0, defaults[P::input_gain] / 20.0), nullptr);

        juce::ValueTree values("Parameters");
        for (int i = 0; i < parameterCount; ++i)
            values.setProperty(parameters[size_t(i)].key, defaults.values[size_t(i)], nullptr);
        tree.addChild(values, -1, nullptr);

        juce::ValueTree loops("Loops");
        for (int j = 0; j < loopCount; ++j)
        {
            juce::ValueTree loop("Loop");
            loop.setProperty("index", j, nullptr);
            for (int i = 0; i < loopParameterCount; ++i)
                loop.setProperty(loopParameters[size_t(i)].key, defaults.loops[size_t(j)][size_t(i)], nullptr);
            loops.addChild(loop, -1, nullptr);
        }
        tree.addChild(loops, -1, nullptr);

        auto relativeParent = source.getParentDirectory().getRelativePathFrom(sourceFolder);
        juce::File targetDir = slotCollectionRoot;
        if (relativeParent.isNotEmpty() && relativeParent != ".")
            targetDir = slotCollectionRoot.getChildFile(relativeParent);
        targetDir.createDirectory();

        auto cleanBase = juce::File::createLegalFileName(source.getFileNameWithoutExtension()).trim();
        if (cleanBase.isEmpty()) cleanBase = "Sample";
        auto presetFile = targetDir.getChildFile("Slot_" + cleanBase + LibraryManager::slotExtension);
        if (presetFile.existsAsFile())
            presetFile = targetDir.getNonexistentChildFile("Slot_" + cleanBase,
                                                            LibraryManager::slotExtension,
                                                            false);

        if (!writePreset(presetFile, tree, localError))
        {
            ++skippedFiles;
            continue;
        }
        ++importedSlots;
    }

    if (!foundSupportedAudio)
    {
        error = "No supported audio files found";
        return false;
    }
    if (importedSlots == 0)
    {
        error = "No audio files could be imported";
        return false;
    }
    return true;
}

bool LSampler24AudioProcessor::exportLibraryFolderArchive(const juce::File& slotFolder,
                                                           const juce::File& requestedTarget,
                                                           int& exportedSlots,
                                                           int& exportedSamples,
                                                           juce::String& error)
{
    exportedSlots = 0;
    exportedSamples = 0;
    error.clear();

    const bool singleSlot = slotFolder.existsAsFile() && slotFolder.hasFileExtension(LibraryManager::slotExtension);
    if ((!slotFolder.isDirectory() && !singleSlot)
        || (slotFolder != library.slots() && !slotFolder.isAChildOf(library.slots())))
    {
        error = "Select a slot or folder inside Library/Slots";
        return false;
    }

    juce::Array<juce::File> slotFiles;
    if (singleSlot) slotFiles.add(slotFolder);
    else slotFolder.findChildFiles(slotFiles, juce::File::findFiles, true,
                                   "*" + juce::String(LibraryManager::slotExtension));

    struct ExportSample { juce::File file; juce::String archivePath; };
    struct ExportSlot { juce::File slot; juce::String slotPath; std::vector<ExportSample> samples; };
    std::vector<ExportSlot> valid;

    int scanIndex = 0;
    const int scanTotal = juce::jmax(1, slotFiles.size());
    for (const auto& slotFile : slotFiles)
    {
        setFileTaskProgress(0.70 * double(scanIndex++) / double(scanTotal));
        if (shouldStopFileTask()) { error = "Export stopped"; return false; }
        juce::String readError;
        auto tree = readPreset(slotFile, readError);
        if (!tree.hasType("LSampler24Slot"))
            continue;

        std::vector<ExportSample> samples;
        std::set<juce::String> slotSeen;
        bool referencesOk = true;
        const auto addReference = [&](const juce::String& reference, const juce::String& hash)
        {
            if (reference.isEmpty()) return;
            auto sample = library.resolveSampleReference(reference, hash);
            if (!sample.existsAsFile() || !sample.isAChildOf(library.samples())) { referencesOk = false; return; }
            const auto key = sample.getFullPathName().replaceCharacter('\\', '/').toLowerCase();
            if (!slotSeen.insert(key).second) return;
            const auto relative = sample.getRelativePathFrom(library.samples()).replaceCharacter('\\', '/');
            samples.push_back({ sample, "Library/Samples/" + relative });
        };

        auto primaryReference = tree.getProperty("sampleReference").toString();
        if (primaryReference.isEmpty()) primaryReference = tree.getProperty("samplePath").toString();
        addReference(primaryReference, tree.getProperty("sampleHash").toString());

        const auto sampleSet = tree.getChildWithName("SampleSet");
        if (sampleSet.isValid())
            for (int i = 0; i < sampleSet.getNumChildren(); ++i)
            {
                const auto entry = sampleSet.getChild(i);
                addReference(entry.getProperty("sampleReference").toString(),
                             entry.getProperty("sampleHash").toString());
            }

        if (!referencesOk || samples.empty())
            continue;

        const auto slotRelative = slotFile.getRelativePathFrom(library.slots()).replaceCharacter('\\', '/');
        valid.push_back({ slotFile, "Library/Slots/" + slotRelative, std::move(samples) });
    }

    if (valid.empty())
    {
        error = "No valid LSampler-24 slots found in the selected folder";
        return false;
    }

    juce::File target = requestedTarget;
    if (!target.getFileName().endsWithIgnoreCase(".lsampler-24.ls24"))
        target = target.getSiblingFile(target.getFileNameWithoutExtension() + ".lsampler-24.ls24");

    target.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(target);
    juce::ZipFile::Builder builder;
    std::set<juce::String> seenSamples;
    for (const auto& item : valid)
    {
        builder.addFile(item.slot, 9, item.slotPath);
        ++exportedSlots;
        for (const auto& sample : item.samples)
        {
            const auto sampleKey = sample.file.getFullPathName().replaceCharacter('\\', '/').toLowerCase();
            if (seenSamples.insert(sampleKey).second)
            {
                builder.addFile(sample.file, 0, sample.archivePath);
                ++exportedSamples;
            }
        }
    }

    setFileTaskProgress(0.85);
    auto stream = temporary.getFile().createOutputStream();
    if (stream == nullptr) { error = "Could not create export file"; return false; }
    if (!builder.writeToStream(*stream, nullptr)) { error = "Could not write LSampler-24 library export"; return false; }
    stream->flush();
    if (stream->getStatus().failed()) { error = "Could not flush library export"; return false; }
    stream.reset();
    if (!temporary.overwriteTargetFileWithTemporary()) { error = "Could not replace export file"; return false; }
    setFileTaskProgress(1.0);
    return true;
}

bool LSampler24AudioProcessor::exportLibraryArchive(const juce::File& requestedTarget,
                                                     int& exportedSlots,
                                                     int& exportedSamples,
                                                     juce::String& error)
{
    return exportLibraryFolderArchive(library.slots(), requestedTarget, exportedSlots, exportedSamples, error);
}

// TEST106: a Bank export is a portable library package containing exactly one
// bank preset and every external WAV referenced by its 24 slots / Sample Sets.
// Do not change the active 24-slot audio state while packaging a saved Bank.
bool LSampler24AudioProcessor::exportBankArchive(const juce::File& bankPreset,
                                                const juce::File& requestedTarget,
                                                int& exportedSamples, juce::String& error)
{
    exportedSamples = 0;
    error.clear();
    if (!bankPreset.existsAsFile() || !bankPreset.hasFileExtension(LibraryManager::bankExtension)
        || !bankPreset.isAChildOf(library.banks()))
    {
        error = "Select a saved bank inside Library/Banks";
        return false;
    }

    auto bank = readPreset(bankPreset, error);
    if (!bank.hasType("LSampler24Bank"))
    {
        error = "Not an LSampler-24 bank";
        return false;
    }

    // Package WAVs under the bank's own name, not in the generic BankPackages
    // directory. Match the readable bank name used by the internal browser.
    auto sampleFolderName = bankPreset.getFileNameWithoutExtension().trim();
    if (sampleFolderName.startsWithIgnoreCase("Bank_"))
        sampleFolderName = sampleFolderName.substring(5);
    sampleFolderName = juce::File::createLegalFileName(sampleFolderName.trim());
    if (sampleFolderName.isEmpty() || sampleFolderName == "." || sampleFolderName == "..")
        sampleFolderName = "LSampler-24 Bank";
    const auto sampleArchiveFolder = "Library/Samples/" + sampleFolderName + "/";

    struct ArchiveSample { juce::File source; juce::String path; };
    std::vector<ArchiveSample> sources;
    std::map<juce::String, juce::String> pathByFile;
    std::set<juce::String> usedArchivePaths;
    const int totalSlots = juce::jmax(1, bank.getNumChildren());
    for (int i = 0; i < bank.getNumChildren(); ++i)
    {
        setFileTaskProgress(0.10 + 0.60 * double(i) / double(totalSlots));
        if (shouldStopFileTask()) { error = "Export stopped"; return false; }
        auto slot = bank.getChild(i);
        const auto prepareReference = [this, &sources, &pathByFile, &usedArchivePaths,
                                       &sampleArchiveFolder, &error](juce::ValueTree node,
                                                                    bool primary) -> bool
        {
            auto ref = node.getProperty("sampleReference").toString();
            if (ref.isEmpty() && primary) ref = node.getProperty("samplePath").toString();
            if (ref.isEmpty()) return true;

            const auto source = library.resolveSampleReference(ref, node.getProperty("sampleHash").toString());
            if (!source.existsAsFile())
            {
                error = "Bank sample not found: " + juce::File(ref).getFileName();
                return false;  // A portable export must not silently omit samples.
            }
            const auto hash = library.makeSampleHash(source);
            if (hash.isEmpty()) { error = "Could not hash bank sample: " + source.getFileName(); return false; }
            const auto key = source.getFullPathName().replaceCharacter('\\', '/').toLowerCase();
            auto it = pathByFile.find(key);
            juce::String relative;
            if (it == pathByFile.end())
            {
                // Keep the WAV's original, readable filename. TEST106 prefixed
                // SHA-256 to filenames, so strip that prefix when re-exporting
                // samples already imported from an older Bank package. Only
                // remove a prefix equal to the verified hash of this very file.
                auto filename = source.getFileName();
                const auto oldPrefix = hash + "_";
                while (filename.startsWithIgnoreCase(oldPrefix)
                       && filename.length() > oldPrefix.length())
                    filename = filename.substring(oldPrefix.length());
                filename = juce::File::createLegalFileName(filename);
                if (filename.isEmpty() || filename == "." || filename == "..")
                {
                    error = "Invalid bank sample filename";
                    return false;
                }

                relative = sampleArchiveFolder + filename;
                // A bank may reference two different WAVs with the same name.
                // Preserve original names unless there is a genuine collision;
                // only then append _2, _3 etc. before the extension.
                if (usedArchivePaths.find(relative.toLowerCase()) != usedArchivePaths.end())
                {
                    const int dot = filename.lastIndexOfChar('.');
                    const auto stem = dot > 0 ? filename.substring(0, dot) : filename;
                    const auto extension = dot > 0 ? filename.substring(dot) : juce::String();
                    int suffix = 2;
                    do
                    {
                        relative = sampleArchiveFolder + stem + "_" + juce::String(suffix++) + extension;
                    }
                    while (usedArchivePaths.find(relative.toLowerCase()) != usedArchivePaths.end());
                }
                usedArchivePaths.insert(relative.toLowerCase());
                pathByFile.emplace(key, relative);
                sources.push_back({ source, relative });
            }
            else relative = it->second;

            node.setProperty("sampleReference", relative, nullptr);
            node.setProperty("sampleHash", hash, nullptr);
            if (primary && node.hasProperty("samplePath")) node.removeProperty("samplePath", nullptr);
            return true;
        };

        if (!prepareReference(slot, true)) return false;
        const auto set = slot.getChildWithName("SampleSet");
        for (int j = 0; j < set.getNumChildren(); ++j)
            if (!prepareReference(set.getChild(j), false)) return false;
    }

    const auto staging = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("LSampler24_BankExport", juce::String(), true);
    if (staging.createDirectory().failed()) { error = "Could not create temporary bank folder"; return false; }
    struct Cleanup { juce::File dir; ~Cleanup() { if (dir.exists()) dir.deleteRecursively(); } } cleanup { staging };
    const auto stagedBank = staging.getChildFile(bankPreset.getFileName());
    if (!writePreset(stagedBank, bank, error)) return false;

    juce::ZipFile::Builder builder;
    builder.addFile(stagedBank, 9, "Library/Banks/" + stagedBank.getFileName());
    for (int i = 0; i < static_cast<int>(sources.size()); ++i)
    {
        if (shouldStopFileTask()) { error = "Export stopped"; return false; }
        builder.addFile(sources[size_t(i)].source, 0, sources[size_t(i)].path);
        setFileTaskProgress(0.70 + 0.25 * double(i + 1) / double(juce::jmax(1, int(sources.size()))));
    }
    exportedSamples = static_cast<int>(sources.size());

    auto target = requestedTarget;
    if (!target.getFileName().endsWithIgnoreCase(".lsampler-24.ls24"))
        target = target.getSiblingFile(target.getFileNameWithoutExtension() + ".lsampler-24.ls24");
    target.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(target);
    auto stream = temporary.getFile().createOutputStream();
    if (stream == nullptr) { error = "Could not create bank export"; return false; }
    if (!builder.writeToStream(*stream, nullptr)) { error = "Could not write bank export"; return false; }
    stream->flush();
    if (stream->getStatus().failed()) { error = "Could not flush bank export"; return false; }
    stream.reset();
    if (!temporary.overwriteTargetFileWithTemporary()) { error = "Could not save bank export"; return false; }
    setFileTaskProgress(1.0);
    return true;
}

bool LSampler24AudioProcessor::importBankArchive(const juce::File& archiveFile,
                                                juce::File& importedBank,
                                                int& importedSamples, juce::String& error)
{
    importedBank = {};
    importedSamples = 0;
    error.clear();
    if (!archiveFile.existsAsFile()) { error = "Bank archive not found"; return false; }

    juce::ZipFile zip(archiveFile);
    if (zip.getNumEntries() <= 0) { error = "Invalid LSampler-24 bank archive"; return false; }
    // Reject unsafe archive member paths before extraction, including ZIP-slip.
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* entry = zip.getEntry(i);
        if (entry == nullptr) { error = "Invalid bank archive entry"; return false; }
        const auto raw = entry->filename.replaceCharacter('\\', '/');
        const auto parts = juce::StringArray::fromTokens(raw, "/", "");
        if (raw.isEmpty() || raw.startsWithChar('/') || raw.containsChar(':')
            || parts.contains("..") || parts.contains(".")
            || !(raw.startsWith("Library/Banks/") || raw.startsWith("Library/Samples/")))
        {
            error = "Unsafe or unsupported bank archive path";
            return false;
        }
    }

    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("LSampler24_BankImport", juce::String(), true);
    if (tempRoot.createDirectory().failed()) { error = "Could not create temporary bank import folder"; return false; }
    struct Cleanup { juce::File dir; ~Cleanup() { if (dir.exists()) dir.deleteRecursively(); } } cleanup { tempRoot };
    if (zip.uncompressTo(tempRoot, true).failed()) { error = "Could not extract LSampler-24 bank"; return false; }
    const auto archivedBankDir = tempRoot.getChildFile("Library").getChildFile("Banks");
    const auto archivedSampleDir = tempRoot.getChildFile("Library").getChildFile("Samples");
    juce::Array<juce::File> archivedBanks;
    archivedBankDir.findChildFiles(archivedBanks, juce::File::findFiles, true,
                                   "*" + juce::String(LibraryManager::bankExtension));
    if (archivedBanks.size() != 1)
    {
        error = "Bank archive must contain one bank";
        return false;
    }
    auto bank = readPreset(archivedBanks.getFirst(), error);
    if (!bank.hasType("LSampler24Bank")) { error = "Invalid bank preset in archive"; return false; }

    const int slotTotal = juce::jmax(1, bank.getNumChildren());
    for (int i = 0; i < bank.getNumChildren(); ++i)
    {
        if (shouldStopFileTask()) { error = "Import stopped"; return false; }
        setFileTaskProgress(0.15 + 0.78 * double(i) / double(slotTotal));
        auto slot = bank.getChild(i);
        const auto installReference = [this, &tempRoot, &importedSamples, &error](juce::ValueTree node,
                                                                                               bool primary) -> bool
        {
            auto reference = node.getProperty("sampleReference").toString();
            if (reference.isEmpty() && primary) reference = node.getProperty("samplePath").toString();
            if (reference.isEmpty()) return true;
            const auto normalised = reference.replaceCharacter('\\', '/');
            if (!normalised.startsWith("Library/Samples/"))
            {
                error = "Bank has an external or invalid sample reference";
                return false;
            }
            const auto sample = tempRoot.getChildFile(normalised.replaceCharacter('/', juce::File::getSeparatorChar()));
            if (!sample.existsAsFile() || !sample.isAChildOf(tempRoot.getChildFile("Library").getChildFile("Samples")))
            {
                error = "Bank archive sample missing: " + juce::File(reference).getFileName();
                return false;
            }
            auto expected = node.getProperty("sampleHash").toString();
            if (expected.isNotEmpty() && juce::SHA256(sample).toHexString() != expected)
            {
                error = "Bank archive sample checksum mismatch: " + sample.getFileName();
                return false;
            }
            auto relative = sample.getRelativePathFrom(tempRoot.getChildFile("Library").getChildFile("Samples"));
            const auto destination = library.samples().getChildFile(relative);
            const bool alreadyPresent = destination.existsAsFile() && destination.getSize() == sample.getSize()
                && library.makeSampleHash(destination) == library.makeSampleHash(sample);
            juce::String localError;
            const auto installed = library.materialiseSampleAtRelativePath(sample, relative, localError);
            if (!installed.existsAsFile()) { error = localError; return false; }
            if (!alreadyPresent) ++importedSamples;
            node.setProperty("sampleReference", library.makeSampleReference(installed), nullptr);
            node.setProperty("sampleHash", library.makeSampleHash(installed), nullptr);
            if (primary && node.hasProperty("samplePath")) node.removeProperty("samplePath", nullptr);
            return true;
        };
        if (!installReference(slot, true)) return false;
        const auto set = slot.getChildWithName("SampleSet");
        for (int j = 0; j < set.getNumChildren(); ++j)
            if (!installReference(set.getChild(j), false)) return false;
    }
    const auto bankFileName = archivedBanks.getFirst().getFileName();
    auto target = library.banks().getChildFile(bankFileName);
    if (target.existsAsFile())
        target = library.banks().getNonexistentChildFile(archivedBanks.getFirst().getFileNameWithoutExtension(),
                                                       LibraryManager::bankExtension, false);
    if (!writePreset(target, bank, error)) return false;
    importedBank = target;
    setFileTaskProgress(1.0);
    return true;
}

bool LSampler24AudioProcessor::importLibraryArchive(const juce::File& archiveFile,
                                                       int& importedSlots,
                                                       int& importedSamples,
                                                       int& skippedItems,
                                                       juce::String& error)
{
    importedSlots = 0;
    importedSamples = 0;
    skippedItems = 0;
    error.clear();

    if (!archiveFile.existsAsFile())
    {
        error = "Library file not found";
        return false;
    }

    const auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("LSampler24_LibraryImport", juce::String(), true);
    if (tempRoot.createDirectory().failed())
    {
        error = "Could not create temporary import folder";
        return false;
    }

    struct TempCleanup { juce::File dir; ~TempCleanup() { if (dir.isDirectory()) dir.deleteRecursively(); } } cleanup { tempRoot };

    juce::ZipFile zip(archiveFile);
    if (zip.getNumEntries() <= 0)
    {
        error = "Invalid or empty LSampler-24 library";
        return false;
    }

    auto unzipResult = zip.uncompressTo(tempRoot, true);
    if (unzipResult.failed())
    {
        error = "Could not read LSampler-24 library";
        return false;
    }

    auto importedSlotsDir = tempRoot.getChildFile("Library").getChildFile("Slots");
    auto importedSamplesDir = tempRoot.getChildFile("Library").getChildFile("Samples");
    if (!importedSlotsDir.isDirectory() || !importedSamplesDir.isDirectory())
    {
        error = "Invalid LSampler-24 library structure";
        return false;
    }

    juce::Array<juce::File> slotFiles;
    importedSlotsDir.findChildFiles(slotFiles, juce::File::findFiles, true,
                                     "*" + juce::String(LibraryManager::slotExtension));

    int importIndex = 0;
    const int importTotal = juce::jmax(1, slotFiles.size());
    for (const auto& sourceSlot : slotFiles)
    {
        setFileTaskProgress(0.15 + 0.80 * double(importIndex++) / double(importTotal));
        if (shouldStopFileTask()) { error = "Import stopped"; return false; }
        juce::String localError;
        auto tree = readPreset(sourceSlot, localError);
        if (!tree.hasType("LSampler24Slot"))
        {
            ++skippedItems;
            continue;
        }

        bool hadReference = false;
        bool referencesOk = true;
        auto importReference = [&](juce::ValueTree node, bool allowLegacyPath)
        {
            auto reference = node.getProperty("sampleReference").toString();
            if (reference.isEmpty() && allowLegacyPath)
                reference = node.getProperty("samplePath").toString();
            if (reference.isEmpty()) return true;
            hadReference = true;

            if (juce::File::isAbsolutePath(reference))
                return false; // Exported libraries must be self-contained.

            juce::File archivedSample;
            const auto normalised = reference.replaceCharacter('\\', '/');
            if (normalised.startsWithIgnoreCase("Library/Samples/"))
                archivedSample = tempRoot.getChildFile(normalised.replaceCharacter('/', juce::File::getSeparatorChar()));
            else
                archivedSample = importedSamplesDir.getChildFile(juce::File(normalised).getFileName());

            if (!archivedSample.existsAsFile())
                return false;

            const auto archivedRelative = archivedSample.getRelativePathFrom(importedSamplesDir).replaceCharacter('\\', '/');
            const auto desiredLocal = library.samples().getChildFile(archivedRelative.replaceCharacter('/', juce::File::getSeparatorChar()));
            const bool alreadyPresent = desiredLocal.existsAsFile()
                && desiredLocal.getSize() == archivedSample.getSize()
                && juce::SHA256(desiredLocal).toHexString() == juce::SHA256(archivedSample).toHexString();

            auto localSample = library.materialiseSampleAtRelativePath(archivedSample, archivedRelative, localError);
            if (!localSample.existsAsFile())
                return false;
            if (!alreadyPresent)
                ++importedSamples;

            node.setProperty("sampleReference", library.makeSampleReference(localSample), nullptr);
            node.setProperty("sampleHash", library.makeSampleHash(localSample), nullptr);
            if (allowLegacyPath && node.hasProperty("samplePath"))
                node.removeProperty("samplePath", nullptr);
            return true;
        };

        referencesOk = importReference(tree, true);
        auto sampleSet = tree.getChildWithName("SampleSet");
        if (referencesOk && sampleSet.isValid())
            for (int i = 0; i < sampleSet.getNumChildren(); ++i)
                if (!importReference(sampleSet.getChild(i), false))
                {
                    referencesOk = false;
                    break;
                }

        if (!hadReference || !referencesOk)
        {
            ++skippedItems;
            continue;
        }

        auto relativeParent = sourceSlot.getParentDirectory().getRelativePathFrom(importedSlotsDir);
        juce::File destDir = library.slots();
        if (relativeParent.isNotEmpty() && relativeParent != ".")
            destDir = library.slots().getChildFile(relativeParent);
        destDir.createDirectory();

        auto dest = destDir.getChildFile(sourceSlot.getFileName());
        if (dest.existsAsFile())
            dest = destDir.getNonexistentChildFile(sourceSlot.getFileNameWithoutExtension(),
                                                    LibraryManager::slotExtension,
                                                    false);
        if (!writePreset(dest, tree, localError))
        {
            ++skippedItems;
            continue;
        }
        ++importedSlots;
    }

    if (importedSlots == 0)
    {
        error = "No valid LSampler-24 library items found";
        return false;
    }
    return true;
}


juce::File LSampler24AudioProcessor::defaultLibraryRoot()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LSampler-24");
}

void LSampler24AudioProcessor::setLibraryRoot(const juce::File& root)
{
    if (root.getFullPathName().isEmpty()) return;
    library = LibraryManager(root);
    uiRevision.fetch_add(1, std::memory_order_release);
}

void LSampler24AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("LSampler24State");
    state.setProperty("formatVersion", 5, nullptr);
    state.setProperty("slotCount", slotCount, nullptr);
    state.setProperty("currentSlot", currentSlot.load(), nullptr);
    state.setProperty("currentBankName", getCurrentBankName(), nullptr);
    for (int i = 0; i < globalParameterCount; ++i)
        state.setProperty(globalParameters[size_t(i)].key,
                          getGlobalOutputParameter(static_cast<GlobalP>(i)), nullptr);
    for (int i = 0; i < slotCount; ++i)
        state.addChild(makeSlotState(i, "Slot"), -1, nullptr);
    saveBankMacroState(state);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void LSampler24AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        juce::String error;
        { const juce::ScopedLock lock(stateLock);
          currentBankName = state.getProperty("currentBankName", juce::String()).toString(); }

        if (state.hasType("LSampler24State") && state.getNumChildren() > 0)
        {
            const int count = juce::jmin(slotCount, state.getNumChildren());
            for (int i = 0; i < count; ++i)
            {
                juce::String ignored;
                restoreSlotState(i, state.getChild(i), ignored);
            }
            currentSlot = juce::jlimit(0, slotCount - 1, static_cast<int>(state.getProperty("currentSlot", 0)));
            for (int i = 0; i < globalParameterCount; ++i)
                if (state.hasProperty(globalParameters[size_t(i)].key))
                {
                    const auto value=sanitise(globalParameters[size_t(i)],double(state.getProperty(globalParameters[size_t(i)].key)));
                    hostGlobals[size_t(i)]->storeReal(value);
                    globalOutputParameters[size_t(i)].store(value);
                }
            restoreBankMacroState(state);
            resetOutputEnvelope.store(true, std::memory_order_release);
            markAudioStateDirty();
        }
        else
        {
            // TEST1/TEST2 single-slot compatibility.
            restoreSlotState(0, state, error);
            currentSlot = 0;
            restoreBankMacroState(state);
            markAudioStateDirty();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LSampler24AudioProcessor();
}

lsampler::SliceState LSampler24AudioProcessor::getSliceState(int slot) const {
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    return slots[size_t(juce::jlimit(0,slotCount-1,slot))].slice;
}
void LSampler24AudioProcessor::setSliceState(int slot,const lsampler::SliceState& state) {
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& target=slots[size_t(juce::jlimit(0,slotCount-1,slot))];
    target.slice=state;target.slice.sanitise();markAudioStateDirty();
}
lsampler::SliceAudioState LSampler24AudioProcessor::getSliceLayout(int slot,int& absoluteStart,double& rate) const {
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const auto& s=slots[size_t(juce::jlimit(0,slotCount-1,slot))];
    const auto audio=prepareSlotAudioState(s.parameters,s.sample.get(),preparedSampleRate,0,
                                           s.thresholdStartFrame,s.thresholdEndFrame);
    absoluteStart=audio.start;rate=s.sample?s.sample->sourceSampleRate:preparedSampleRate;
    return prepareSliceAudio(s.slice,audio.length);
}
bool LSampler24AudioProcessor::moveSliceBoundary(int slot,int boundary,int direction,int frames) {
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    auto& s=slots[size_t(juce::jlimit(0,slotCount-1,slot))];
    if(!s.sample||direction==0)return false;
    const auto audio=prepareSlotAudioState(s.parameters,s.sample.get(),preparedSampleRate,0,
                                           s.thresholdStartFrame,s.thresholdEndFrame);
    const auto layout=prepareSliceAudio(s.slice,audio.length);
    if(boundary<=0||boundary>=layout.count||layout.count!=s.slice.division())return false;
    const int lower=layout.boundaries[size_t(boundary-1)]+1,upper=layout.boundaries[size_t(boundary+1)]-1;
    if(lower>upper)return false;
    const int old=layout.boundaries[size_t(boundary)];
    int next=std::clamp(old+(direction<0?-1:1)*std::max(1,frames),lower,upper);
    if(s.slice.zeroCrossing) {
        const int delta=direction<0?-1:1;next=old;
        for(int i=old+delta;i>=lower&&i<=upper;i+=delta)
            if(sampleCrossesZero(s.sample->audio,audio.start+i,audio.start,audio.start+audio.length)){next=i;break;}
    }
    if(next==old)return false;
    // A quarter frame offset makes integer floor conversion stable in doubles.
    s.slice.boundaries[size_t(boundary)]=(next+.25)/audio.length;
    markAudioStateDirty();return true;
}
void LSampler24AudioProcessor::requestSlicePreview(int slot,int kind,int item,bool toggle) noexcept {
    const int command=juce::jlimit(0,5,kind)|(juce::jlimit(0,slotCount-1,slot)<<3)
        |(juce::jlimit(0,127,item)<<8)|(toggle?(1<<15):0);
    slicePreviewCommand.store(command,std::memory_order_release);
}

LSampler24AudioProcessor::VisualSlotState LSampler24AudioProcessor::getVisualSlotState(int slot) const
{
    const juce::ScopedLock lock(stateLock); absorbHostValuesLocked();
    const auto& source = slots[size_t(juce::jlimit(0, slotCount - 1, slot))];
    return { source.sample, source.parameters, source.slice,
             source.thresholdStartFrame, source.thresholdEndFrame, source.slotName };
}


void LSampler24AudioProcessor::createHostParameters()
{
    static_assert(std::atomic<double>::is_always_lock_free, "Automation requires lock-free real values");
    const auto add = [&](const juce::String& id,const juce::String& name,Descriptor d,std::atomic<uint64_t>& revision) {
        auto* p=new HostParameter(id,name,d,revision,uiRevision);
        addParameter(p); // AudioProcessor owns this fixed lifetime parameter.
        return p;
    };
    for(int i=0;i<globalParameterCount;++i)
        hostGlobals[size_t(i)]=add("global_"+juce::String(globalParameters[size_t(i)].key),
            globalParameters[size_t(i)].name,globalParameters[size_t(i)],globalHostGeneration);
    for(int slot=0;slot<slotCount;++slot) {
        auto& h=hostSlots[size_t(slot)];auto& revision=hostGenerations[size_t(slot)];
        const juce::String id="slot"+juce::String(slot+1).paddedLeft('0',2)+"_";
        const juce::String name="Slot "+juce::String(slot+1)+" - ";
        for(int i=0;i<parameterCount;++i) if(hostAutomatable(static_cast<P>(i))) {
            const auto& d=parameters[size_t(i)];juce::String title=d.name;
            if(i==int(P::input_gain)) title="Volume";
            if(i==int(P::lp_cutoff)) title="Filter Cutoff";
            if(i==int(P::lp_resonance)) title="Filter Resonance";
            h.values[size_t(i)]=add(id+d.key,name+title,d,revision);
        }
        for(int lfo=0;lfo<2;++lfo) {
            auto* rate=h.values[size_t(lfo?P::lfo2_rate:P::lfo1_rate)];
            auto* sync=h.values[size_t(lfo?P::lfo2_bpm_sync:P::lfo1_bpm_sync)];
            rate->syncParameter=sync;sync->syncedRate=rate;
        }
        for(int j=0;j<loopCount;++j)for(int i=0;i<loopParameterCount;++i) {
            const auto& d=loopParameters[size_t(i)];
            h.loops[size_t(j)][size_t(i)]=add(id+"loop"+juce::String(j+1).paddedLeft('0',2)+"_"+d.key,
                name+"Loop "+juce::String(j+1)+" - "+d.name,d,revision);
        }
        for(int i=0;i<int(sliceGlobals.size());++i) {
            const auto& d=sliceGlobals[size_t(i)];
            Descriptor converted{d.key,d.name,"Slice",d.step==1?Kind::integer:Kind::continuous,
                d.initial,d.min,d.max,d.step,d.step,"",d.step==1?0:2,d.labels};
            h.slice[size_t(i)]=add(id+"slice_"+d.key,name+d.name,converted,revision);
        }
        Descriptor variation{"variation_mode","Variation Mode","Sample Set",Kind::enumeration,0,0,4,1,1,"",0,
            "Off|Round Robin|Random|Random No Repeat|Shuffle No Repeat"};
        h.variation=add(id+variation.key,name+variation.name,variation,revision);
        for(int j=0;j<sampleSetSize;++j) {
            const auto sampleId=id+"sample"+juce::String(j+1).paddedLeft('0',2)+"_";
            const auto sampleName=name+"Sample "+juce::String(j+1)+" - ";
            Descriptor low{"velocity_low","Velocity Low","Sample Set",Kind::integer,1,1,127,1,1,"",0,""};
            Descriptor high{"velocity_high","Velocity High","Sample Set",Kind::integer,127,1,127,1,1,"",0,""};
            h.velocityLow[size_t(j)]=add(sampleId+low.key,sampleName+low.name,low,revision);
            h.velocityHigh[size_t(j)]=add(sampleId+high.key,sampleName+high.name,high,revision);
            h.controlLow[size_t(j)]=1;h.controlHigh[size_t(j)]=127;
        }
    }
    // Append after the existing 6629 parameters: never shift an old index or
    // reuse an ID. JUCE VST3 hosts see each Bank Macro as its own automation lane.
    std::array<bool, parameterCount> added {};
    for (const auto& entry : grid) {
        if (!bankMacroEligible(entry)) continue;
        const int index = entry.parameter;
        if (added[size_t(index)]) continue;
        added[size_t(index)] = true;
        const auto& descriptor = parameters[size_t(index)];
        hostBankMacros[size_t(index)] = add("bankmacro_" + juce::String(descriptor.key),
            "Bank Macro - " + juce::String(descriptor.name),
            bankMacroDeltaDescriptor(index), bankMacroHostGeneration);
    }
    hostParametersReady=true;
}

// Control threads only, with stateLock held. Materialise the canonical host
// values before reads/edits/preset saves. This never notifies the DAW.
void LSampler24AudioProcessor::absorbHostValuesLocked() const
{
    if(!hostParametersReady)return;
    for(int k=0;k<slotCount;++k) {
        const auto generation=hostGenerations[size_t(k)].load(std::memory_order_acquire);
        if(controlHostGenerations[size_t(k)]==generation)continue;
        controlHostGenerations[size_t(k)]=generation;
        auto& s=slots[size_t(k)];const auto& h=hostSlots[size_t(k)];
        const double previousStart=s.parameters[P::sample_start],previousEnd=s.parameters[P::sample_end];
        for(int i=0;i<parameterCount;++i)if(auto* p=h.values[size_t(i)])
            s.parameters.values[size_t(i)]=h.control.values[size_t(i)]=p->load();
        for(int j=0;j<loopCount;++j)for(int i=0;i<loopParameterCount;++i)
            s.parameters.loops[size_t(j)][size_t(i)]=h.control.loops[size_t(j)][size_t(i)]=h.loops[size_t(j)][size_t(i)]->load();
        for(int i=0;i<int(sliceGlobals.size());++i) {
            const auto x=h.slice[size_t(i)]->load();
            if(i==int(SliceG::division)&&s.slice.globals[size_t(i)]!=x)s.slice.setDivision(int(x));
            s.slice.globals[size_t(i)]=h.controlSlice.globals[size_t(i)]=x;
        }
        if(s.sample&&(previousStart!=s.parameters[P::sample_start]||previousEnd!=s.parameters[P::sample_end])) {
            const int n=s.sample->audio.getNumSamples();
            if(n>=2) {
                s.thresholdStartFrame=std::clamp(int(std::floor(n*s.parameters[P::sample_start]*.01)),0,n-2);
                s.thresholdEndFrame=std::clamp(int(std::floor(n*s.parameters[P::sample_end]*.01)),s.thresholdStartFrame+1,n);
            }
        }
        s.variationMode=h.controlVariation=int(h.variation->load());
        for(int j=0;j<sampleSetSize;++j) {
            s.sampleVelocityLow[size_t(j)]=h.controlLow[size_t(j)]=int(h.velocityLow[size_t(j)]->load());
            s.sampleVelocityHigh[size_t(j)]=h.controlHigh[size_t(j)]=int(h.velocityHigh[size_t(j)]->load());
        }
    }
}

void LSampler24AudioProcessor::publishHostValuesLocked(bool notify, int forceSlot)
{
    if(!hostParametersReady)return;
    const auto publish=[&](HostParameter* p,double value,double previous,bool force) {
        if(p && (force || value!=previous)) {
            p->storeReal(value);
            if(notify) notifyHostControl(p);
        }
    };
    for(int k=0;k<slotCount;++k) {
        auto& h=hostSlots[size_t(k)];auto& s=slots[size_t(k)];
        for(int i=0;i<parameterCount;++i)publish(h.values[size_t(i)],s.parameters.values[size_t(i)],h.control.values[size_t(i)],k==forceSlot);
        for(int j=0;j<loopCount;++j)for(int i=0;i<loopParameterCount;++i)
            publish(h.loops[size_t(j)][size_t(i)],s.parameters.loops[size_t(j)][size_t(i)],h.control.loops[size_t(j)][size_t(i)],k==forceSlot);
        for(int i=0;i<int(sliceGlobals.size());++i)publish(h.slice[size_t(i)],s.slice.globals[size_t(i)],h.controlSlice.globals[size_t(i)],k==forceSlot);
        publish(h.variation,s.variationMode,h.controlVariation,k==forceSlot);
        for(int j=0;j<sampleSetSize;++j) {
            publish(h.velocityLow[size_t(j)],s.sampleVelocityLow[size_t(j)],h.controlLow[size_t(j)],k==forceSlot);
            publish(h.velocityHigh[size_t(j)],s.sampleVelocityHigh[size_t(j)],h.controlHigh[size_t(j)],k==forceSlot);
        }
        h.control=s.parameters;h.controlSlice=s.slice;h.controlVariation=s.variationMode;
        h.controlLow=s.sampleVelocityLow;h.controlHigh=s.sampleVelocityHigh;
        if(k==forceSlot)controlHostGenerations[size_t(k)]=~uint64_t(0);
    }
    // Read back coupled sanitisation (e.g. synced LFO rate), and concurrent
    // host writes, before publishing DSP/preset state. Do not leave a mirror
    // containing the unsanitised input of an otherwise unchanged host value.
    absorbHostValuesLocked();
}

// Called only from an actual keyboard gesture on the GUI thread. Send a
// no-change parameter edit to the DAW, using the same JUCE/VST3 notification
// channel as normal mouse/keyboard edits. This can make the parameter 'last
// touched'; the DAW alone owns automation lane visibility and arming.
juce::String LSampler24AudioProcessor::touchAutomationParameter(HostParameter* p)
{
    if (p == nullptr || shuttingDown.load(std::memory_order_acquire)) return {};
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        mm == nullptr || !mm->isThisTheMessageThread()) return {};
    p->beginChangeGesture();
    p->sendValueChangedMessageToListeners(p->getValue());
    p->endChangeGesture();
    return p->getName(256);
}

// These helpers intentionally share one selection map with the legacy
// last-touched fallback: both must select the same host parameter ID.
juce::AudioProcessorParameter* LSampler24AudioProcessor::getAutomationGridParameter(int slot,
    int gridIndex, int loop, bool bankMacro) const noexcept
{
    if (!hostParametersReady || !juce::isPositiveAndBelow(gridIndex, int(lsampler::grid.size()))) return nullptr;
    const auto& e = lsampler::grid[size_t(gridIndex)];
    if (e.action != lsampler::Action::none) return nullptr;
    if (bankMacro)
    {
        if (e.global >= 0 || e.loop >= 0 || !juce::isPositiveAndBelow(e.parameter, lsampler::parameterCount))
            return nullptr;
        return hostBankMacros[size_t(e.parameter)];
    }
    if (e.global >= 0)
        return juce::isPositiveAndBelow(e.global, lsampler::globalParameterCount)
            ? hostGlobals[size_t(e.global)] : nullptr;
    if (!juce::isPositiveAndBelow(slot, slotCount)) return nullptr;
    const auto& hs = hostSlots[size_t(slot)];
    if (e.loop >= 0)
        return juce::isPositiveAndBelow(e.loop, lsampler::loopParameterCount)
            ? hs.loops[size_t(juce::jlimit(0, lsampler::loopCount - 1, loop))][size_t(e.loop)]
            : nullptr;
    return juce::isPositiveAndBelow(e.parameter, lsampler::parameterCount)
        ? hs.values[size_t(e.parameter)] : nullptr;
}

juce::AudioProcessorParameter* LSampler24AudioProcessor::getAutomationSampleSetParameter(int slot,
    int sample, int page, int field) const noexcept
{
    if (!hostParametersReady || !juce::isPositiveAndBelow(slot, slotCount)) return nullptr;
    const auto& hs = hostSlots[size_t(slot)];
    if (page == 2) return hs.variation;
    if (page != 1 || !juce::isPositiveAndBelow(sample, sampleSetSize)) return nullptr;
    if (field == 0) return hs.velocityLow[size_t(sample)];
    if (field == 1) return hs.velocityHigh[size_t(sample)];
    return nullptr;
}

juce::AudioProcessorParameter* LSampler24AudioProcessor::getAutomationSliceGlobalParameter(int slot,
    int global) const noexcept
{
    if (!hostParametersReady || !juce::isPositiveAndBelow(slot, slotCount)
        || !juce::isPositiveAndBelow(global, int(lsampler::sliceGlobals.size()))) return nullptr;
    return hostSlots[size_t(slot)].slice[size_t(global)];
}

juce::String LSampler24AudioProcessor::touchAutomationGridParameter(int slot, int gridIndex,
                                                                     int loop, bool bankMacro)
{
    return touchAutomationParameter(static_cast<HostParameter*>(
        getAutomationGridParameter(slot, gridIndex, loop, bankMacro)));
}

juce::String LSampler24AudioProcessor::touchAutomationSampleSetParameter(int slot, int sample,
                                                                          int page, int field)
{
    return touchAutomationParameter(static_cast<HostParameter*>(
        getAutomationSampleSetParameter(slot, sample, page, field)));
}

juce::String LSampler24AudioProcessor::touchAutomationSliceGlobalParameter(int slot, int global)
{
    return touchAutomationParameter(static_cast<HostParameter*>(
        getAutomationSliceGlobalParameter(slot, global)));
}

void LSampler24AudioProcessor::notifyHostControl(HostParameter* p)
{
    if(shuttingDown.load(std::memory_order_acquire))return;
    if(auto* manager=juce::MessageManager::getInstanceWithoutCreating(); manager&&manager->isThisTheMessageThread()) {
        p->beginChangeGesture();
        // Equivalent to setValueNotifyingHost, without converting the original
        // double value through float and back or recursively invoking setValue.
        p->sendValueChangedMessageToListeners(p->getValue());
        p->endChangeGesture();
    } else {
        p->notificationPending.store(true,std::memory_order_release);
        triggerAsyncUpdate();
    }
}

void LSampler24AudioProcessor::handleAsyncUpdate()
{
    // UI/control writes only schedule this callback. Audio/host callbacks never
    // post messages. Notify the latest canonical value; a delayed notification
    // must never replay an old UI value over more recent DAW automation.
    for(auto* base:getParameters()) {
        auto* p=static_cast<HostParameter*>(base);
        if(p->notificationPending.exchange(false,std::memory_order_acq_rel)) {
            p->beginChangeGesture();
            p->sendValueChangedMessageToListeners(p->getValue());
            p->endChangeGesture();
        }
    }
}

void LSampler24AudioProcessor::updateAutomatedAudio(bool snapshotChanged)
{
    const auto& base=snapshots[size_t(readerSnapshot)].states;
    const auto macroGeneration = bankMacroHostGeneration.load(std::memory_order_acquire);
    const bool macroChanged = macroGeneration != audioBankMacroGeneration;
    audioBankMacroGeneration = macroGeneration;
    // The audio callback only reads lock-free atomics. No host notifications,
    // allocation, files, or state-lock acquisition occur here.
    std::array<double, parameterCount> macroOffsets {};
    if (macroChanged || snapshotChanged)
        for (int i = 0; i < parameterCount; ++i)
            if (auto* macro = hostBankMacros[size_t(i)])
                macroOffsets[size_t(i)] = macro->load();
    bool changed=snapshotChanged;
    for(int k=0;k<slotCount;++k) {
        const auto generation=hostGenerations[size_t(k)].load(std::memory_order_acquire);
        if(!snapshotChanged && !macroChanged && generation==audioHostGenerations[size_t(k)])continue;
        audioHostGenerations[size_t(k)]=generation;
        auto& out=automatedAudio[size_t(k)];const auto& original=base[size_t(k)];
        out=original;auto p=original.params;const auto& h=hostSlots[size_t(k)];
        for(int i=0;i<parameterCount;++i)if(auto* parameter=h.values[size_t(i)])p.values[size_t(i)]=parameter->load();
        for(int j=0;j<loopCount;++j)for(int i=0;i<loopParameterCount;++i)
            p.loops[size_t(j)][size_t(i)]=h.loops[size_t(j)][size_t(i)]->load();
        // Relative Bank Macros are a non-destructive overlay on top of each
        // slot's own (possibly host-automated) settings. New/empty slots are
        // unaffected until a sample actually occupies them.
        bool occupied = false;
        for (auto* sample : original.sampleSet) if (sample != nullptr) { occupied = true; break; }
        if (occupied) {
            for (int i = 0; i < parameterCount; ++i) {
                const double offset = (macroChanged || snapshotChanged) ? macroOffsets[size_t(i)]
                    : (hostBankMacros[size_t(i)] ? hostBankMacros[size_t(i)]->load() : 0.0);
                if (offset != 0.0)
                    p.values[size_t(i)] = sanitise(parameters[size_t(i)], p.values[size_t(i)] + offset);
            }
        }
        // Crossed envelopes have a deterministic safe window; host values remain
        // independent. Playback always clamps to at least one valid frame.
        p[P::sample_end]=std::max(p[P::sample_start],p[P::sample_end]);
        p[P::sample_play_start]=std::clamp(p[P::sample_play_start],p[P::sample_start],p[P::sample_end]);
        for(int lfo=0;lfo<2;++lfo) {
            const auto sync=lfo?P::lfo2_bpm_sync:P::lfo1_bpm_sync;
            const auto rate=lfo?P::lfo2_rate:P::lfo1_rate;
            if(p[sync]!=0)p[rate]=std::clamp(std::round(p[rate]*8)/8,.125,512.0);
        }
        bool parameterChanged=p.values!=original.params.values || p.loops!=original.params.loops;
        if(parameterChanged) {
            // Explicit cached bounds avoid calculateThresholdWindow's sample scan.
            // Automated start/end use raw non-destructive playback boundaries;
            // other controls keep the threshold-derived window exactly as loaded.
            const bool windowChanged=p[P::sample_start]!=original.params[P::sample_start]
                ||p[P::sample_end]!=original.params[P::sample_end];
            const auto bounds=[&](int j) {
                const auto& old=original.playback[size_t(j)];auto* sample=original.sampleSet[size_t(j)];
                std::array<int,2> result {old.start,old.start+old.length};
                if(windowChanged&&sample&&sample->audio.getNumSamples()>=2) {
                    const int n=sample->audio.getNumSamples();
                    result[0]=std::clamp(int(std::floor(n*p[P::sample_start]*.01)),0,n-2);
                    result[1]=std::clamp(int(std::floor(n*p[P::sample_end]*.01)),result[0]+1,n);
                }
                return result;
            };
            const auto first=bounds(0);
            out=prepareSlotAudioState(p,original.sample,preparedSampleRate,0,first[0],first[1]);
            out.sampleSet=original.sampleSet;
            for(int j=1;j<sampleSetSize;++j) {
                const auto b=bounds(j);
                out.playback[size_t(j)]=prepareSamplePlaybackState(p,original.sampleSet[size_t(j)],preparedSampleRate,b[0],b[1]);
            }
            out.slice=original.slice;
        }
        auto slice=original.slice.state;bool sliceChanged=false;
        for(int i=0;i<int(sliceGlobals.size());++i) {
            const double x=h.slice[size_t(i)]->load();sliceChanged|=slice.globals[size_t(i)]!=x;
            if(i==int(SliceG::division)&&slice.globals[size_t(i)]!=x)slice.setDivision(int(x));
            slice.globals[size_t(i)]=x;
        }
        if(sliceChanged||out.length!=original.length)out.slice=prepareAutomatedSliceAudio(slice,out.length);
        out.variationMode=int(h.variation->load());
        for(int j=0;j<sampleSetSize;++j) {
            const int low=int(h.velocityLow[size_t(j)]->load()),high=int(h.velocityHigh[size_t(j)]->load());
            out.sampleVelocityLow[size_t(j)]=uint8_t(std::min(low,high));
            out.sampleVelocityHigh[size_t(j)]=uint8_t(std::max(low,high));
        }
        out.revision=++audioAutomationRevision;changed=true;
    }
    if(changed)voicePool.setStates(&automatedAudio);
}
