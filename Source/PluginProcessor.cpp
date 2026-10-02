#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
using namespace lsampler;

LSampler24AudioProcessor::LSampler24AudioProcessor(const juce::File& libraryRootOverride)
    : AudioProcessor([] {
        BusesProperties buses;
        buses = buses.withOutput("Output", juce::AudioChannelSet::stereo(), true);
        for (int i = 1; i <= 24; ++i)
            buses = buses.withOutput("Out " + juce::String(2*i+1) + "/" + juce::String(2*i+2), juce::AudioChannelSet::stereo(), false);
        return buses;
      }()), library(libraryRootOverride)
{
    static_assert(std::atomic<int>::is_always_lock_free);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    markAudioStateDirty();
}

LSampler24AudioProcessor::~LSampler24AudioProcessor() { voicePool.allNotesOff(); }

void LSampler24AudioProcessor::prepareToPlay(double sampleRate, int)
{
    voicePool.prepare(sampleRate);
    {
        const juce::ScopedLock lock(stateLock);
        preparedSampleRate = sampleRate > 0 ? sampleRate : 44100;
        markAudioStateDirty();
    }
    syncAudioStateFromSlots();
}

void LSampler24AudioProcessor::releaseResources()
{
    voicePool.allNotesOff();
    previewPlaying = false;
    previewPlayingSlot = -1;
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

void LSampler24AudioProcessor::markAudioStateDirty()
{
    // This function is called only by control/state threads. No audio-thread lock,
    // shared_ptr update, allocation, or destruction is needed to consume a state.
    // Called by control/state code while its caller owns stateLock. The initial
    // constructor call happens before the processor is visible to another thread.
    auto& target = snapshots[size_t(writerSnapshot)];
    for (int i = 0; i < slotCount; ++i) {
        const auto idx = size_t(i);
        if (target.owners[idx] && target.owners[idx] != slots[idx].sample)
            retiredSamples.push_back(target.owners[idx]);
        target.owners[idx] = slots[idx].sample;
        target.states[idx] = prepareSlotAudioState(slots[idx].parameters, slots[idx].sample.get(), preparedSampleRate, ++nextRevision);
    }
    writerSnapshot = middleSnapshot.exchange(writerSnapshot | 4, std::memory_order_acq_rel) & 3;
    // Sample references held by voices are intrusive counters: deletion always occurs here.
    retiredSamples.erase(std::remove_if(retiredSamples.begin(), retiredSamples.end(), [](const auto& sample) {
        return sample->voiceReferences.load(std::memory_order_acquire) == 0;
    }), retiredSamples.end());
}

void LSampler24AudioProcessor::syncAudioStateFromSlots()
{
    if ((middleSnapshot.load(std::memory_order_acquire) & 4) != 0)
        readerSnapshot = middleSnapshot.exchange(readerSnapshot, std::memory_order_acq_rel) & 3;
    voicePool.setStates(&snapshots[size_t(readerSnapshot)].states);
}

void LSampler24AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    syncAudioStateFromSlots();
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
    const auto& audio = snapshots[size_t(readerSnapshot)].states;
    const auto stop = stopVoicesMask.exchange(0, std::memory_order_acq_rel);
    for (int i = 0; i < slotCount; ++i) if ((stop & (1u << i)) != 0) voicePool.stopSlotVoices(i);
    previewPlaying = voicePool.hasPreviewVoices();
    if (!previewPlaying) previewPlayingSlot = -1;

    if (previewStopRequested.exchange(false, std::memory_order_acq_rel)) {
        voicePool.stopPreviewVoices();
        previewPlaying = false;
        previewPlayingSlot = -1;
    }

    if (previewRestartRequested.exchange(false, std::memory_order_acq_rel)) {
        const int slot = juce::jlimit(0, slotCount - 1, previewTargetSlot.load());
        const bool shouldRestart = previewPlaying && previewPlayingSlot == slot;
        if (shouldRestart) {
            voicePool.stopPreviewVoices(slot);
            if (audio[size_t(slot)].sample) {
                voicePool.noteOn(slot, int(audio[size_t(slot)].params[P::root]), 1.0f, 0, true);
                previewPlaying = true;
                previewPlayingSlot = slot;
            } else {
                previewPlaying = false;
                previewPlayingSlot = -1;
            }
        }
    }

    double bpm = 120;
    if (auto* playHead = getPlayHead()) if (auto position = playHead->getPosition())
        if (auto tempo = position->getBpm()) if (std::isfinite(*tempo) && *tempo > 0) bpm = *tempo;
    voicePool.setTempo(bpm);
    std::array<int, 25> routes;
    routes.fill(-1);
    for (int i = 0; i < getBusCount(false) && i < int(routes.size()); ++i)
        if (auto* bus = getBus(false, i); bus != nullptr && bus->isEnabled())
            routes[size_t(i)] = bus->getChannelIndexInProcessBlockBuffer(0);
    voicePool.setOutputRoutes(routes);

    if (previewToggleRequested.exchange(false, std::memory_order_acq_rel)) {
        if (previewPlaying) { voicePool.stopPreviewVoices(); previewPlaying = false; previewPlayingSlot = -1; }
        else {
            voicePool.stopPreviewVoices();
            const int slot = juce::jlimit(0, slotCount-1, previewTargetSlot.load());
            if (audio[size_t(slot)].sample) {
                voicePool.noteOn(slot, int(audio[size_t(slot)].params[P::root]), 1.0f, 0, true);
                previewPlaying = true; previewPlayingSlot = slot;
            }
        }
    }
    // Process MIDI at its sample offset; getMessage() can allocate for SysEx, so
    // inspect the short MIDI bytes directly and ignore non-performance messages.
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
            for (int i = 0; i < slotCount; ++i) {
                const auto& s = audio[size_t(i)]; const auto& p = s.params;
                if (s.sample && note >= p[P::low] && note <= p[P::high]
                    && velocity >= p[P::velocity_low] && velocity <= p[P::velocity_high])
                    voicePool.noteOn(i, note, float(velocity)/127.0f, channel);
            }
        } else if (event.numBytes >= 3 && (command == 0x80 || (command == 0x90 && d[2] == 0)))
            voicePool.noteOff(d[1] & 127, channel);
        else if (event.numBytes >= 3 && command == 0xb0)
            voicePool.controller(channel, d[1] & 127, d[2] & 127);
        else if (event.numBytes >= 3 && command == 0xe0)
            voicePool.pitchBend(channel, (int(d[1] & 127) | (int(d[2] & 127) << 7)) - 8192);
    }
    voicePool.render(buffer, offset, buffer.getNumSamples()-offset);

    // F3-style import browser preview: a lightweight direct source audition.
    // It never changes slot state and performs no optional slot DSP.
    if (importPreviewPlaying && importPreview.sample != nullptr && buffer.getNumChannels() > 0)
    {
        const auto& src = importPreview.sample->audio;
        const int n = src.getNumSamples();
        const int channels = src.getNumChannels();
        const double sourceRate = importPreview.sample->sourceSampleRate > 0.0 ? importPreview.sample->sourceSampleRate : preparedSampleRate;
        const double inc = sourceRate / juce::jmax(1.0, preparedSampleRate);
        auto* outL = buffer.getWritePointer(0);
        auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (importPreviewPosition >= n - 1)
            {
                importPreviewPosition = juce::jmax(0.0, double(n - 1));
                importPreviewPlaying = false;
                importPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
                break;
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
    midi.clear();
    if (previewPlaying && !voicePool.hasPreviewVoices(previewPlayingSlot)) { previewPlaying = false; previewPlayingSlot = -1; }
}

juce::AudioProcessorEditor* LSampler24AudioProcessor::createEditor()
{
    return new LSampler24AudioProcessorEditor(*this);
}

void LSampler24AudioProcessor::setCurrentSlot(int slotIndex)
{
    currentSlot = juce::jlimit(0, slotCount - 1, slotIndex);
}

juce::String LSampler24AudioProcessor::getSlotLabel(int slotIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock);
    const auto& s = slots[static_cast<size_t>(slotIndex)];
    const auto name = s.sampleFile.getFileNameWithoutExtension();
    return "Slot " + juce::String(slotIndex + 1) + ", " + (name.isNotEmpty() ? name : "empty");
}

bool LSampler24AudioProcessor::loadSample(const juce::File& file, juce::String& error)
{
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded)
    {
        const juce::ScopedLock lock(stateLock);
        slots[static_cast<size_t>(currentSlot.load())].status = error;
        return false;
    }

    {
        const juce::ScopedLock lock(stateLock);
        auto& s = slots[static_cast<size_t>(currentSlot.load())];
        s.sample = std::move(loaded);
        s.sampleFile = file;
        s.status = "Loaded: " + file.getFileName();
        markAudioStateDirty();
    }
    return true;
}


bool LSampler24AudioProcessor::isSlotOccupied(int slotIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock);
    return slots[static_cast<size_t>(slotIndex)].sample != nullptr;
}

bool LSampler24AudioProcessor::importSampleToSlot(const juce::File& file, int slotIndex, double startSeconds, double endSeconds, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded) return false;

    const double sourceRate = loaded->sourceSampleRate > 0.0 ? loaded->sourceSampleRate : preparedSampleRate;
    const double totalSeconds = loaded->audio.getNumSamples() / juce::jmax(1.0, sourceRate);
    const bool hasSlice = endSeconds > startSeconds + 0.0005 && totalSeconds > 0.0;
    double startPct = 0.0, endPct = 100.0;
    if (hasSlice)
    {
        startSeconds = juce::jlimit(0.0, totalSeconds, startSeconds);
        endSeconds = juce::jlimit(startSeconds + 0.0005, totalSeconds, endSeconds);
        startPct = 100.0 * startSeconds / totalSeconds;
        endPct = 100.0 * endSeconds / totalSeconds;
    }

    {
        const juce::ScopedLock lock(stateLock);
        auto& slot = slots[static_cast<size_t>(slotIndex)];
        slot.sample = std::move(loaded);
        slot.sampleFile = file;
        slot.parameters = lsampler::SlotParameters{};
        slot.parameters[lsampler::P::root] = 60.0;
        slot.parameters[lsampler::P::low] = 0.0;
        slot.parameters[lsampler::P::high] = 127.0;
        slot.parameters[lsampler::P::sample_start] = startPct;
        slot.parameters[lsampler::P::sample_end] = endPct;
        slot.parameters[lsampler::P::sample_play_start] = 0.0;
        slot.status = "Loaded: " + file.getFileName();
        markAudioStateDirty();
    }
    return true;
}

bool LSampler24AudioProcessor::prepareImportPreview(const juce::File& file, juce::String& error)
{
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded) return false;
    auto& target = importPreviewSnapshots[static_cast<size_t>(importPreviewWriter)];
    target.owner = std::move(loaded);
    target.sample = target.owner.get();
    target.revision = ++importPreviewRevision;
    const double rate = target.sample->sourceSampleRate > 0.0 ? target.sample->sourceSampleRate : preparedSampleRate;
    importPreviewLengthSeconds.store(target.sample->audio.getNumSamples() / juce::jmax(1.0, rate), std::memory_order_relaxed);
    importPreviewWriter = importPreviewMiddle.exchange(importPreviewWriter | 4, std::memory_order_acq_rel) & 3;
    importPreviewSeekSeconds.store(0.0, std::memory_order_relaxed);
    importPreviewSeekRequested.store(true, std::memory_order_release);
    importPreviewStopRequested.store(true, std::memory_order_release);
    return true;
}

int LSampler24AudioProcessor::getLowKey() const noexcept { const juce::ScopedLock lock(stateLock); return int(slots[size_t(currentSlot.load())].parameters[P::low]); }
int LSampler24AudioProcessor::getHighKey() const noexcept { const juce::ScopedLock lock(stateLock); return int(slots[size_t(currentSlot.load())].parameters[P::high]); }
int LSampler24AudioProcessor::getRootNote() const noexcept { const juce::ScopedLock lock(stateLock); return int(slots[size_t(currentSlot.load())].parameters[P::root]); }
float LSampler24AudioProcessor::getVolume() const noexcept { const juce::ScopedLock lock(stateLock); return float(std::pow(10.0,slots[size_t(currentSlot.load())].parameters[P::input_gain]/20.0)); }
void LSampler24AudioProcessor::setLowKey(int n) { const juce::ScopedLock lock(stateLock); slots[size_t(currentSlot.load())].parameters[P::low]=juce::jlimit(0,127,n); markAudioStateDirty(); }
void LSampler24AudioProcessor::setHighKey(int n) { const juce::ScopedLock lock(stateLock); slots[size_t(currentSlot.load())].parameters[P::high]=juce::jlimit(0,127,n); markAudioStateDirty(); }
void LSampler24AudioProcessor::setRootNote(int n) { const juce::ScopedLock lock(stateLock); slots[size_t(currentSlot.load())].parameters[P::root]=juce::jlimit(0,127,n); markAudioStateDirty(); }
void LSampler24AudioProcessor::setVolume(float v) { const juce::ScopedLock lock(stateLock); slots[size_t(currentSlot.load())].parameters[P::input_gain]=v>0?20*std::log10(juce::jlimit(.000001f,1.0f,v)):-120; markAudioStateDirty(); }

double LSampler24AudioProcessor::getSlotParameter(int index, int loop) const {
    const juce::ScopedLock lock(stateLock);
    return slots[size_t(currentSlot.load())].parameters.get(grid[size_t(juce::jlimit(0,int(grid.size())-1,index))],juce::jlimit(0,9,loop));
}
void LSampler24AudioProcessor::setSlotParameter(int index,double value,int loop) {
    const auto& e=grid[size_t(juce::jlimit(0,int(grid.size())-1,index))];
    if (e.action!=Action::none) { if(value>=.5)applyZeroCrossing(e.action==Action::loopZero,loop);return; }
    const juce::ScopedLock lock(stateLock);
    auto& p=slots[size_t(currentSlot.load())].parameters;
    p.set(e,juce::jlimit(0,9,loop),value);
    for (int i=0;i<2;++i) {
        const P sync=i?P::lfo2_bpm_sync:P::lfo1_bpm_sync, rate=i?P::lfo2_rate:P::lfo1_rate;
        if (p[sync]!=0 && (e.parameter==int(sync)||e.parameter==int(rate)))
            p[rate]=std::clamp(std::round(p[rate]*8)/8,.125,512.0);
    }
    markAudioStateDirty();
}
void LSampler24AudioProcessor::resetSlotParameter(int index,int loop) {
    if (index<0 || index>=int(grid.size())) return;
    setSlotParameter(index,descriptor(grid[size_t(index)]).initial,loop);
}
void LSampler24AudioProcessor::applyZeroCrossing(bool loopWindow,int loopIndex) {
    // A native UI command: no audio-thread scan, bridge, pulse or request/result state.
    const juce::ScopedLock lock(stateLock);
    auto& slot=slots[size_t(currentSlot.load())]; if(!slot.sample || slot.sample->audio.getNumSamples()<2)return;
    auto& p=slot.parameters; const auto& a=slot.sample->audio;
    const int count=a.getNumSamples();
    const int first=loopWindow?std::clamp(int(count*p[P::sample_start]*.01),0,count-2):0;
    const int last=loopWindow?std::clamp(int(count*p[P::sample_end]*.01),first+1,count):count;
    auto snap=[&](double pct) {
        const int target=std::clamp(first+int(std::round((last-first)*pct*.01)),first,last-1);
        auto crosses=[&](int at) {
            if(at<=first||at>=last)return false;
            for(int ch=0;ch<a.getNumChannels();++ch) {
                const float x=a.getSample(ch,at-1),y=a.getSample(ch,at);
                if((x<=0&&y>=0)||(x>=0&&y<=0))return true;
            }
            return false;
        };
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
    } else { p[P::sample_start]=snap(p[P::sample_start]);p[P::sample_end]=snap(p[P::sample_end]); }
    markAudioStateDirty();
}

bool LSampler24AudioProcessor::copyCurrentSlot()
{
    const juce::ScopedLock lock(stateLock);
    slotClipboard = slots[static_cast<size_t>(currentSlot.load())];
    slotClipboardHasData = true;
    return true;
}

bool LSampler24AudioProcessor::cutCurrentSlot()
{
    const juce::ScopedLock lock(stateLock);
    slotClipboard = slots[static_cast<size_t>(currentSlot.load())];
    slotClipboardHasData = true;
    slots[static_cast<size_t>(currentSlot.load())] = SlotState{};
    markAudioStateDirty();
    stopVoicesMask.fetch_or(1u << currentSlot.load(std::memory_order_relaxed), std::memory_order_release);
    return true;
}

bool LSampler24AudioProcessor::pasteCurrentSlot()
{
    const juce::ScopedLock lock(stateLock);
    if (!slotClipboardHasData)
        return false;
    slots[static_cast<size_t>(currentSlot.load())] = slotClipboard;
    markAudioStateDirty();
    stopVoicesMask.fetch_or(1u << currentSlot.load(std::memory_order_relaxed), std::memory_order_release);
    return true;
}

void LSampler24AudioProcessor::clearCurrentSlot()
{
    const juce::ScopedLock lock(stateLock);
    slots[static_cast<size_t>(currentSlot.load())] = SlotState{};
    markAudioStateDirty();
    stopVoicesMask.fetch_or(1u << currentSlot.load(std::memory_order_relaxed), std::memory_order_release);
}

void LSampler24AudioProcessor::clearBank()
{
    const juce::ScopedLock lock(stateLock);
    for (auto& slot : slots)
        slot = SlotState{};
    markAudioStateDirty();
    stopVoicesMask.fetch_or(0xffffffu, std::memory_order_release);
}

juce::File LSampler24AudioProcessor::getCurrentSampleFile() const
{
    const juce::ScopedLock lock(stateLock);
    return slots[static_cast<size_t>(currentSlot.load())].sampleFile;
}

juce::String LSampler24AudioProcessor::getSampleStatus() const
{
    const juce::ScopedLock lock(stateLock);
    return slots[static_cast<size_t>(currentSlot.load())].status;
}

juce::ValueTree LSampler24AudioProcessor::makeSlotState(int slotIndex, const juce::String& type) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock);
    const auto& s = slots[static_cast<size_t>(slotIndex)];
    juce::ValueTree tree(type);
    tree.setProperty("format", "LSampler-24 Slot", nullptr);
    tree.setProperty("formatVersion", 3, nullptr);
    tree.setProperty("index", slotIndex, nullptr);
    tree.setProperty("sampleReference", library.makeSampleReference(s.sampleFile), nullptr);
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
    return tree;
}

bool LSampler24AudioProcessor::restoreSlotState(int slotIndex, const juce::ValueTree& tree, juce::String& error)
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

    auto reference = tree.getProperty("sampleReference").toString();
    if (reference.isEmpty())
        reference = tree.getProperty("samplePath").toString();

    const juce::File file = library.resolveSampleReference(reference);
    std::shared_ptr<SharedSample> loaded;
    juce::String statusText = "No sample loaded";
    bool ok = true;

    if (file.getFullPathName().isNotEmpty())
    {
        if (!file.existsAsFile())
        {
            statusText = "Sample missing: " + file.getFileName();
            error = statusText;
            ok = false;
        }
        else
        {
            loaded = SamplePool::instance().load(file, error);
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

    {
        const juce::ScopedLock lock(stateLock);
        auto& slot = slots[static_cast<size_t>(slotIndex)];
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
        slot.sample = std::move(loaded);
        slot.sampleFile = file;
        slot.status = statusText;
        markAudioStateDirty();
    }

    return ok;
}

bool LSampler24AudioProcessor::materialiseSlotSample(int slotIndex, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    juce::File source;
    {
        const juce::ScopedLock lock(stateLock);
        source = slots[static_cast<size_t>(slotIndex)].sampleFile;
    }

    if (source.getFullPathName().isEmpty())
        return true;

    auto local = library.materialiseSample(source, error);
    if (local.getFullPathName().isEmpty())
        return false;

    if (local != source)
    {
        std::shared_ptr<SharedSample> original;
        { const juce::ScopedLock lock(stateLock); original = slots[size_t(slotIndex)].sample; }
        auto loaded = original ? SamplePool::instance().aliasFile(local, original) : SamplePool::instance().load(local, error);
        if (!loaded) return false;

        const juce::ScopedLock lock(stateLock);
        auto& slot = slots[static_cast<size_t>(slotIndex)];
        slot.sample = std::move(loaded);
        slot.sampleFile = local;
        slot.status = "Loaded: " + local.getFileName();
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
    if (!materialiseSlotSample(currentSlot.load(std::memory_order_relaxed), error))
        return false;
    return writePreset(presetFile, makeSlotState(currentSlot.load(std::memory_order_relaxed), "LSampler24Slot"), error);
}

bool LSampler24AudioProcessor::loadSlotPreset(const juce::File& presetFile, juce::String& error)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }
    return restoreSlotState(currentSlot.load(std::memory_order_relaxed), tree, error);
}

bool LSampler24AudioProcessor::saveBankPreset(const juce::File& presetFile, juce::String& error)
{
    for (int i = 0; i < slotCount; ++i)
        if (!materialiseSlotSample(i, error))
            return false;

    juce::ValueTree bank("LSampler24Bank");
    bank.setProperty("format", "LSampler-24 Bank", nullptr);
    bank.setProperty("formatVersion", 3, nullptr);
    bank.setProperty("slotCount", slotCount, nullptr);
    bank.setProperty("currentSlot", currentSlot.load(), nullptr);

    for (int i = 0; i < slotCount; ++i)
        bank.addChild(makeSlotState(i, "Slot"), -1, nullptr);

    return writePreset(presetFile, bank, error);
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
    for (int i = 0; i < count; ++i)
    {
        juce::String slotError;
        if (!restoreSlotState(i, bank.getChild(i), slotError))
        {
            ok = false;
            if (firstError.isEmpty()) firstError = slotError;
        }
    }
    {
        const juce::ScopedLock lock(stateLock);
        for (int i = count; i < slotCount; ++i)
            slots[static_cast<size_t>(i)] = SlotState{};
    }

    currentSlot = juce::jlimit(0, slotCount - 1, static_cast<int>(bank.getProperty("currentSlot", 0)));
    markAudioStateDirty();
    error = firstError;
    return ok;
}

void LSampler24AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("LSampler24State");
    state.setProperty("formatVersion", 3, nullptr);
    state.setProperty("slotCount", slotCount, nullptr);
    state.setProperty("currentSlot", currentSlot.load(), nullptr);
    for (int i = 0; i < slotCount; ++i)
        state.addChild(makeSlotState(i, "Slot"), -1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void LSampler24AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        juce::String error;

        if (state.hasType("LSampler24State") && state.getNumChildren() > 0)
        {
            const int count = juce::jmin(slotCount, state.getNumChildren());
            for (int i = 0; i < count; ++i)
            {
                juce::String ignored;
                restoreSlotState(i, state.getChild(i), ignored);
            }
            currentSlot = juce::jlimit(0, slotCount - 1, static_cast<int>(state.getProperty("currentSlot", 0)));
            markAudioStateDirty();
        }
        else
        {
            // TEST1/TEST2 single-slot compatibility.
            restoreSlotState(0, state, error);
            currentSlot = 0;
            markAudioStateDirty();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LSampler24AudioProcessor();
}
