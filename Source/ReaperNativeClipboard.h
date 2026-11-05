#pragma once
// LSampler-24 TEST169 | REAPER clipboard / SWS media-source path reader (Windows VST3 only).
// The caller MUST first confirm IReaperHostApplication is present.
// No interception of REAPER Ctrl+C, no Lua. SWS/BR path-copy is optional
// (it is an existing REAPER action; LSampler links to no SWS binary).
// File-backed, unprocessed audio takes only; no GUI/audio-thread polling.
#include <juce_core/juce_core.h>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace lsampler_reaper_clipboard {
struct Item
{
    juce::File file;
    juce::String name;
    double startSeconds = 0.0, endSeconds = 0.0;
};
struct Result
{
    bool recognised = false;
    std::vector<Item> items;
    int skipped = 0;
    juce::String error;
};

inline juce::String argument(juce::String line)
{
    line = line.trim();
    const int space = line.indexOfChar(' ');
    if (space < 0) return {};
    auto value = line.substring(space + 1).trim();
    if (value.startsWithChar('"'))
    {
        juce::String decoded;
        bool escaped = false;
        // REAPER's RPP quoted strings can contain escaped quotes. Keep
        // ordinary backslashes untouched (Windows drive and UNC paths).
        for (int i = 1; i < value.length(); ++i)
        {
            const auto c = value[i];
            if (escaped) { decoded += juce::String::charToString(c); escaped = false; continue; }
            if (c == '\\' && i + 1 < value.length() && value[i + 1] == '"')
                { escaped = true; continue; }
            if (c == '"') break;
            decoded += juce::String::charToString(c);
        }
        return decoded;
    }
    return value;
}
inline double numeric(juce::String line, double defaultValue)
{
    const auto a = argument(std::move(line)).upToFirstOccurrenceOf(" ", false, false);
    if (a.isEmpty()) return defaultValue;
    auto v = a.getDoubleValue();
    return std::isfinite(v) ? v : defaultValue;
}
inline juce::String chunkType(const juce::String& trimmed)
{
    if (!trimmed.startsWithChar('<')) return {};
    return trimmed.substring(1).upToFirstOccurrenceOf(" ", false, false).trim();
}
inline bool hasTop(const std::vector<juce::String>& stack, const juce::String& name)
{
    return std::find(stack.begin(), stack.end(), name) != stack.end();
}

// REAPER exports RPP-like item blocks to the Windows clipboard. Parse only
// structurally valid <ITEM> entries; never read the currently selected items.
// Preserve REAPER's copied-item order even across different source tracks.
inline Result parseReaperItemText(const juce::String& text)
{
    Result result;
    if (!text.contains("<ITEM")) return result;
    std::vector<juce::String> stack;
    bool active = false;
    bool lengthKnown = false, hasFile = false;
    bool rateUnsupported = false, fxUnsupported = false, stretchUnsupported = false;
    bool midiUnsupported = false;
    size_t itemDepth = 0;
    int takeCount = 0;
    double offset = 0, length = 0;
    juce::String filename, name;

    auto finish = [&]()
    {
        if (!active) return;
        const double end = offset + length;
        if (hasFile && lengthKnown && !rateUnsupported && !fxUnsupported
            && !stretchUnsupported && !midiUnsupported && takeCount <= 1
            && std::isfinite(offset) && std::isfinite(end)
            && offset >= 0 && length > 0 && end > offset && end < 864000.0)
        {
            if (juce::File::isAbsolutePath(filename) && juce::File(filename).existsAsFile())
                result.items.push_back({ juce::File(filename), name, offset, end });
            else ++result.skipped;
        }
        else ++result.skipped;
        active = false;
    };
    const auto lines = juce::StringArray::fromLines(text);
    for (const auto& original : lines)
    {
        const auto line = original.trim();
        if (line.isEmpty()) continue;
        if (line.startsWithChar('<'))
        {
            const auto type = chunkType(line);
            if (!active && type == "ITEM")
            {
                finish();
                active = true;
                itemDepth = stack.size() + 1;
                result.recognised = true;
                lengthKnown = hasFile = rateUnsupported = fxUnsupported = false;
                stretchUnsupported = midiUnsupported = false;
                takeCount = 0; offset = length = 0; filename.clear(); name.clear();
            }
            else if (active)
            {
                if (type == "TAKE") ++takeCount;
                if (type == "TAKEFX" || type == "FXCHAIN" || type == "ITEMFX")
                    fxUnsupported = true;
                if (type == "SOURCE")
                {
                    const auto sourceType = argument(line);
                    if (sourceType == "MIDI" || sourceType == "SECTION" || sourceType == "RPP_PROJECT")
                        midiUnsupported = true;
                }
            }
            stack.push_back(type);
            if (stack.size() > 64) { stack.clear(); finish(); }
            continue;
        }
        if (line == ">")
        {
            if (!stack.empty())
            {
                const bool endItem = active && stack.size() == itemDepth && stack.back() == "ITEM";
                stack.pop_back();
                if (endItem) finish();
            }
            continue;
        }
        if (!active || stack.size() < itemDepth || !hasTop(stack, "ITEM")) continue;
        if (stack.size() == itemDepth)
        {
            if (line.startsWith("LENGTH "))
            {
                length = numeric(line, -1);
                lengthKnown = length > 0;
            }
            else if (line.startsWith("STRETCH") || line.startsWith("SM ")) stretchUnsupported = true;
        }
        if (line.startsWith("SOFFS ")) offset = numeric(line, -1);
        else if (line.startsWith("PLAYRATE "))
            rateUnsupported = std::abs(numeric(line, -1) - 1.0) > 0.000001;
        else if (line.startsWith("PITCH "))
            rateUnsupported = rateUnsupported || std::abs(numeric(line, -1)) > 0.000001;
        else if (line.startsWith("FILE ") && hasTop(stack, "SOURCE"))
        {
            filename = argument(line);
            hasFile = filename.isNotEmpty();
        }
        else if (line.startsWith("NAME ") && (hasTop(stack, "TAKE") || stack.size() == itemDepth))
            name = argument(line);
        else if (line.startsWith("STRETCHMARKER ") || line.startsWith("STRETCHMARKERS "))
            stretchUnsupported = true;
    }
    // Reject malformed/truncated chunks: never paste incomplete clipboard data.
    if (active) { result.recognised = true; ++result.skipped; }
    if (result.recognised && result.items.empty())
        result.error = "Copied REAPER items contain no supported audio takes";
    return result;
}

// SWS/BR _BR_TSOURCE_PATH_TO_CLIPBOARD writes one absolute source path
// per line to the standard text clipboard. Unlike native item Ctrl+C, this
// contains *file paths*, not RPP item chunks. File imports intentionally
// load the ENTIRE source: a trimmed item/FX/stretched take is not rendered.
inline Result parseMediaSourcePaths(const juce::String& clipboardText)
{
    Result result;
    // All-or-nothing recognition: never interpret general prose as filenames.
    // Recognise only an absolute path as the first nonempty text line.
    auto lines = juce::StringArray::fromLines(clipboardText);
    bool examinedFirst = false;
    for (auto line : lines)
    {
        line = line.trim();
        if (line.isEmpty()) continue;
        if (!examinedFirst && line.startsWithChar(0xfeff)) line = line.substring(1).trim();
        if (line.isEmpty()) continue;
        if (line.startsWithChar('"') && line.endsWithChar('"') && line.length() > 2)
            line = line.substring(1, line.length() - 1);
        if (!examinedFirst)
        {
            examinedFirst = true;
            // Avoid the RPP text path: a single arbitrary string containing
            // e.g. 'C:\' in the middle is not an exported media path.
            if (!juce::File::isAbsolutePath(line)) return result;
            result.recognised = true;
        }
        if (!juce::File::isAbsolutePath(line))
        {
            ++result.skipped;
            continue;
        }
        const juce::File file(line);
        if (file.existsAsFile())
            result.items.push_back({ file, file.getFileNameWithoutExtension(), 0.0, 0.0 });
        else
            ++result.skipped;
        // A huge pasted list must not allocate unlimited entries.
        if (result.items.size() >= 1024) { ++result.skipped; break; }
    }
    if (result.recognised && result.items.empty())
        result.error = "Source file paths copied from REAPER do not exist on disk";
    return result;
}

#if JUCE_WINDOWS
inline bool isReaperFormat(UINT fmt)
{
    char name[128] = {};
    if (fmt < 0xC000 || GetClipboardFormatNameA(fmt, name, 127) <= 0) return false;
    return juce::String(name).containsIgnoreCase("REAPER");
}
inline juce::String readText(UINT format)
{
    HANDLE h = GetClipboardData(format);
    if (!h) return {};
    const SIZE_T count = GlobalSize(h);
    if (count < 2 || count > 16 * 1024 * 1024) return {};
    const auto* bytes = static_cast<const char*>(GlobalLock(h));
    if (bytes == nullptr) return {};
    juce::String result;
    if (format == CF_UNICODETEXT || (count >= 10 && bytes[1] == 0 && bytes[3] == 0))
    {
        const auto* w = reinterpret_cast<const wchar_t*>(bytes);
        const size_t maxChars = count / sizeof(wchar_t);
        size_t n = 0;
        while (n < maxChars && w[n] != 0) ++n;
        result = juce::String(w, static_cast<int>(n));
    }
    else
    {
        size_t n = 0;
        while (n < count && bytes[n] != '\0') ++n;
        if (n > 0)
        {
            // CF_TEXT is the Windows ANSI code page, NOT necessarily UTF-8.
            // SWS may publish file paths with accented/non-ASCII characters.
            if (format == CF_TEXT)
            {
                const int wideLen = MultiByteToWideChar(CP_ACP, 0, bytes,
                                                        static_cast<int>(n), nullptr, 0);
                if (wideLen > 0)
                {
                    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
                    if (MultiByteToWideChar(CP_ACP, 0, bytes, static_cast<int>(n),
                                            wide.data(), wideLen) == wideLen)
                        result = juce::String(wide.c_str(), wideLen);
                }
            }
            else result = juce::String::fromUTF8(bytes, static_cast<int>(n));
        }
    }
    GlobalUnlock(h);
    return result;
}
inline Result readNativeReaperAudioItems()
{
    Result result;
    if (!OpenClipboard(nullptr)) return result;
    struct AutoClose { ~AutoClose() { CloseClipboard(); } } autoClose;

    // Try the reliable SWS/BR media source paths FIRST, even when a custom
    // REAPER clipboard format coexists. Keep an invalid-path result to
    // prevent accidental fallback to an older internally copied slot.
    if (IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        auto parsed = parseMediaSourcePaths(readText(CF_UNICODETEXT));
        if (parsed.recognised) return parsed;
    }
    if (IsClipboardFormatAvailable(CF_TEXT))
    {
        auto parsed = parseMediaSourcePaths(readText(CF_TEXT));
        if (parsed.recognised) return parsed;
    }

    // Backward-compatible experimental native REAPER Ctrl+C RPP parser.
    std::vector<UINT> formats;
    bool sawReaperFormat = false;
    for (UINT f = 0; (f = EnumClipboardFormats(f)) != 0 && formats.size() < 256; )
        if (isReaperFormat(f)) { formats.push_back(f); sawReaperFormat = true; }
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) formats.push_back(CF_UNICODETEXT);
    if (IsClipboardFormatAvailable(CF_TEXT)) formats.push_back(CF_TEXT);
    for (const auto f : formats)
    {
        auto parsed = parseReaperItemText(readText(f));
        if (parsed.recognised) return parsed;
    }
    if (sawReaperFormat)
    {
        result.recognised = true;
        result.error = "REAPER item clipboard format not recognised. Use SWS/BR: Copy take media source file path";
    }
    return result;
}
#else
inline Result readNativeReaperAudioItems() { return {}; }
#endif
} // namespace lsampler_reaper_clipboard
