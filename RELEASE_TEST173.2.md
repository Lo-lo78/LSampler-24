# LSampler-24 0.99.25 TEST173.2 — Audio parity correction

The C++ build succeeded on GitHub Actions for TEST173.1 but CTest `filter-parity` failed in `all_LFO_destinations_filters_0`.

In TEST173, the voice start position was unconditionally reset to the beginning of the cropped sample, even for legacy snapshots produced by `prepareSlotAudioState` and not processed by the new Sample Set global timeline. This changed the established Sample Play Start behavior and broke comparison with the TEST59 frozen DSP reference.

TEST173.2 introduces an explicit `sharedSampleWindow` state flag, set only by `applySampleSetGlobalWindow`. Legacy snapshots continue using their original absolute Sample Play Start positioning; globally windowed snapshots start from their already-cropped global Start position, except explicit preview positions. The monophonic retrigger path follows the same distinction.

The Python TEST173 verifier now checks both start-position paths. C++ audio parity and Windows VST3 packaging still require GitHub Actions validation.
