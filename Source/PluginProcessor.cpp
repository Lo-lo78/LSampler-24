#include "PluginProcessor.h"
#include "PluginEditor.h"

LSampler24AudioProcessor::LSampler24AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // TEST4: new slots start open across the full MIDI keyboard.
    // MIDI has 128 notes numbered 0..127. Original Pitch is neutral at 60 (C4).
    for (int i = 0; i < slotCount; ++i)
    {
        auto& slot = slots[(size_t) i];
        slot.lowKey = 0;
        slot.highKey = 127;
        slot.originalNote = 60;
    }
}

void LSampler24AudioProcessor::prepareToPlay(double sampleRate, int)
{
    voiceBank.prepare(sampleRate);
    syncVoiceBank();
}

void LSampler24AudioProcessor::releaseResources() { voiceBank.allNotesOff(); }

bool LSampler24AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LSampler24AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    voiceBank.render(buffer, midi);
}

juce::AudioProcessorEditor* LSampler24AudioProcessor::createEditor()
{
    return new LSampler24AudioProcessorEditor(*this);
}

void LSampler24AudioProcessor::syncVoiceBank()
{
    std::array<SlotPlaybackState, VoiceBank::slotCount> playback {};
    const juce::ScopedLock lock(stateLock);
    for (int i = 0; i < slotCount; ++i)
    {
        const auto& s = slots[(size_t) i];
        auto& p = playback[(size_t) i];
        p.sample = s.sample;
        p.lowKey = s.lowKey;
        p.highKey = s.highKey;
        p.originalNote = s.originalNote;
        p.gain = s.volume;
        p.mono = s.voiceMode == VoiceMode::Mono;
        p.monoLegato = s.monoMode == MonoMode::Legato;
    }
    voiceBank.setSlots(playback);
}

void LSampler24AudioProcessor::setCurrentSlot(int oneBasedSlot)
{
    currentSlot = juce::jlimit(1, slotCount, oneBasedSlot);
}

LSampler24AudioProcessor::SlotState LSampler24AudioProcessor::getSlotState(int oneBasedSlot) const
{
    const juce::ScopedLock lock(stateLock);
    return slots[(size_t) (juce::jlimit(1, slotCount, oneBasedSlot) - 1)];
}

juce::String LSampler24AudioProcessor::getSlotLabel(int oneBasedSlot) const
{
    const auto slot = getSlotState(oneBasedSlot);
    return "Slot " + juce::String(oneBasedSlot) + ", "
         + (slot.sampleFile.getFullPathName().isEmpty() ? juce::String("empty") : slot.sampleFile.getFileName());
}

bool LSampler24AudioProcessor::loadSample(const juce::File& file, juce::String& error)
{
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded)
        return false;

    {
        const juce::ScopedLock lock(stateLock);
        auto& slot = slots[(size_t) (currentSlot - 1)];
        slot.sample = std::move(loaded);
        slot.sampleFile = file;
        slot.status = "Loaded: " + file.getFileName();
    }
    syncVoiceBank();
    return true;
}

int LSampler24AudioProcessor::getLowKey() const { return getSlotState(currentSlot).lowKey; }
int LSampler24AudioProcessor::getHighKey() const { return getSlotState(currentSlot).highKey; }
int LSampler24AudioProcessor::getOriginalNote() const { return getSlotState(currentSlot).originalNote; }
float LSampler24AudioProcessor::getVolume() const { return getSlotState(currentSlot).volume; }
LSampler24AudioProcessor::VoiceMode LSampler24AudioProcessor::getVoiceMode() const { return getSlotState(currentSlot).voiceMode; }
LSampler24AudioProcessor::MonoMode LSampler24AudioProcessor::getMonoMode() const { return getSlotState(currentSlot).monoMode; }
juce::String LSampler24AudioProcessor::getSampleStatus() const { return getSlotState(currentSlot).status; }

void LSampler24AudioProcessor::setLowKey(int note)
{
    { const juce::ScopedLock lock(stateLock); slots[(size_t)(currentSlot - 1)].lowKey = juce::jlimit(0, 127, note); }
    syncVoiceBank();
}
void LSampler24AudioProcessor::setHighKey(int note)
{
    { const juce::ScopedLock lock(stateLock); slots[(size_t)(currentSlot - 1)].highKey = juce::jlimit(0, 127, note); }
    syncVoiceBank();
}
void LSampler24AudioProcessor::setOriginalNote(int note)
{
    { const juce::ScopedLock lock(stateLock); slots[(size_t)(currentSlot - 1)].originalNote = juce::jlimit(0, 127, note); }
    syncVoiceBank();
}
void LSampler24AudioProcessor::setVolume(float value)
{
    { const juce::ScopedLock lock(stateLock); slots[(size_t)(currentSlot - 1)].volume = juce::jlimit(0.0f, 1.0f, value); }
    syncVoiceBank();
}
void LSampler24AudioProcessor::setVoiceMode(VoiceMode mode)
{
    { const juce::ScopedLock lock(stateLock); slots[(size_t)(currentSlot - 1)].voiceMode = mode; }
    syncVoiceBank();
}
void LSampler24AudioProcessor::setMonoMode(MonoMode mode)
{
    { const juce::ScopedLock lock(stateLock); slots[(size_t)(currentSlot - 1)].monoMode = mode; }
    syncVoiceBank();
}

juce::ValueTree LSampler24AudioProcessor::makeSlotStateTree(int slotIndex, const juce::String& type) const
{
    const juce::ScopedLock lock(stateLock);
    const auto& s = slots[(size_t) slotIndex];
    juce::ValueTree tree(type);
    tree.setProperty("format", "LSampler-24 Slot", nullptr);
    tree.setProperty("formatVersion", 4, nullptr);
    tree.setProperty("slot", slotIndex + 1, nullptr);
    tree.setProperty("sampleReference", library.makeSampleReference(s.sampleFile), nullptr);
    tree.setProperty("lowKey", s.lowKey, nullptr);
    tree.setProperty("highKey", s.highKey, nullptr);
    tree.setProperty("originalNote", s.originalNote, nullptr);
    tree.setProperty("volume", s.volume, nullptr);
    tree.setProperty("voiceMode", static_cast<int>(s.voiceMode), nullptr);
    tree.setProperty("monoMode", static_cast<int>(s.monoMode), nullptr);
    return tree;
}

bool LSampler24AudioProcessor::restoreSlotStateTree(int slotIndex, const juce::ValueTree& tree, juce::String& error)
{
    if (!tree.isValid() || slotIndex < 0 || slotIndex >= slotCount)
    {
        error = "Invalid slot preset";
        return false;
    }

    SlotState restored;
    const int defaultLow = 0;
    const int defaultHigh = 127;
    const int defaultRoot = 60;
    const bool hasSavedRoot = tree.hasProperty("originalNote") || tree.hasProperty("rootNote");
    const int savedRoot = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("originalNote", tree.getProperty("rootNote", defaultRoot))));
    // TEST1/TEST2 compatibility: an old state with a saved root but no range remains single-note.
    // TEST1/TEST2 states with a saved root but no range stay single-note for compatibility.
    // A genuinely new TEST4 slot defaults to the full 0..127 range.
    const int missingLowDefault = hasSavedRoot ? savedRoot : defaultLow;
    const int missingHighDefault = hasSavedRoot ? savedRoot : defaultHigh;
    restored.lowKey = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("lowKey", missingLowDefault)));
    restored.highKey = juce::jlimit(0, 127, static_cast<int>(tree.getProperty("highKey", missingHighDefault)));
    restored.originalNote = savedRoot;
    restored.volume = juce::jlimit(0.0f, 1.0f, static_cast<float>(tree.getProperty("volume", 1.0)));
    restored.voiceMode = static_cast<int>(tree.getProperty("voiceMode", 1)) == 0 ? VoiceMode::Mono : VoiceMode::Poly;
    restored.monoMode = static_cast<int>(tree.getProperty("monoMode", 0)) == 1 ? MonoMode::Legato : MonoMode::Trigger;

    auto reference = tree.getProperty("sampleReference").toString();
    if (reference.isEmpty()) reference = tree.getProperty("samplePath").toString();
    auto file = library.resolveSampleReference(reference);
    if (!file.getFullPathName().isEmpty())
    {
        restored.sampleFile = file;
        if (file.existsAsFile())
        {
            juce::String loadError;
            restored.sample = SamplePool::instance().load(file, loadError);
            if (!restored.sample)
            {
                restored.status = loadError;
                error = loadError;
            }
            else restored.status = "Loaded: " + file.getFileName();
        }
        else
        {
            restored.status = "Sample missing: " + file.getFileName();
            error = restored.status;
        }
    }

    { const juce::ScopedLock lock(stateLock); slots[(size_t) slotIndex] = std::move(restored); }
    return error.isEmpty();
}

bool LSampler24AudioProcessor::materialiseSlotSample(int slotIndex, juce::String& error)
{
    juce::File source;
    { const juce::ScopedLock lock(stateLock); source = slots[(size_t)slotIndex].sampleFile; }
    if (source.getFullPathName().isEmpty()) return true;
    auto local = library.materialiseSample(source, error);
    if (local.getFullPathName().isEmpty()) return false;
    if (local != source)
    {
        auto loaded = SamplePool::instance().load(local, error);
        if (!loaded) return false;
        const juce::ScopedLock lock(stateLock);
        auto& s = slots[(size_t)slotIndex];
        s.sampleFile = local;
        s.sample = std::move(loaded);
        s.status = "Loaded: " + local.getFileName();
    }
    return true;
}

juce::ValueTree LSampler24AudioProcessor::makeBankState(const juce::String& type) const
{
    juce::ValueTree bank(type);
    bank.setProperty("format", "LSampler-24 Bank", nullptr);
    bank.setProperty("formatVersion", 4, nullptr);
    bank.setProperty("slotCount", slotCount, nullptr);
    bank.setProperty("currentSlot", currentSlot, nullptr);
    for (int i = 0; i < slotCount; ++i)
        bank.addChild(makeSlotStateTree(i), -1, nullptr);
    return bank;
}

bool LSampler24AudioProcessor::restoreBankState(const juce::ValueTree& bank, juce::String& error)
{
    if (!bank.isValid()) { error = "Invalid bank"; return false; }
    juce::String firstError;
    for (int i = 0; i < slotCount; ++i)
    {
        auto child = bank.getChild(i);
        if (!child.isValid()) child = juce::ValueTree("Slot");
        juce::String slotError;
        restoreSlotStateTree(i, child, slotError);
        if (firstError.isEmpty() && slotError.isNotEmpty()) firstError = slotError;
    }
    currentSlot = juce::jlimit(1, slotCount, static_cast<int>(bank.getProperty("currentSlot", 1)));
    syncVoiceBank();
    error = firstError;
    return firstError.isEmpty();
}

bool LSampler24AudioProcessor::writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const
{
    auto xml = tree.createXml();
    if (!xml || !xml->writeTo(file)) { error = "Could not write preset"; return false; }
    return true;
}

juce::ValueTree LSampler24AudioProcessor::readPreset(const juce::File& file, juce::String& error) const
{
    auto xml = juce::XmlDocument::parse(file);
    if (!xml) { error = "Could not read preset"; return {}; }
    return juce::ValueTree::fromXml(*xml);
}

bool LSampler24AudioProcessor::saveSlotPreset(const juce::File& file, juce::String& error)
{
    const int index = currentSlot - 1;
    if (!materialiseSlotSample(index, error)) return false;
    syncVoiceBank();
    return writePreset(file, makeSlotStateTree(index, "LSampler24Slot"), error);
}

bool LSampler24AudioProcessor::loadSlotPreset(const juce::File& file, juce::String& error)
{
    auto tree = readPreset(file, error);
    if (!tree.hasType("LSampler24Slot") && !tree.hasType("Slot")) { error = "Not an LSampler-24 slot preset"; return false; }
    const bool ok = restoreSlotStateTree(currentSlot - 1, tree, error);
    syncVoiceBank();
    return ok;
}

bool LSampler24AudioProcessor::saveBankPreset(const juce::File& file, juce::String& error)
{
    for (int i = 0; i < slotCount; ++i)
        if (!materialiseSlotSample(i, error)) return false;
    syncVoiceBank();
    return writePreset(file, makeBankState("LSampler24Bank"), error);
}

bool LSampler24AudioProcessor::loadBankPreset(const juce::File& file, juce::String& error)
{
    auto tree = readPreset(file, error);
    if (!tree.hasType("LSampler24Bank")) { error = "Not an LSampler-24 bank preset"; return false; }
    return restoreBankState(tree, error);
}

void LSampler24AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = makeBankState("LSampler24State");
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void LSampler24AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        juce::String error;
        if (state.hasType("LSampler24State") || state.hasType("LSampler24Bank"))
            restoreBankState(state, error);
        else
        {
            // TEST1/TEST2 one-slot project-state compatibility.
            restoreSlotStateTree(0, state, error);
            syncVoiceBank();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LSampler24AudioProcessor(); }
