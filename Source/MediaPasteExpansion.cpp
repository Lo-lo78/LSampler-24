#include "MediaPasteExpansion.h"
#include "SamplePool.h"
#include "MediaTrackGrouping.h"
#include "ExtractedCacheFilename.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
 #include <propvarutil.h>
 #include <wrl/client.h>
#endif

namespace lsampler_media_paste
{
namespace
{
constexpr int maxSourceChannels = 64;
constexpr int ioFrames = 8192;
// A shared lock permits simultaneous imports from different plug-in instances;
// explicit cache clearing waits for all in-progress extractions to finish.
std::shared_mutex extractionCacheAccess;


juce::File extractionDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Lo-lo78").getChildFile("LSampler-24")
        .getChildFile("ExtractedAudio");
}

juce::String sourceIdentity(const juce::File& source)
{
    auto identity = source.getFullPathName() + "|" + juce::String(source.getSize())
        + "|" + juce::String(source.getLastModificationTime().toMilliseconds()) + "|TEST182-v1";
    const juce::MD5 digest(identity.toRawUTF8(), static_cast<size_t>(identity.getNumBytesAsUTF8()));
    return digest.toHexString().substring(0, 24);
}

juce::File channelFile(const juce::File& source, const juce::String& identity, int pair, int channels)
{
    // Never use a file name from the clipboard as a directory component.
    auto basename = juce::File::createLegalFileName(source.getFileNameWithoutExtension()).substring(0, 48);
    if (basename.isEmpty()) basename = "audio";
    const int first = pair * 2 + 1;
    return extractionDirectory().getChildFile(basename + "_" + identity + "_ch"
        + juce::String(first).paddedLeft('0', 2) + "-"
        + juce::String(std::min(first + 1, channels)).paddedLeft('0', 2) + ".wav");
}

juce::String channelSuffix(int pair, int count)
{
    const int first = 2 * pair + 1;
    return " [channels " + juce::String(first)
        + (first < count ? "-" + juce::String(first + 1) : juce::String{}) + "]";
}

struct ChannelWriter
{
    juce::File temporary;
    juce::File target;
    std::unique_ptr<juce::AudioFormatWriter> writer;
};

// Commit only fully extracted, closed WAVs. A failed or cancelled extraction
// removes its partial files; pre-existing project-referenced WAVs are untouched.
class WavOutputGroup
{
public:
    ~WavOutputGroup()
    {
        for (auto& entry : files)
        {
            entry.writer.reset(); // Close the handle before deleting on Windows.
            if (entry.temporary.getFullPathName().isNotEmpty()) entry.temporary.deleteFile();
        }
    }

    bool initialise(const juce::File& source, int channels, int tracks,
                    double rate, juce::String& error)
    {
        if (tracks < 1 || rate <= 0 || channels < 1) { error = "Invalid audio format"; return false; }
        if (extractionDirectory().createDirectory().failed())
        {
            error = "Cannot create LSampler-24 extracted audio cache";
            return false;
        }
        const auto id = sourceIdentity(source);
        juce::WavAudioFormat format;
        for (int pair = 0; pair < tracks; ++pair)
        {
            ChannelWriter file;
            file.target = channelFile(source, id, pair, channels);
            if (file.target.existsAsFile() && file.target.getSize() > 44)
            {
                files.push_back(std::move(file));
                continue;
            }
            file.temporary = file.target.getSiblingFile(file.target.getFileName()
                + ".partial-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()));
            auto stream = file.temporary.createOutputStream();
            if (!stream || !stream->openedOk())
            {
                error = "Cannot write extracted audio WAV";
                return false;
            }
            const auto channelPair = lsampler_media_grouping::pairAt(channels, pair);
            const unsigned int outputChannels = static_cast<unsigned int>(channelPair.channelCount);
            auto* raw = format.createWriterFor(stream.get(), rate, outputChannels, 24, {}, 0);
            if (raw == nullptr)
            {
                error = "Cannot create extracted WAV writer";
                return false;
            }
            stream.release(); // writer owns the OutputStream
            file.writer.reset(raw);
            files.push_back(std::move(file));
        }
        return true;
    }

    bool allCached() const
    {
        return std::all_of(files.begin(), files.end(), [](const auto& f) { return f.writer == nullptr; });
    }

    bool write(const juce::AudioBuffer<float>& buffer, int frames, juce::String& error)
    {
        for (size_t pair = 0; pair < files.size(); ++pair)
        {
            auto& output = files[pair];
            if (output.writer == nullptr) continue;
            const int first = static_cast<int>(pair) * 2;
            const int channels = lsampler_media_grouping::pairAt(buffer.getNumChannels(), static_cast<int>(pair)).channelCount;
            juce::AudioBuffer<float> stereo(channels, frames);
            for (int ch = 0; ch < channels; ++ch)
                stereo.copyFrom(ch, 0, buffer, first + ch, 0, frames);
            if (!output.writer->writeFromAudioSampleBuffer(stereo, 0, frames))
            {
                error = "Cannot write extracted audio samples (disk space?)";
                return false;
            }
        }
        return true;
    }

    bool commit(juce::String& error)
    {
        for (auto& output : files)
        {
            if (output.writer == nullptr) continue;
            output.writer.reset(); // Complete the RIFF header before renaming.
            if (!output.temporary.moveFileTo(output.target))
            {
                // Another concurrent paste may have finished this very same source.
                if (!output.target.existsAsFile() || output.target.getSize() <= 44)
                {
                    error = "Cannot finalise extracted audio WAV";
                    return false;
                }
                output.temporary.deleteFile();
            }
            output.temporary = {};
        }
        return true;
    }

    std::vector<Track> result(const juce::File& source, int channels) const
    {
        std::vector<Track> result;
        for (size_t i = 0; i < files.size(); ++i)
            result.push_back({files[i].target, channelSuffix(static_cast<int>(i), channels)});
        return result;
    }

private:
    std::vector<ChannelWriter> files;
};

std::vector<Track> splitNativeAudio(const juce::File& source, juce::AudioFormatReader& reader,
                                    int limit, const StopRequested& stop,
                                    const Progress& progress,
                                    juce::String& error, int& discarded)
{
    const int channels = static_cast<int>(reader.numChannels);
    if (channels <= 2) return {{source, {}}}; // Preserve exact originals / no cache.
    if (channels > maxSourceChannels || reader.lengthInSamples <= 0)
    { error = "Invalid or excessive multichannel audio format"; return {}; }
    const int allPairs = lsampler_media_grouping::trackCount(channels);
    const int pairs = lsampler_media_grouping::limitedTracks(channels, limit);
    discarded += allPairs - pairs;
    if (pairs <= 0) return {};
    WavOutputGroup group;
    if (!group.initialise(source, channels, pairs, reader.sampleRate, error)) return {};
    if (!group.allCached())
    {
        juce::AudioBuffer<float> chunk(channels, ioFrames);
        for (juce::int64 position = 0; position < reader.lengthInSamples;)
        {
            if (stop && stop()) { error = "Import stopped"; return {}; }
            const int frames = static_cast<int>(std::min<juce::int64>(ioFrames, reader.lengthInSamples - position));
            if (!reader.read(&chunk, 0, frames, position, true, true))
            { error = "Cannot decode all source audio channels"; return {}; }
            if (!group.write(chunk, frames, error)) return {};
            position += frames;
            if (progress) progress(double(position) / double(reader.lengthInSamples));
        }
        if (!group.commit(error)) return {};
    }
    if (progress) progress(1.0);
    return group.result(source, channels);
}

#if JUCE_WINDOWS
using Microsoft::WRL::ComPtr;

class MediaFoundationScope
{
public:
    MediaFoundationScope()
    {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        shouldUninitCom = SUCCEEDED(com);
        if (SUCCEEDED(com) || com == RPC_E_CHANGED_MODE)
            started = SUCCEEDED(MFStartup(MF_VERSION));
    }
    ~MediaFoundationScope()
    {
        if (started) MFShutdown();
        if (shouldUninitCom) CoUninitialize();
    }
    explicit operator bool() const noexcept { return started; }
private:
    bool started = false, shouldUninitCom = false;
};

float toFloat(const BYTE* data, UINT32 bits, bool isFloat)
{
    if (isFloat)
    {
        float value = 0.0f;
        std::memcpy(&value, data, sizeof(value));
        return std::isfinite(value) ? value : 0.0f;
    }
    if (bits == 8) return (float(data[0]) - 128.0f) / 128.0f;
    if (bits == 16)
    {
        int16_t value = 0;
        std::memcpy(&value, data, 2);
        return float(value) / 32768.0f;
    }
    if (bits == 24)
    {
        int32_t value = static_cast<int32_t>(data[0]) | (static_cast<int32_t>(data[1]) << 8)
            | (static_cast<int32_t>(data[2]) << 16);
        if ((value & 0x800000) != 0) value |= ~0xffffff;
        return float(value) / 8388608.0f;
    }
    int32_t value = 0;
    std::memcpy(&value, data, 4);
    return float(double(value) / 2147483648.0);
}

std::vector<Track> splitMediaFoundation(const juce::File& source, int limit,
                                        const StopRequested& stop, const Progress& progress,
                                        juce::String& error, int& discarded)
{
    MediaFoundationScope mf;
    if (!mf) { error = "Windows Media Foundation unavailable"; return {}; }
    ComPtr<IMFSourceReader> reader;
    const HRESULT open = MFCreateSourceReaderFromURL(source.getFullPathName().toWideCharPointer(),
                                                     nullptr, reader.GetAddressOf());
    if (FAILED(open)) { error = "Windows cannot decode this audio/video format"; return {}; }
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE); // Skip video frames.
    if (FAILED(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE)))
    { error = "No decodable audio stream in video"; return {}; }

    // Preserve the source's channel count. Some codecs silently downmix
    // surround channels when the requested format leaves this unspecified.
    UINT32 nativeChannels = 0, nativeRate = 0;
    ComPtr<IMFMediaType> nativeType;
    if (SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                                             nativeType.GetAddressOf())))
    {
        nativeType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &nativeChannels);
        nativeType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &nativeRate);
    }
    // Prefer IEEE float decoding for clean 24-bit WAV output. Some Windows
    // codecs only expose signed integer PCM; support both transparently.
    HRESULT set = E_FAIL;
    // Try floating-point output first; some MFTs accept only a partial type,
    // others require the native channel count and rate to be supplied.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const bool wantFloat = attempt < 2;
        const bool explicitLayout = (attempt % 2) == 0;
        ComPtr<IMFMediaType> requested;
        if (FAILED(MFCreateMediaType(requested.GetAddressOf()))) break;
        requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        requested->SetGUID(MF_MT_SUBTYPE, wantFloat ? MFAudioFormat_Float : MFAudioFormat_PCM);
        if (explicitLayout)
        {
            if (nativeChannels >= 1 && nativeChannels <= maxSourceChannels)
                requested->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, nativeChannels);
            if (nativeRate >= 1000 && nativeRate <= 384000)
                requested->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, nativeRate);
        }
        set = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, requested.Get());
        if (SUCCEEDED(set)) break;
    }
    if (FAILED(set)) { error = "Windows cannot convert video audio to PCM"; return {}; }
    ComPtr<IMFMediaType> outputType;
    if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, outputType.GetAddressOf())))
    { error = "Cannot inspect decoded audio format"; return {}; }
    UINT32 channels = 0, rate = 0, bits = 0, alignment = 0;
    GUID subtype {};
    outputType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    outputType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    outputType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    outputType->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &alignment);
    outputType->GetGUID(MF_MT_SUBTYPE, &subtype);
    const bool floating = subtype == MFAudioFormat_Float;
    if (nativeChannels > 0 && channels < nativeChannels)
    { error = "Windows decoder would downmix and lose source channels"; return {}; }
    if (channels < 1 || channels > maxSourceChannels || rate < 1000 || rate > 384000
        || (floating && bits != 32) || (!floating && bits != 8 && bits != 16 && bits != 24 && bits != 32)
        || alignment != channels * (bits / 8))
    { error = "Unsupported decoded channel layout or bit depth"; return {}; }
    const int allPairs = lsampler_media_grouping::trackCount(static_cast<int>(channels));
    const int pairs = lsampler_media_grouping::limitedTracks(static_cast<int>(channels), limit);
    discarded += allPairs - pairs;
    if (pairs <= 0) return {};
    WavOutputGroup group;
    if (!group.initialise(source, static_cast<int>(channels), pairs, rate, error)) return {};
    if (!group.allCached())
    {
        PROPVARIANT duration; PropVariantInit(&duration);
        const bool hasDuration = SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                                           MF_PD_DURATION, &duration))
            && duration.vt == VT_UI8 && duration.uhVal.QuadPart > 0;
        const double length100ns = hasDuration ? double(duration.uhVal.QuadPart) : 0.0;
        PropVariantClear(&duration);
        juce::AudioBuffer<float> pcm(static_cast<int>(channels), ioFrames);
        juce::int64 emittedFrames = 0;
        for (;;)
        {
            if (stop && stop()) { error = "Import stopped"; return {}; }
            DWORD actual = 0, flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            const HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                &actual, &flags, &timestamp, sample.GetAddressOf());
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR))
            { error = "Video audio decoding failed"; return {}; }
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0)
            { error = "Video changes audio channel format mid-file"; return {}; }
            if (sample != nullptr)
            {
                ComPtr<IMFMediaBuffer> buffer;
                if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf())))
                { error = "Cannot retrieve decoded video audio"; return {}; }
                BYTE* bytes = nullptr;
                DWORD maxBytes = 0, usedBytes = 0;
                if (FAILED(buffer->Lock(&bytes, &maxBytes, &usedBytes)))
                { error = "Cannot read decoded audio buffer"; return {}; }
                bool wrote = true;
                if (usedBytes % alignment != 0)
                { error = "Invalid decoded audio block size"; wrote = false; }
                else
                {
                    const UINT32 decodedFrames = usedBytes / alignment;
                    const int bps = static_cast<int>(bits / 8);
                    for (UINT32 offset = 0; offset < decodedFrames && wrote;)
                    {
                        const int frames = static_cast<int>(std::min<UINT32>(ioFrames, decodedFrames - offset));
                        for (int ch = 0; ch < static_cast<int>(channels); ++ch)
                        {
                            float* dest = pcm.getWritePointer(ch);
                            for (int frame = 0; frame < frames; ++frame)
                            {
                                const auto byteOffset = (size_t(offset) + size_t(frame)) * size_t(alignment)
                                    + size_t(ch) * size_t(bps);
                                dest[frame] = toFloat(bytes + byteOffset, bits, floating);
                            }
                        }
                        wrote = group.write(pcm, frames, error);
                        offset += static_cast<UINT32>(frames);
                        emittedFrames += frames;
                    }
                }
                buffer->Unlock();
                if (!wrote) return {};
            }
            if (progress && hasDuration)
                progress(juce::jlimit(0.0, 0.99, (double(timestamp) + 10000000.0
                    * double(emittedFrames % static_cast<juce::int64>(rate)) / double(rate)) / length100ns));
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
        if (emittedFrames <= 0) { error = "Video contains no audio samples"; return {}; }
        if (!group.commit(error)) return {};
    }
    if (progress) progress(1.0);
    return group.result(source, static_cast<int>(channels));
}
#endif
}

juce::File extractionCacheDirectory()
{
    return extractionDirectory();
}

CacheCleanupResult clearExtractedAudioCache(const std::vector<juce::File>& protectedFiles,
                                            const StopRequested& shouldStop)
{
    CacheCleanupResult result;
    // This operation runs exclusively on the existing file worker, not the UI
    // or audio thread. No extraction can create/rename a WAV while we delete.
    const std::unique_lock<std::shared_mutex> cacheLock(extractionCacheAccess);
    const auto directory = extractionDirectory();
    if (!directory.isDirectory()) return result;

    // Match ONLY output owned by this feature. Unknown user files in the
    // directory are never touched, even if their extension is WAV.
    juce::Array<juce::File> children;
    directory.findChildFiles(children, juce::File::findFiles, false);
    for (const auto& file : children)
    {
        if (shouldStop && shouldStop()) break;
        if (file.getParentDirectory() != directory
            || !lsampler_extracted_cache::isGeneratedFileName(file.getFileName().toStdString()))
            continue;
        if (std::find(protectedFiles.begin(), protectedFiles.end(), file) != protectedFiles.end())
        {
            ++result.protectedFiles;
            continue;
        }
        const auto bytes = file.getSize();
        if (file.deleteFile())
        {
            ++result.deleted;
            result.bytesFreed += static_cast<std::int64_t>(bytes);
        }
        else ++result.failed;
    }
    return result;
}

bool isCandidate(const juce::File& file)
{
    if (SamplePool::instance().hasSupportedExtension(file)) return true;
#if JUCE_WINDOWS
    return file.hasFileExtension("aac;adt;adts;m4a;mp4;m4v;mov;avi;wmv;asf;mkv;webm;mpg;mpeg;ts;m2ts;3gp;3g2;wma;ac3;mp2;flv");
#else
    return false;
#endif
}

std::vector<Track> expand(const juce::File& source, int maxTracks,
                          const StopRequested& stop, const Progress& progress,
                          juce::String& error, int& discardedTracks)
{
    discardedTracks = 0;
    error.clear();
    if (!source.existsAsFile() || !isCandidate(source))
    { error = "File not found or unsupported media extension"; return {}; }
    if (maxTracks <= 0) { discardedTracks = 1; return {}; }
    // Protect derived WAVs from an explicit cache cleanup until this entire
    // extraction finishes. Shared lock does not serialise independent imports.
    const std::shared_lock<std::shared_mutex> cacheLock(extractionCacheAccess);
    // Prefer JUCE's unmodified decoder for standard audio files; only split
    // when its format header explicitly reports 3+ channels.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source)))
        return splitNativeAudio(source, *reader, maxTracks, stop, progress, error, discardedTracks);
#if JUCE_WINDOWS
    return splitMediaFoundation(source, maxTracks, stop, progress, error, discardedTracks);
#else
    error = "This audio/video codec is not supported on this platform";
    return {};
#endif
}
}
