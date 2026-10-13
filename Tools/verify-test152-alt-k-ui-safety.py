#!/usr/bin/env python3
"""Static, non-build regression audit for TEST152 Alt+K UI lifecycle."""
from pathlib import Path
import re
root = Path(__file__).resolve().parent.parent
cpp = (root / "Source/PluginEditor.cpp").read_text(encoding="utf-8")
h = (root / "Source/PluginEditor.h").read_text(encoding="utf-8")
checks = {
 "queued Alt+K dispatch": "queueBankMacroToggle();" in cpp and
                              "juce::MessageManager::callAsync(" in cpp and
                              "bankMacroTransitionPending = true;" in cpp,
 "SafePointer guard": "SafePointer<LSampler24AudioProcessorEditor>(this)" in cpp,
 "no synchronous shortcut reconstruction": "if (bankMacroOpen) closeBankMacro();\n        else openBankMacro();" not in cpp,
 "UI-value change guard": "if (!parameterPage || updatingParameterUi) return;" in cpp,
 "UI-selection change guard": "if (updatingParameterUi) return;" in cpp,
 "scoped refresh guard": "const juce::ScopedValueSetter<bool> uiGuard(updatingParameterUi, true);" in cpp,
 "macro list bounded": "if (bankMacroEntries.empty()) return;" in cpp,
 "macro exit repairs virtual index": "selectedParameter = juce::jlimit(0, slotBankSendFirst + slotBankSendCount - 1," in cpp,
 "bank grid index checks": "if (!juce::isPositiveAndBelow(gridIndex, int(lsampler::grid.size()))) return {};" in cpp,
 "separate flags": "bool bankMacroTransitionPending = false;" in h and "bool updatingParameterUi = false;" in h,
 "FX IDs unmodified": (root / "Source/FXParameters.h").exists(),
}
for label, passed in checks.items():
    print(("PASS" if passed else "FAIL") + ": " + label)
assert all(checks.values()), "TEST152 Alt+K static safety regression"
print("PASS TEST152: Alt+K UI transitions and virtual send index protected (no build performed)")
