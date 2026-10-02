#include "PluginProcessor.h"
#include "PluginEditor.h"

LSampler24AudioProcessor::LSampler24AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void LSampler24AudioProcessor::prepareToPlay(double sampleRate, int)
{
    voiceBank.prepare(sampleRate);
    voiceBank.setRootNote(rootNote);
    voiceBank.setGain(volume);
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
    voiceBank.render(buffer, midi);
}

juce::AudioProcessorEditor* LSampler24AudioProcessor::createEditor()
{
    return new LSampler24AudioProcessorEditor(*this);
}

bool LSampler24AudioProcessor::loadSample(const juce::File& file, juce::String& error)
{
    auto loaded = SamplePool::instance().load(file, error);
    if (!loaded)
    {
        const juce::ScopedLock lock(stateLock);
        status = error;
        return false;
    }

    {
        const juce::ScopedLock lock(stateLock);
        currentSample = std::move(loaded);
        currentSampleFile = file;
        status = "Loaded: " + file.getFileName();
        voiceBank.setSample(currentSample);
    }
    return true;
}

void LSampler24AudioProcessor::setRootNote(int note)
{
    rootNote = juce::jlimit(0, 127, note);
    voiceBank.setRootNote(rootNote);
}

void LSampler24AudioProcessor::setVolume(float newVolume)
{
    volume = juce::jlimit(0.0f, 1.0f, newVolume);
    voiceBank.setGain(volume);
}

juce::File LSampler24AudioProcessor::getCurrentSampleFile() const
{
    const juce::ScopedLock lock(stateLock);
    return currentSampleFile;
}

juce::String LSampler24AudioProcessor::getSampleStatus() const
{
    const juce::ScopedLock lock(stateLock);
    return status;
}

juce::ValueTree LSampler24AudioProcessor::makeSlotState(const juce::String& type) const
{
    juce::ValueTree tree(type);
    tree.setProperty("format", "LSampler-24 Slot", nullptr);
    tree.setProperty("formatVersion", 1, nullptr);
    tree.setProperty("sampleReference", library.makeSampleReference(currentSampleFile), nullptr);
    tree.setProperty("rootNote", rootNote, nullptr);
    tree.setProperty("volume", volume, nullptr);
    return tree;
}

bool LSampler24AudioProcessor::restoreSlotState(const juce::ValueTree& tree, juce::String& error)
{
    if (!tree.isValid())
    {
        error = "Invalid preset";
        return false;
    }

    setRootNote(static_cast<int>(tree.getProperty("rootNote", 60)));
    setVolume(static_cast<float>(tree.getProperty("volume", 1.0)));

    auto reference = tree.getProperty("sampleReference").toString();
    // TEST1 compatibility only; new TEST2 presets always write sampleReference.
    if (reference.isEmpty())
        reference = tree.getProperty("samplePath").toString();
    const juce::File file = library.resolveSampleReference(reference);
    if (file.getFullPathName().isEmpty())
    {
        currentSample.reset();
        currentSampleFile = {};
        voiceBank.setSample({});
        status = "No sample loaded";
        return true;
    }

    if (!file.existsAsFile())
    {
        const juce::ScopedLock lock(stateLock);
        currentSample.reset();
        currentSampleFile = file;
        voiceBank.setSample({});
        status = "Sample missing: " + file.getFileName();
        error = status;
        return false;
    }
    return loadSample(file, error);
}

bool LSampler24AudioProcessor::materialiseCurrentSample(juce::String& error)
{
    if (currentSampleFile.getFullPathName().isEmpty())
        return true;

    auto local = library.materialiseSample(currentSampleFile, error);
    if (local.getFullPathName().isEmpty())
        return false;

    if (local != currentSampleFile)
        return loadSample(local, error);
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
    if (!materialiseCurrentSample(error))
        return false;
    return writePreset(presetFile, makeSlotState("LSampler24Slot"), error);
}

bool LSampler24AudioProcessor::loadSlotPreset(const juce::File& presetFile, juce::String& error)
{
    auto tree = readPreset(presetFile, error);
    if (!tree.hasType("LSampler24Slot"))
    {
        error = "Not an LSampler-24 slot preset";
        return false;
    }
    return restoreSlotState(tree, error);
}

bool LSampler24AudioProcessor::saveBankPreset(const juce::File& presetFile, juce::String& error)
{
    if (!materialiseCurrentSample(error))
        return false;

    juce::ValueTree bank("LSampler24Bank");
    bank.setProperty("format", "LSampler-24 Bank", nullptr);
    bank.setProperty("formatVersion", 1, nullptr);
    bank.setProperty("slotCount", 24, nullptr);

    // TEST2 still has one playable slot, but the bank file is already a real
    // 24-slot snapshot. Slots 2..24 are explicit empty placeholders so the
    // on-disk contract will not change when the engine grows to 24 slots.
    for (int index = 0; index < 24; ++index)
    {
        juce::ValueTree slot = (index == 0) ? makeSlotState("Slot") : juce::ValueTree("Slot");
        slot.setProperty("index", index, nullptr);
        slot.setProperty("formatVersion", 1, nullptr);
        if (index != 0)
        {
            slot.setProperty("sampleReference", "", nullptr);
            slot.setProperty("rootNote", 60, nullptr);
            slot.setProperty("volume", 1.0f, nullptr);
        }
        bank.addChild(slot, -1, nullptr);
    }

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
    return restoreSlotState(bank.getChild(0), error);
}

void LSampler24AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = makeSlotState("LSampler24State");
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void LSampler24AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        juce::String error;
        restoreSlotState(state, error);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LSampler24AudioProcessor();
}
