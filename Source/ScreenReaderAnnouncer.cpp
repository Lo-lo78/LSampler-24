#include "ScreenReaderAnnouncer.h"

#if JUCE_WINDOWS
 #include <Windows.h>
 #include <UIAutomation.h>
 #include <oleauto.h>
#endif

namespace lsampler
{
void announceToActiveScreenReader(juce::Component& source, const juce::String& message)
{
    source.setDescription(message);

   #if JUCE_WINDOWS
    if (!UiaClientsAreListening())
        return;

    auto* handler = source.getAccessibilityHandler();
    if (handler == nullptr)
        return;

    auto* unknown = reinterpret_cast<IUnknown*>(handler->getNativeImplementation());
    if (unknown == nullptr)
        return;

    IRawElementProviderSimple* provider = nullptr;
    if (FAILED(unknown->QueryInterface(__uuidof(IRawElementProviderSimple),
                                       reinterpret_cast<void**>(&provider))))
        return;

    auto* text = SysAllocString(message.toWideCharPointer());
    auto* activity = SysAllocString(L"LSampler24");
    UiaRaiseNotificationEvent(provider,
                              NotificationKind_Other,
                              NotificationProcessing_ImportantMostRecent,
                              text,
                              activity);
    SysFreeString(text);
    SysFreeString(activity);
    provider->Release();
   #else
    if (auto* handler = source.getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::titleChanged);
   #endif
}
}
