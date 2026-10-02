#include "LibraryManager.h"
#include <juce_cryptography/juce_cryptography.h>

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
    const juce::SHA256 sourceHash(source);
    juce::Array<juce::File> existing;
    samplesDir.findChildFiles(existing, juce::File::findFiles, true);
    for (const auto& candidate : existing)
    {
        if (candidate.getSize() == sourceSize && juce::SHA256(candidate).toHexString() == sourceHash.toHexString())
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
    const juce::SHA256 sourceHash(source);

    if (desired.existsAsFile())
    {
        if (desired.getSize() == sourceSize && juce::SHA256(desired).toHexString() == sourceHash.toHexString())
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
    return juce::SHA256(sampleFile).toHexString();
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
            if (expectedHash.isEmpty() || juce::SHA256(direct).toHexString().equalsIgnoreCase(expectedHash))
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
            if (juce::SHA256(candidate).toHexString().equalsIgnoreCase(expectedHash))
                return candidate;
    }

    return direct;
}

juce::String LibraryManager::defaultName(const juce::String& prefix)
{
    return prefix + "_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
}
