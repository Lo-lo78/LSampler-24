#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr int presetFormatVersion = 3;
}

LSampler24AudioProcessor::LSampler24AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    publishRuntimeSnapshot();
}

void LSampler24AudioProcessor::prepareToPlay(double sampleRate, int)
{
    voiceBank.prepare(sampleRate);
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
    auto snapshot = std::atomic_load(&runtimeSnapshot);
    if (snapshot)
        voiceBank.render(buffer, midi, *snapshot);
}

juce::AudioProcessorEditor* LSampler24AudioProcessor::createEditor()
{
    return new LSampler24AudioProcessorEditor(*this);
}

void LSampler24AudioProcessor::setCurrentSlot(int slot)
{
    currentSlot.store(juce::jlimit(0, slotCount - 1, slot));
}

LSampler24AudioProcessor::SlotState LSampler24AudioProcessor::getSlotState(int slot) const
{
    const juce::ScopedLock lock(stateLock);
    return slots[static_cast<size_t>(juce::jlimit(0, slotCount - 1, slot))];
}

juce::String LSampler24AudioProcessor::getSlotLabel(int slot) const
{
    const auto state = getSlotState(slot);
    const auto name = state.sampleFile.getFullPathName().isEmpty() ? juce::String("empty")
                                                                   : state.sampleFile.getFileName();
    return "Slot " + juce::String(slot + 1) + ", " + name;
}

juce::String LSampler24AudioProcessor::noteName(int midiNote)
{
    static const char* names[] = { "C", "C sharp", "D", "D sharp", "E", "F",
                                   "F sharp", "G", "G sharp", "A", "A sharp", "B" };
    const int n = juce::jlimit(0, 127, midiNote);
    return juce::String(n) + " " + names[n % 12] + " " + juce::String(n / 12 - 1);
}

std::shared_ptr<const SamplerRuntimeState> LSampler24AudioProcessor::buildRuntimeSnapshot() const
{
    auto snapshot = std::make_shared<SamplerRuntimeState>();
    const juce::ScopedLock lock(stateLock);
    for (int i = 0; i < slotCount; ++i)
    {
        const auto& src = slots[static_cast<size_t>(i)];
        auto& dst = snapshot->slots[static_cast<size_t>(i)];
        dst.sample = src.sample;
        dst.lowKey = src.lowKey;
        dst.highKey = src.highKey;
        dst.originalPitch = src.originalPitch;
        dst.volume = src.volume;
        dst.mono = src.voiceMode == 1;
        dst.legato = src.monoMode == 1;
    }
    return snapshot;
}

void LSampler24AudioProcessor::publishRuntimeSnapshot()
{
    std::atomic_store(&runtimeSnapshot, buildRuntimeSnapshot());
}

bool LSampler24AudioProcessor::loadSample(const juce::File& file, juce::String& error)
{
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded)
        return false;

    const int slot = getCurrentSlot();
    {
        const juce::ScopedLock lock(stateLock);
        auto& state = slots[static_cast<size_t>(slot)];
        state.sample = std::move(loaded);
        state.sampleFile = file;
        state.status = "Loaded: " + file.getFileName();
    }
    publishRuntimeSnapshot();
    return true;
}

void LSampler24AudioProcessor::setLowKey(int value)
{
    {
        const juce::ScopedLock lock(stateLock);
        auto& s = slots[static_cast<size_t>(getCurrentSlot())];
        s.lowKey = juce::jlimit(0, s.highKey, value);
    }
    publishRuntimeSnapshot();
}

void LSampler24AudioProcessor::setHighKey(int value)
{
    {
        const juce::ScopedLock lock(stateLock);
        auto& s = slots[static_cast<size_t>(getCurrentSlot())];
        s.highKey = juce::jlimit(s.lowKey, 127, value);
    }
    publishRuntimeSnapshot();
}

void LSampler24AudioProcessor::setOriginalPitch(int value)
{
    {
        const juce::ScopedLock lock(stateLock);
        slots[static_cast<size_t>(getCurrentSlot())].originalPitch = juce::jlimit(0, 127, value);
    }
    publishRuntimeSnapshot();
}

void LSampler24AudioProcessor::setVoiceMode(int value)
{
    {
        const juce::ScopedLock lock(stateLock);
        slots[static_cast<size_t>(getCurrentSlot())].voiceMode = juce::jlimit(0, 1, value);
    }
    publishRuntimeSnapshot();
}

void LSampler24AudioProcessor::setMonoMode(int value)
{
    {
        const juce::ScopedLock lock(stateLock);
        slots[static_cast<size_t>(getCurrentSlot())].monoMode = juce::jlimit(0, 1, value);
    }
    publishRuntimeSnapshot();
}

void LSampler24AudioProcessor::setVolume(float value)
{
    {
        const juce::ScopedLock lock(stateLock);
        slots[static_cast<size_t>(getCurrentSlot())].volume = juce::jlimit(0.0f, 1.0f, value);
    }
    publishRuntimeSnapshot();
}

juce::String LSampler24AudioProcessor::getSampleStatus() const
{
    const auto s = getSlotState(getCurrentSlot());
    return "Slot " + juce::String(getCurrentSlot() + 1) + ". " + s.status;
}

juce::ValueTree LSampler24AudioProcessor::makeSlotState(int slot, const juce::String& type) const
{
    const auto s = getSlotState(slot);
    juce::ValueTree tree(type);
    tree.setProperty("index", slot, nullptr);
    tree.setProperty("formatVersion", presetFormatVersion, nullptr);
    tree.setProperty("sampleReference", library.makeSampleReference(s.sampleFile), nullptr);
    tree.setProperty("lowKey", s.lowKey, nullptr);
    tree.setProperty("highKey", s.highKey, nullptr);
    tree.setProperty("originalPitch", s.originalPitch, nullptr);
    tree.setProperty("rootNote", s.originalPitch, nullptr); // compatibility alias
    tree.setProperty("voiceMode", s.voiceMode, nullptr);
    tree.setProperty("monoMode", s.monoMode, nullptr);
    tree.setProperty("volume", s.volume, nullptr);
    return tree;
}

bool LSampler24AudioProcessor::restoreSlotState(int slot, const juce::ValueTree& tree, juce::String& error)
{
    if (!tree.isValid())
    {
        error = "Invalid preset";
        return false;
    }

    SlotState next;
    next.lowKey = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("lowKey", 0)));
    next.highKey = juce::jlimit(next.lowKey, 127, static_cast<int>(tree.getProperty("highKey", 127)));
    next.originalPitch = juce::jlimit(0, 127, static_cast<int>(tree.getProperty(
        "originalPitch", tree.getProperty("rootNote", 60))));
    next.voiceMode = juce::jlimit(0, 1, static_cast<int>(tree.getProperty("voiceMode", 0)));
    next.monoMode = juce::jlimit(0, 1, static_cast<int>(tree.getProperty("monoMode", 0)));
    next.volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(tree.getProperty("volume", 1.0f)));

    auto reference = tree.getProperty("sampleReference").toString();
    if (reference.isEmpty())
        reference = tree.getProperty("samplePath").toString();

    const juce::File file = library.resolveSampleReference(reference);
    if (reference.isNotEmpty())
    {
        if (!file.existsAsFile())
        {
            next.sampleFile = file;
            next.status = "Sample missing: " + file.getFileName();
            {
                const juce::ScopedLock lock(stateLock);
                slots[static_cast<size_t>(slot)] = std::move(next);
            }
            publishRuntimeSnapshot();
            error = "Sample missing: " + file.getFileName();
            return false;
        }

        auto loaded = SamplePool::instance().load(file, error);
        if (!loaded)
            return false;
        next.sample = std::move(loaded);
        next.sampleFile = file;
        next.status = "Loaded: " + file.getFileName();
    }
    else
    {
        next.status = "empty";
    }

    {
        const juce::ScopedLock lock(stateLock);
        slots[static_cast<size_t>(slot)] = std::move(next);
    }
    publishRuntimeSnapshot();
    return true;
}

bool LSampler24AudioProcessor::materialiseSlotSample(int slot, juce::String& error)
{
    const auto state = getSlotState(slot);
    if (state.sampleFile.getFullPathName().isEmpty())
        return true;

    auto local = library.materialiseSample(state.sampleFile, error);
    if (local.getFullPathName().isEmpty())
        return false;
    if (local == state.sampleFile)
        return true;

    auto loaded = SamplePool::instance().load(local, error);
    if (!loaded)
        return false;

    {
        const juce::ScopedLock lock(stateLock);
        auto& dst = slots[static_cast<size_t>(slot)];
        dst.sample = std::move(loaded);
        dst.sampleFile = local;
        dst.status = "Loaded: " + local.getFileName();
    }
    publishRuntimeSnapshot();
    return true;
}

bool LSampler24AudioProcessor::writePreset(const juce::File& file, const juce::ValueTree& tree,
                                           juce::String& error) const
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
    const int slot = getCurrentSlot();
    if (!materialiseSlotSample(slot, error))
        return false;
    auto tree = makeSlotState(slot, "LSampler24Slot");
    tree.setProperty("format", "LSampler-24 Slot", nullptr);
    return writePreset(presetFile, tree, error);
}

bool LSampler24AudioProcessor::loadSlotPreset(const juce::File& presetFile, juce::String& error)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }
    return restoreSlotState(getCurrentSlot(), tree, error);
}

bool LSampler24AudioProcessor::saveBankPreset(const juce::File& presetFile, juce::String& error)
{
    for (int slot = 0; slot < slotCount; ++slot)
        if (!materialiseSlotSample(slot, error))
            return false;

    juce::ValueTree bank("LSampler24Bank");
    bank.setProperty("format", "LSampler-24 Bank", nullptr);
    bank.setProperty("formatVersion", presetFormatVersion, nullptr);
    bank.setProperty("slotCount", slotCount, nullptr);
    for (int slot = 0; slot < slotCount; ++slot)
        bank.addChild(makeSlotState(slot, "Slot"), -1, nullptr);
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
    for (int slot = 0; slot < count; ++slot)
    {
        juce::String slotError;
        if (!restoreSlotState(slot, bank.getChild(slot), slotError))
        {
            ok = false;
            if (firstError.isEmpty()) firstError = slotError;
        }
    }
    for (int slot = count; slot < slotCount; ++slot)
    {
        juce::ValueTree empty("Slot");
        restoreSlotState(slot, empty, firstError);
    }
    if (!ok) error = firstError;
    return ok;
}

void LSampler24AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("LSampler24State");
    state.setProperty("formatVersion", presetFormatVersion, nullptr);
    state.setProperty("currentSlot", getCurrentSlot(), nullptr);
    for (int slot = 0; slot < slotCount; ++slot)
        state.addChild(makeSlotState(slot, "Slot"), -1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void LSampler24AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml)
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    juce::String error;
    if (state.hasType("LSampler24State") && state.getNumChildren() > 0)
    {
        const int count = juce::jmin(slotCount, state.getNumChildren());
        for (int slot = 0; slot < count; ++slot)
            restoreSlotState(slot, state.getChild(slot), error);
        setCurrentSlot(static_cast<int>(state.getProperty("currentSlot", 0)));
        return;
    }

    // TEST1/TEST2 compatibility: their project state described only slot 1.
    if (state.hasType("LSampler24State"))
        restoreSlotState(0, state, error);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LSampler24AudioProcessor();
}
