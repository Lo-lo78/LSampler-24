#pragma once
#include "ParameterSearchKey.h"

// TEST136 temporary UI-thread keyboard probe. No logging from the audio thread.
namespace lsampler {
inline bool& keyboardDiagnosticEnabled() { static bool enabled = false; return enabled; }
inline bool keyboardDiagnosticToggle(const juce::KeyPress& key) {
    const auto m = key.getModifiers();
    // TEST137: F11 is a known-working JUCE function key in the grid.
    // Use it as a second toggle to distinguish a blocked toggle from a
    // blocked digit. F11 is reserved only while running this test.
    if (key.getKeyCode() == juce::KeyPress::F11Key && !m.isCtrlDown()
        && !m.isAltDown() && !m.isShiftDown() && !m.isCommandDown())
        return true;
    return m.isCtrlDown() && m.isAltDown() && m.isShiftDown()
        && !m.isCommandDown()
        && (key.getKeyCode() == 'D' || key.getKeyCode() == 'd');
}
inline juce::String keyboardDiagnosticReport(const juce::KeyPress& key, const juce::String& context) {
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const auto typed = key.getTextCharacter();
    const auto search = parameterSearchCharacter(key);
    auto asText = [](juce::juce_wchar c) -> juce::String {
        return c >= 32 && c < 127 ? juce::String::charToString(c) : "none";
    };
    return "Keyboard diagnostic. " + context + ". Code " + juce::String(code)
        + ". Text " + juce::String(static_cast<int>(typed)) + ", " + asText(typed)
        + ". Search " + juce::String(static_cast<int>(search)) + ", " + asText(search)
        + ". Modifiers shift " + juce::String(int(mods.isShiftDown()))
        + ", control " + juce::String(int(mods.isCtrlDown()))
        + ", alt " + juce::String(int(mods.isAltDown())) + ".";
}
}
