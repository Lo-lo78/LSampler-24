#pragma once
#include "SamplePool.h"
#include <functional>

// Optional progress for worker-thread audio decoding. Separate from frozen
// SamplePool.h: TEST59 needs that shared dependency byte-for-byte unchanged.
namespace lsampler
{
using SampleLoadProgress = std::function<void(double)>;
std::shared_ptr<SharedSample> loadSampleWithProgress(const juce::File& file,
                                                     juce::String& error,
                                                     const SampleLoadProgress& progress);
}
