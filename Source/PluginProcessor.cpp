#include "PluginProcessor.h"
#include "PluginEditor.h"

LSampler24AudioProcessor::LSampler24AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void LSampler24AudioProcessor::prepareToPlay(double sampleRate, int)
{
    voicePool.prepare(sampleRate);
    appliedAudioStateRevision = 0;
    syncAudioStateFromSlots();
}

void LSampler24AudioProcessor::releaseResources()
{
    voicePool.allNotesOff();
    previewPlaying = false;
    previewPlayingSlot = -1;
}

bool LSampler24AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LSampler24AudioProcessor::markAudioStateDirty() noexcept
{
    audioStateRevision.fetch_add(1, std::memory_order_release);
}

void LSampler24AudioProcessor::syncAudioStateFromSlots()
{
    const auto requested = audioStateRevision.load(std::memory_order_acquire);
    if (requested == appliedAudioStateRevision)
        return;

    {
        const juce::ScopedLock lock(stateLock);
        for (int i = 0; i < slotCount; ++i)
        {
            const auto& slot = slots[static_cast<size_t>(i)];
            audioSamples[static_cast<size_t>(i)] = slot.sample;
            audioRootNotes[static_cast<size_t>(i)] = slot.rootNote;
            audioGains[static_cast<size_t>(i)] = slot.volume;
            audioLowKeys[static_cast<size_t>(i)] = slot.lowKey;
            audioHighKeys[static_cast<size_t>(i)] = slot.highKey;
        }
    }

    appliedAudioStateRevision = requested;
}

void LSampler24AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    syncAudioStateFromSlots();

    const bool previewRequest = previewToggleRequested.exchange(false);
    const int requestedPreviewSlot = juce::jlimit(0, slotCount - 1, previewTargetSlot.load());

    if (previewRequest)
    {
        if (previewPlaying)
        {
            voicePool.stopPreviewVoices(previewPlayingSlot);
            previewPlaying = false;
            previewPlayingSlot = -1;
        }
        else
        {
            voicePool.stopPreviewVoices();
            const auto idx = static_cast<size_t>(requestedPreviewSlot);
            if (audioSamples[idx])
            {
                voicePool.noteOn(requestedPreviewSlot, audioRootNotes[idx], 1.0f,
                                 audioSamples[idx], audioRootNotes[idx], audioGains[idx], true);
                previewPlaying = true;
                previewPlayingSlot = requestedPreviewSlot;
            }
        }
    }

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const int note = message.getNoteNumber();
            const float velocity = message.getFloatVelocity();
            for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
            {
                const auto idx = static_cast<size_t>(slotIndex);
                if (note >= audioLowKeys[idx] && note <= audioHighKeys[idx] && audioSamples[idx])
                    voicePool.noteOn(slotIndex, note, velocity, audioSamples[idx],
                                     audioRootNotes[idx], audioGains[idx], false);
            }
        }
        else if (message.isNoteOff())
        {
            voicePool.noteOff(message.getNoteNumber());
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            voicePool.allNotesOff();
            previewPlaying = false;
            previewPlayingSlot = -1;
        }
    }

    voicePool.render(buffer);

    if (previewPlaying && !voicePool.hasPreviewVoices(previewPlayingSlot))
    {
        previewPlaying = false;
        previewPlayingSlot = -1;
    }
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
        slots[static_cast<size_t>(currentSlot)].status = error;
        return false;
    }

    {
        const juce::ScopedLock lock(stateLock);
        auto& s = slots[static_cast<size_t>(currentSlot)];
        s.sample = std::move(loaded);
        s.sampleFile = file;
        s.status = "Loaded: " + file.getFileName();
        markAudioStateDirty();
    }
    return true;
}

int LSampler24AudioProcessor::getLowKey() const noexcept
{
    return slots[static_cast<size_t>(currentSlot)].lowKey;
}

void LSampler24AudioProcessor::setLowKey(int note)
{
    const juce::ScopedLock lock(stateLock);
    auto& s = slots[static_cast<size_t>(currentSlot)];
    s.lowKey = juce::jlimit(0, 127, note);
    markAudioStateDirty();
}

int LSampler24AudioProcessor::getHighKey() const noexcept
{
    return slots[static_cast<size_t>(currentSlot)].highKey;
}

void LSampler24AudioProcessor::setHighKey(int note)
{
    const juce::ScopedLock lock(stateLock);
    auto& s = slots[static_cast<size_t>(currentSlot)];
    s.highKey = juce::jlimit(0, 127, note);
    markAudioStateDirty();
}

int LSampler24AudioProcessor::getRootNote() const noexcept
{
    return slots[static_cast<size_t>(currentSlot)].rootNote;
}

void LSampler24AudioProcessor::setRootNote(int note)
{
    const juce::ScopedLock lock(stateLock);
    auto& s = slots[static_cast<size_t>(currentSlot)];
    s.rootNote = juce::jlimit(0, 127, note);
    markAudioStateDirty();
}

float LSampler24AudioProcessor::getVolume() const noexcept
{
    return slots[static_cast<size_t>(currentSlot)].volume;
}

void LSampler24AudioProcessor::setVolume(float newVolume)
{
    const juce::ScopedLock lock(stateLock);
    auto& s = slots[static_cast<size_t>(currentSlot)];
    s.volume = juce::jlimit(0.0f, 1.0f, newVolume);
    markAudioStateDirty();
}

juce::File LSampler24AudioProcessor::getCurrentSampleFile() const
{
    const juce::ScopedLock lock(stateLock);
    return slots[static_cast<size_t>(currentSlot)].sampleFile;
}

juce::String LSampler24AudioProcessor::getSampleStatus() const
{
    const juce::ScopedLock lock(stateLock);
    return slots[static_cast<size_t>(currentSlot)].status;
}

juce::ValueTree LSampler24AudioProcessor::makeSlotState(int slotIndex, const juce::String& type) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const auto& s = slots[static_cast<size_t>(slotIndex)];
    juce::ValueTree tree(type);
    tree.setProperty("format", "LSampler-24 Slot", nullptr);
    tree.setProperty("formatVersion", 2, nullptr);
    tree.setProperty("index", slotIndex, nullptr);
    tree.setProperty("sampleReference", library.makeSampleReference(s.sampleFile), nullptr);
    tree.setProperty("lowKey", s.lowKey, nullptr);
    tree.setProperty("highKey", s.highKey, nullptr);
    tree.setProperty("rootNote", s.rootNote, nullptr);
    tree.setProperty("volume", s.volume, nullptr);
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
        slot.lowKey = lowKey;
        slot.highKey = highKey;
        slot.rootNote = rootNote;
        slot.volume = volume;
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
        auto loaded = SamplePool::instance().load(local, error);
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
    if (!materialiseSlotSample(currentSlot, error))
        return false;
    return writePreset(presetFile, makeSlotState(currentSlot, "LSampler24Slot"), error);
}

bool LSampler24AudioProcessor::loadSlotPreset(const juce::File& presetFile, juce::String& error)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }
    return restoreSlotState(currentSlot, tree, error);
}

bool LSampler24AudioProcessor::saveBankPreset(const juce::File& presetFile, juce::String& error)
{
    for (int i = 0; i < slotCount; ++i)
        if (!materialiseSlotSample(i, error))
            return false;

    juce::ValueTree bank("LSampler24Bank");
    bank.setProperty("format", "LSampler-24 Bank", nullptr);
    bank.setProperty("formatVersion", 2, nullptr);
    bank.setProperty("slotCount", slotCount, nullptr);
    bank.setProperty("currentSlot", currentSlot, nullptr);

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
    state.setProperty("formatVersion", 2, nullptr);
    state.setProperty("slotCount", slotCount, nullptr);
    state.setProperty("currentSlot", currentSlot, nullptr);
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
