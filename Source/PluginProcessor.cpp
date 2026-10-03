#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <set>
#include <juce_cryptography/juce_cryptography.h>
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
    libraryPreviewVoicePool.prepare(sampleRate);
    libraryPreviewVoicePool.setStates(&libraryPreviewStates);
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
    libraryPreviewVoicePool.allNotesOff();
    libraryPreviewPlaying = false;
    libraryPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
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

    if (previewAuditionRequested.exchange(false, std::memory_order_acq_rel)) {
        const int slot = juce::jlimit(0, slotCount - 1, previewTargetSlot.load());
        const bool shouldRestart = previewPlaying && previewPlayingSlot == slot;
        if (shouldRestart) {
            voicePool.stopPreviewVoices(slot);
            if (audio[size_t(slot)].sample) {
                const double startPercent = previewAuditionStartPercent.load(std::memory_order_relaxed);
                voicePool.noteOn(slot, int(audio[size_t(slot)].params[P::root]), 1.0f, 0, true, startPercent);
                previewPlaying = true;
                previewPlayingSlot = slot;
            } else {
                previewPlaying = false;
                previewPlayingSlot = -1;
            }
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
            libraryPreviewVoicePool.noteOn(0, libraryPreviewNote, 1.0f, 0, true);
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
        const juce::ScopedLock lock(stateLock);
        auto& slot = slots[static_cast<size_t>(slotIndex)];
        slot.sample = std::move(loaded);
        slot.sampleFile = file;
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
    importPreviewLoopEnabled.store(false, std::memory_order_release);
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
double LSampler24AudioProcessor::getSamplePlayStart() const {
    const juce::ScopedLock lock(stateLock);
    return slots[size_t(currentSlot.load())].parameters[P::sample_play_start];
}
void LSampler24AudioProcessor::setSamplePlayStart(double value) {
    const juce::ScopedLock lock(stateLock);
    auto& p = slots[size_t(currentSlot.load())].parameters;
    p[P::sample_play_start] = juce::jlimit(parameters[static_cast<size_t>(P::sample_play_start)].minimum,
                                           parameters[static_cast<size_t>(P::sample_play_start)].maximum, value);
    markAudioStateDirty();
}
void LSampler24AudioProcessor::requestSampleBoundaryAudition(bool endBoundary)
{
    double startPercent = 0.0;
    {
        const juce::ScopedLock lock(stateLock);
        const auto& slot = slots[size_t(currentSlot.load())];
        if (!slot.sample || slot.sample->audio.getNumSamples() < 2)
            return;

        const auto& p = slot.parameters;
        // Sample Start auditions from the new Start itself. Sample End is deliberately
        // independent: it auditions from the current Sample Play Start, which the
        // user positions with Ctrl+Left/Right, so tail editing is deterministic.
        startPercent = endBoundary ? p[P::sample_play_start] : p[P::sample_start];
    }
    requestPreviewAuditionFromPercent(startPercent);
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
        slot.sample = std::move(trimmed);
        p[P::sample_start] = 0.0;
        p[P::sample_end] = 100.0;
        p[P::sample_play_start] = 0.0;
    }
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

    const auto sampleHash = tree.getProperty("sampleHash").toString();
    const juce::File file = library.resolveSampleReference(reference, sampleHash);
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
    const int slotIndex = currentSlot.load(std::memory_order_relaxed);
    const bool ok = restoreSlotState(slotIndex, tree, error);
    if (ok)
    {
        juce::File actual;
        { const juce::ScopedLock lock(stateLock); actual = slots[size_t(slotIndex)].sampleFile; }
        if (actual.existsAsFile())
        {
            tree.setProperty("sampleReference", library.makeSampleReference(actual), nullptr);
            tree.setProperty("sampleHash", library.makeSampleHash(actual), nullptr);
            juce::String ignored; writePreset(presetFile, tree, ignored);
        }
    }
    return ok;
}

bool LSampler24AudioProcessor::loadSlotPresetToSlot(const juce::File& presetFile, int slotIndex, juce::String& error)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const bool ok = restoreSlotState(slotIndex, tree, error);
    if (ok)
    {
        juce::File actual;
        { const juce::ScopedLock lock(stateLock); actual = slots[size_t(slotIndex)].sampleFile; }
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

    libraryPreviewVoicePool.stopPreviewVoices();
    libraryPreviewOwners.fill({});
    libraryPreviewStates = {};
    libraryPreviewOwners[0] = loaded;
    libraryPreviewStates[0] = lsampler::prepareSlotAudioState(params, loaded.get(), preparedSampleRate, ++nextRevision);
    libraryPreviewVoicePool.setStates(&libraryPreviewStates);
    const int low = int(params[P::low]), high = int(params[P::high]), root = int(params[P::root]);
    libraryPreviewNote = (high - low > 36) ? root : juce::jlimit(low, high, (low + high) / 2);
    libraryPreviewPlaying = false;
    libraryPreviewPlayingAtomic.store(false, std::memory_order_relaxed);
    return true;
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


bool LSampler24AudioProcessor::importFilesToLibrary(const juce::Array<juce::File>& sourceFiles,
                                                     int& importedSlots,
                                                     int& skippedFiles,
                                                     juce::String& error)
{
    importedSlots = 0;
    skippedFiles = 0;
    error.clear();

    bool foundSupportedAudio = false;
    for (const auto& source : sourceFiles)
    {
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

    auto collectionName = juce::File::createLegalFileName(sourceFolder.getFileName()).trim();
    if (collectionName.isEmpty())
        collectionName = "Imported";

    const auto slotCollectionRoot = library.slots().getChildFile(collectionName);
    const auto sampleCollectionRoot = library.samples().getChildFile(collectionName);
    slotCollectionRoot.createDirectory();
    sampleCollectionRoot.createDirectory();

    for (const auto& source : files)
    {
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

bool LSampler24AudioProcessor::exportLibraryArchive(const juce::File& requestedTarget,
                                                     int& exportedSlots,
                                                     int& exportedSamples,
                                                     juce::String& error)
{
    exportedSlots = 0;
    exportedSamples = 0;
    error.clear();

    juce::Array<juce::File> slotFiles;
    library.slots().findChildFiles(slotFiles, juce::File::findFiles, true, "*" + juce::String(LibraryManager::slotExtension));

    struct ExportPair { juce::File slot; juce::File sample; juce::String slotPath; juce::String samplePath; };
    std::vector<ExportPair> valid;
    std::set<juce::String> seenSamples;

    for (const auto& slotFile : slotFiles)
    {
        juce::String readError;
        auto tree = readPreset(slotFile, readError);
        if (!tree.hasType("LSampler24Slot"))
            continue;

        auto reference = tree.getProperty("sampleReference").toString();
        if (reference.isEmpty())
            reference = tree.getProperty("samplePath").toString();
        if (reference.isEmpty())
            continue;

        auto sample = library.resolveSampleReference(reference, tree.getProperty("sampleHash").toString());
        if (!sample.existsAsFile() || !sample.isAChildOf(library.samples()))
            continue;

        const auto slotRelative = slotFile.getRelativePathFrom(library.slots()).replaceCharacter('\\', '/');
        const auto sampleRelative = sample.getRelativePathFrom(library.samples()).replaceCharacter('\\', '/');
        valid.push_back({ slotFile, sample, "Library/Slots/" + slotRelative, "Library/Samples/" + sampleRelative });
    }

    if (valid.empty())
    {
        error = "No LSampler-24 library items found";
        return false;
    }

    juce::File target = requestedTarget;
    if (target.getFileExtension().toLowerCase() != ".ls24")
        target = target.withFileExtension(".lsampler-24.ls24");
    else if (!target.getFileName().endsWithIgnoreCase(".lsampler-24.ls24"))
        target = target.getSiblingFile(target.getFileNameWithoutExtension() + ".lsampler-24.ls24");

    target.getParentDirectory().createDirectory();
    if (target.existsAsFile() && !target.deleteFile())
    {
        error = "Could not replace export file";
        return false;
    }

    juce::ZipFile::Builder builder;
    for (const auto& pair : valid)
    {
        builder.addFile(pair.slot, 9, pair.slotPath);
        ++exportedSlots;

        const auto sampleKey = pair.sample.getFullPathName().replaceCharacter('\\', '/').toLowerCase();
        if (seenSamples.insert(sampleKey).second)
        {
            builder.addFile(pair.sample, 0, pair.samplePath);
            ++exportedSamples;
        }
    }

    auto stream = target.createOutputStream();
    if (stream == nullptr)
    {
        error = "Could not create export file";
        return false;
    }

    if (!builder.writeToStream(*stream, nullptr))
    {
        error = "Could not write LSampler-24 library export";
        target.deleteFile();
        return false;
    }

    stream->flush();
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

    for (const auto& sourceSlot : slotFiles)
    {
        juce::String localError;
        auto tree = readPreset(sourceSlot, localError);
        if (!tree.hasType("LSampler24Slot"))
        {
            ++skippedItems;
            continue;
        }

        auto reference = tree.getProperty("sampleReference").toString();
        if (reference.isEmpty())
            reference = tree.getProperty("samplePath").toString();
        if (reference.isEmpty())
        {
            ++skippedItems;
            continue;
        }

        juce::File archivedSample;
        if (juce::File::isAbsolutePath(reference))
        {
            // Exported libraries are self-contained; absolute external references are not accepted.
            ++skippedItems;
            continue;
        }
        auto normalised = reference.replaceCharacter('\\', '/');
        if (normalised.startsWithIgnoreCase("Library/Samples/"))
            archivedSample = tempRoot.getChildFile(normalised.replaceCharacter('/', juce::File::getSeparatorChar()));
        else
            archivedSample = importedSamplesDir.getChildFile(juce::File(normalised).getFileName());

        if (!archivedSample.existsAsFile())
        {
            ++skippedItems;
            continue;
        }

        auto archivedSampleRelative = archivedSample.getRelativePathFrom(importedSamplesDir).replaceCharacter('\\', '/');
        auto desiredLocalSample = library.samples().getChildFile(archivedSampleRelative.replaceCharacter('/', juce::File::getSeparatorChar()));
        const bool sampleAlreadyPresent = desiredLocalSample.existsAsFile()
            && desiredLocalSample.getSize() == archivedSample.getSize()
            && juce::SHA256(desiredLocalSample).toHexString() == juce::SHA256(archivedSample).toHexString();

        auto localSample = library.materialiseSampleAtRelativePath(archivedSample, archivedSampleRelative, localError);
        if (!localSample.existsAsFile())
        {
            ++skippedItems;
            continue;
        }
        if (!sampleAlreadyPresent)
            ++importedSamples;

        tree.setProperty("sampleReference", library.makeSampleReference(localSample), nullptr);
        tree.setProperty("sampleHash", library.makeSampleHash(localSample), nullptr);
        if (tree.hasProperty("samplePath"))
            tree.removeProperty("samplePath", nullptr);

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
