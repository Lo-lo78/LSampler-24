#include "PluginProcessor.h"
#include "PluginEditor.h"

LSampler24AudioProcessor::LSampler24AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void LSampler24AudioProcessor::prepareToPlay(double sampleRate, int)
{
    voiceBank.prepare(sampleRate);
    applyCurrentSlotToVoiceBank();
}

void LSampler24AudioProcessor::releaseResources()
{
    voiceBank.allNotesOff();
}

bool LSampler24AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LSampler24AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    juce::MidiBuffer filtered;
    const int low = activeLowKey.load();
    const int high = activeHighKey.load();

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const int note = message.getNoteNumber();
            if (note >= low && note <= high)
                filtered.addEvent(message, metadata.samplePosition);
        }
        else
        {
            // Note Off and global note-off messages are always allowed through
            // so a previously sounding voice can never become stuck.
            filtered.addEvent(message, metadata.samplePosition);
        }
    }

    if (previewRequested.exchange(false))
    {
        const int previewNote = getRootNote();
        filtered.addEvent(juce::MidiMessage::noteOn(1, previewNote, (juce::uint8) 100), 0);
    }

    voiceBank.render(buffer, filtered);
}

juce::AudioProcessorEditor* LSampler24AudioProcessor::createEditor()
{
    return new LSampler24AudioProcessorEditor(*this);
}

void LSampler24AudioProcessor::applyCurrentSlotToVoiceBank()
{
    const auto& s = slots[static_cast<size_t>(currentSlot)];
    voiceBank.allNotesOff();
    voiceBank.setSample(s.sample);
    voiceBank.setRootNote(s.rootNote);
    voiceBank.setGain(s.volume);
    activeLowKey.store(s.lowKey);
    activeHighKey.store(s.highKey);
}

void LSampler24AudioProcessor::setCurrentSlot(int slotIndex)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    if (slotIndex == currentSlot)
        return;

    currentSlot = slotIndex;
    applyCurrentSlotToVoiceBank();
}

juce::String LSampler24AudioProcessor::getSlotLabel(int slotIndex) const
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    const juce::ScopedLock lock(stateLock);
    const auto& s = slots[static_cast<size_t>(slotIndex)];
    const auto name = s.sampleFile.getFileName();
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
        voiceBank.setSample(s.sample);
    }
    return true;
}

int LSampler24AudioProcessor::getLowKey() const noexcept
{
    return slots[static_cast<size_t>(currentSlot)].lowKey;
}

void LSampler24AudioProcessor::setLowKey(int note)
{
    auto& s = slots[static_cast<size_t>(currentSlot)];
    s.lowKey = juce::jlimit(0, 127, note);
    activeLowKey.store(s.lowKey);
}

int LSampler24AudioProcessor::getHighKey() const noexcept
{
    return slots[static_cast<size_t>(currentSlot)].highKey;
}

void LSampler24AudioProcessor::setHighKey(int note)
{
    auto& s = slots[static_cast<size_t>(currentSlot)];
    s.highKey = juce::jlimit(0, 127, note);
    activeHighKey.store(s.highKey);
}

int LSampler24AudioProcessor::getRootNote() const noexcept
{
    return slots[static_cast<size_t>(currentSlot)].rootNote;
}

void LSampler24AudioProcessor::setRootNote(int note)
{
    auto& s = slots[static_cast<size_t>(currentSlot)];
    s.rootNote = juce::jlimit(0, 127, note);
    voiceBank.setRootNote(s.rootNote);
}

float LSampler24AudioProcessor::getVolume() const noexcept
{
    return slots[static_cast<size_t>(currentSlot)].volume;
}

void LSampler24AudioProcessor::setVolume(float newVolume)
{
    auto& s = slots[static_cast<size_t>(currentSlot)];
    s.volume = juce::jlimit(0.0f, 1.0f, newVolume);
    voiceBank.setGain(s.volume);
    activeLowKey.store(s.lowKey);
    activeHighKey.store(s.highKey);
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
    auto& s = slots[static_cast<size_t>(slotIndex)];
    s.lowKey = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("lowKey", 0)));
    s.highKey = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("highKey", 127)));
    s.rootNote = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("rootNote", 60)));
    s.volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(tree.getProperty("volume", 1.0)));

    auto reference = tree.getProperty("sampleReference").toString();
    if (reference.isEmpty())
        reference = tree.getProperty("samplePath").toString();

    const juce::File file = library.resolveSampleReference(reference);
    if (file.getFullPathName().isEmpty())
    {
        s.sample.reset();
        s.sampleFile = {};
        s.status = "No sample loaded";
        if (slotIndex == currentSlot) applyCurrentSlotToVoiceBank();
        return true;
    }

    if (!file.existsAsFile())
    {
        s.sample.reset();
        s.sampleFile = file;
        s.status = "Sample missing: " + file.getFileName();
        error = s.status;
        if (slotIndex == currentSlot) applyCurrentSlotToVoiceBank();
        return false;
    }

    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded)
    {
        s.status = error;
        return false;
    }

    s.sample = std::move(loaded);
    s.sampleFile = file;
    s.status = "Loaded: " + file.getFileName();
    if (slotIndex == currentSlot) applyCurrentSlotToVoiceBank();
    return true;
}

bool LSampler24AudioProcessor::materialiseSlotSample(int slotIndex, juce::String& error)
{
    slotIndex = juce::jlimit(0, slotCount - 1, slotIndex);
    auto& s = slots[static_cast<size_t>(slotIndex)];
    if (s.sampleFile.getFullPathName().isEmpty())
        return true;

    auto local = library.materialiseSample(s.sampleFile, error);
    if (local.getFullPathName().isEmpty())
        return false;

    if (local != s.sampleFile)
    {
        auto loaded = SamplePool::instance().load(local, error);
        if (!loaded) return false;
        s.sample = std::move(loaded);
        s.sampleFile = local;
        s.status = "Loaded: " + local.getFileName();
        if (slotIndex == currentSlot) applyCurrentSlotToVoiceBank();
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
    for (int i = count; i < slotCount; ++i)
        slots[static_cast<size_t>(i)] = SlotState{};

    currentSlot = juce::jlimit(0, slotCount - 1, static_cast<int>(bank.getProperty("currentSlot", 0)));
    applyCurrentSlotToVoiceBank();
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
            applyCurrentSlotToVoiceBank();
        }
        else
        {
            // TEST1/TEST2 single-slot compatibility.
            restoreSlotState(0, state, error);
            currentSlot = 0;
            applyCurrentSlotToVoiceBank();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LSampler24AudioProcessor();
}
