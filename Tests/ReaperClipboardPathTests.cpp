// TEST169: exercise the actual C++ SWS/BR clipboard path parser on Windows.
// Does not require REAPER or change the OS clipboard.
#include "../Source/ReaperNativeClipboard.h"
#include <iostream>

static void expect(bool condition, const char* test)
{
    if (!condition)
    {
        std::cerr << "TEST169 clipboard path failure: " << test << "\n";
        std::exit(1);
    }
}

int main()
{
    using namespace lsampler_reaper_clipboard;
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("LSampler_TEST169_" + juce::Uuid().toString());
    expect(root.createDirectory().wasOk(), "temp directory");
    struct Cleanup
    {
        juce::File folder;
        ~Cleanup() { folder.deleteRecursively(); }
    } cleanup { root };
    const auto first = root.getChildFile(juce::String::fromUTF8("pianò 01.wav"));
    const auto second = root.getChildFile("long filename 02.wav");
    expect(first.create().wasOk(), "first file");
    expect(second.create().wasOk(), "second file");
    const auto a = first.getFullPathName();
    const auto b = second.getFullPathName();

    auto single = parseMediaSourcePaths(a);
    expect(single.recognised && single.items.size() == 1, "single absolute source");
    expect(single.items[0].file == first, "single file equality");
    expect(single.items[0].startSeconds == 0.0 && single.items[0].endSeconds == 0.0,
           "full file signal (no item crop)");

    auto multi = parseMediaSourcePaths("\"" + a + "\"\r\n" + b + "\r\n");
    expect(multi.recognised && multi.items.size() == 2, "multi line + quoted path");
    expect(multi.items[0].file == first && multi.items[1].file == second,
           "copied source order");
    expect(multi.items[0].name == first.getFileNameWithoutExtension(),
           "unicode sample name");

    auto bom = parseMediaSourcePaths(juce::String::charToString(0xfeff) + b + "\n");
    expect(bom.recognised && bom.items.size() == 1, "unicode BOM");

    auto missing = parseMediaSourcePaths(a + "\r\n" + root.getChildFile("missing.wav").getFullPathName());
    expect(missing.recognised && missing.items.size() == 1 && missing.skipped == 1,
           "missing file counted as skipped");
    auto allMissing = parseMediaSourcePaths(root.getChildFile("missing.wav").getFullPathName());
    expect(allMissing.recognised && allMissing.items.empty() && allMissing.error.isNotEmpty(),
           "missing path recognised with useful error");

    auto invalid = parseMediaSourcePaths("REAPER audio item\r\n" + a);
    expect(!invalid.recognised, "avoid interpreting prose as a path");
    auto rpp = parseMediaSourcePaths("<ITEM\nPOSITION 0\n>\n");
    expect(!rpp.recognised, "do not treat item chunks as paths");

    std::cout << "TEST169 clipboard path parsing passed\n";
    return 0;
}
