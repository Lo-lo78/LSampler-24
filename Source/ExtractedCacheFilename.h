#pragma once
// TEST183: conservative identification of files created by TEST182 extraction.
// Independent of JUCE so this exact matcher has a native regression test.
#include <regex>
#include <string>

namespace lsampler_extracted_cache
{
inline bool isGeneratedFileName(const std::string& name)
{
    // Exact 24 hex source digest, channel pair and WAV extension. A cancelled
    // extraction can leave a partial file; those are ours as well.
    static const std::regex pattern(
        R"(^.+_[0-9a-f]{24}_ch[0-9]{2}-[0-9]{2}\.wav(?:\.partial-[0-9a-f]+)?$)",
        std::regex::icase);
    return std::regex_match(name, pattern);
}
}
