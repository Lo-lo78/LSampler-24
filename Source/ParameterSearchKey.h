#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Keyboard type-ahead: virtual key codes are used only as a fallback for
// numbers/punctuation. Function keys overlap ASCII on some backends.
inline juce::juce_wchar parameterSearchCharacter(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F12Key)
        return 0;
    const auto mods = key.getModifiers();
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return 0;
    const auto typed = key.getTextCharacter();
    if (typed >= 33 && typed != 127 && typed < 0x110000)
        return juce::CharacterFunctions::toLowerCase(typed);
    // Some hosts deliver digits and punctuation with a zero text character.
    // Limit fallback to non-letter keys to preserve function-key isolation.
    if (code >= '0' && code <= '9') return static_cast<juce::juce_wchar>(code);
    if (code == '.' || code == ',' || code == '-' || code == '_' ||
        code == '/' || code == '\\' || code == ';' || code == ':' ||
        code == '[' || code == ']' || code == '=' || code == '+' ||
        code == '\'' || code == '`')
        return static_cast<juce::juce_wchar>(code);
    return 0;
}
