#include "LibraryManager.h"
#include <juce_cryptography/juce_cryptography.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
// TEST104: One disk read per unchanged file, rather than re-hashing every
// Sample Set entry on every host state snapshot. REAPER may ask for a VST3
// state synchronously on its message thread, including near editor closure.
// The full SHA-256 remains in presets for relocation/integrity checking.
struct CachedSampleHash
{
    juce::String hash;
    juce::int64 size = -1;
    juce::int64 modificationTime = 0;
};

juce::String sampleHashCached(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};

    // The on-disk file is the identity; its modification time and size are
    // checked before using a cached SHA. The cache is shared across instances.
    const auto key = file.getFullPathName().toStdString();
    const auto size = file.getSize();
    const auto modified = file.getLastModificationTime().toMilliseconds();

    static std::mutex cacheMutex;
    static std::unordered_map<std::string, CachedSampleHash> hashes;
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        const auto found = hashes.find(key);
        if (found != hashes.end() && found->second.size == size
            && found->second.modificationTime == modified)
            return found->second.hash;
    }

    // Read outside the mutex: never hold the cache lock during disk I/O.
    const auto digest = juce::SHA256(file).toHexString();
    if (file.existsAsFile() && file.getSize() == size
        && file.getLastModificationTime().toMilliseconds() == modified)
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        // Bound memory use when browsing many unrelated sample libraries.
        if (hashes.size() >= 4096 && hashes.find(key) == hashes.end())
            hashes.clear();
        hashes[key] = {digest, size, modified};
    }
    return digest;
}
} // namespace

LibraryManager::LibraryManager(const juce::File& rootOverride)
{
    rootDir = rootOverride.getFullPathName().isNotEmpty() ? rootOverride
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LSampler-24");
    samplesDir = rootDir.getChildFile("Library").getChildFile("Samples");
    slotsDir   = rootDir.getChildFile("Library").getChildFile("Slots");
    banksDir   = rootDir.getChildFile("Library").getChildFile("Banks");

    samplesDir.createDirectory();
    slotsDir.createDirectory();
    banksDir.createDirectory();
}

juce::File LibraryManager::uniqueDestination(const juce::File& folder, const juce::String& fileName)
{
    auto candidate = folder.getChildFile(fileName);
    if (!candidate.exists())
        return candidate;

    const auto base = candidate.getFileNameWithoutExtension();
    const auto ext = candidate.getFileExtension();
    for (int n = 2; n < 100000; ++n)
    {
        auto alt = folder.getChildFile(base + "_" + juce::String(n) + ext);
        if (!alt.exists())
            return alt;
    }
    return folder.getNonexistentChildFile(base, ext, false);
}

juce::File LibraryManager::materialiseSample(const juce::File& source, juce::String& error) const
{
    error.clear();
    if (!source.existsAsFile())
    {
        error = "Source sample not found";
        return {};
    }

    if (source.isAChildOf(samplesDir))
        return source;

    // The audio file is shared raw material. Reuse an existing Library file
    // when its contents are identical, even if another slot/bank saved it first.
    const auto sourceSize = source.getSize();
    const auto sourceHash = sampleHashCached(source);
    juce::Array<juce::File> existing;
    samplesDir.findChildFiles(existing, juce::File::findFiles, true);
    for (const auto& candidate : existing)
    {
        if (candidate.getSize() == sourceSize && sampleHashCached(candidate) == sourceHash)
            return candidate;
    }

    auto dest = uniqueDestination(samplesDir, source.getFileName());
    if (!source.copyFileTo(dest))
    {
        error = "Could not copy sample into the LSampler-24 Library";
        return {};
    }
    return dest;
}

juce::File LibraryManager::materialiseSampleAtRelativePath(const juce::File& source,
                                                            const juce::String& relativePath,
                                                            juce::String& error) const
{
    error.clear();
    if (!source.existsAsFile())
    {
        error = "Source sample not found";
        return {};
    }

    auto normalised = relativePath.replaceCharacter('\\', '/').trimCharactersAtStart("/");
    if (normalised.isEmpty())
        normalised = source.getFileName();

    auto desired = samplesDir.getChildFile(normalised.replaceCharacter('/', juce::File::getSeparatorChar()));
    desired.getParentDirectory().createDirectory();

    const auto sourceSize = source.getSize();
    const auto sourceHash = sampleHashCached(source);

    if (desired.existsAsFile())
    {
        if (desired.getSize() == sourceSize && sampleHashCached(desired) == sourceHash)
            return desired;

        desired = uniqueDestination(desired.getParentDirectory(), desired.getFileName());
    }

    if (!source.copyFileTo(desired))
    {
        error = "Could not copy sample into the LSampler-24 Library";
        return {};
    }
    return desired;
}

juce::File LibraryManager::materialiseEditedSample(const juce::AudioBuffer<float>& audio,
                                                        double sampleRate,
                                                        const juce::String& suggestedBaseName,
                                                        juce::String& error) const
{
    error.clear();
    if (audio.getNumChannels() <= 0 || audio.getNumSamples() <= 0)
    {
        error = "Edited sample is empty";
        return {};
    }

    auto base = suggestedBaseName.trim();
    if (base.isEmpty()) base = "Edited_Sample";
    base = juce::File::createLegalFileName(base);
    auto dest = uniqueDestination(samplesDir, base + "_trim.wav");

    juce::WavAudioFormat format;
    auto stream = dest.createOutputStream();
    if (!stream)
    {
        error = "Could not create edited sample in the LSampler-24 Library";
        return {};
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), juce::jmax(1.0, sampleRate),
                               static_cast<unsigned int>(audio.getNumChannels()), 24, {}, 0));
    if (!writer)
    {
        error = "Could not create WAV writer for edited sample";
        return {};
    }

    stream.release(); // ownership transferred to AudioFormatWriter
    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
    {
        writer.reset();
        dest.deleteFile();
        error = "Could not write edited sample audio";
        return {};
    }
    writer.reset();
    return dest;
}

juce::String LibraryManager::makeSampleReference(const juce::File& sampleFile) const
{
    if (sampleFile.getFullPathName().isEmpty())
        return {};

    if (sampleFile.isAChildOf(rootDir))
        return sampleFile.getRelativePathFrom(rootDir).replaceCharacter('\\', '/');

    // Unsaved/raw sample: project state may still point to the original absolute file.
    return sampleFile.getFullPathName();
}

juce::String LibraryManager::makeSampleHash(const juce::File& sampleFile) const
{
    if (!sampleFile.existsAsFile())
        return {};
    return sampleHashCached(sampleFile);
}

juce::File LibraryManager::resolveSampleReference(const juce::String& reference, const juce::String& expectedHash) const
{
    juce::File direct;
    if (reference.isNotEmpty())
    {
        direct = juce::File::isAbsolutePath(reference)
            ? juce::File(reference)
            : rootDir.getChildFile(reference.replaceCharacter('/', juce::File::getSeparatorChar()));

        if (direct.existsAsFile())
        {
            if (expectedHash.isEmpty() || sampleHashCached(direct).equalsIgnoreCase(expectedHash))
                return direct;
        }
    }

    // The path is only a fast hint. The SHA-256 is the stable identity of a
    // Library sample, so manual reorganisation anywhere below Library/Samples
    // does not break Slot recipes. This slower scan only happens after the
    // saved path no longer resolves (or resolves to different contents).
    if (expectedHash.isNotEmpty())
    {
        juce::Array<juce::File> files;
        samplesDir.findChildFiles(files, juce::File::findFiles, true);
        for (const auto& candidate : files)
            if (sampleHashCached(candidate).equalsIgnoreCase(expectedHash))
                return candidate;
    }

    return direct;
}

juce::String LibraryManager::defaultName(const juce::String& prefix)
{
    return prefix + "_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
}
