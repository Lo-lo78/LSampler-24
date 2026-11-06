# LSampler-24 0.99.25 TEST173 — Sample Set timeline and automation

## Alt+M / F2 — per-sample parameters

1. Velocity Low: 1–127
2. Velocity High: 1–127
3. Pitch: -1200…+1200 cents
4. Octave: -4…+4
5. Pan: -100…+100
6. Volume: -120…+24 dB
7. Delay: 0…1000 ms

Each of the 16 entries in each of the 24 slots has independent values. These are stored in the preset, copied into the saved-slot Alt+S preview, and exposed to REAPER automation by Ctrl+Shift+A. The five new automation parameters (Pitch, Octave, Pan, Volume, Delay) are registered at the end of the existing host parameter list so previous VST3 indices are not displaced.

## Shared Start/End window

All samples in a slot share a timeline. The end of that timeline is the latest natural sample end including its Delay. The slot-wide Sample Start and Sample End percentages crop this timeline. A sample that starts after the window starts waits only its remaining Delay; a sample already in progress starts at the corresponding point in the source. Samples retain independent lengths; they are not time-stretched to the longest sample.

A Note Off received before a delayed voice has started suppresses the scheduled late sound. Delayed voices use the existing global voice pool rather than increasing the 96 voice capacity.

## Tests / build

All existing Python structural verifiers, plus TEST173, passed in the source environment. A Windows VST3 build and an actual audition in REAPER must still be performed via GitHub Actions and user testing. Pay particular attention to loop and slice interactions, a stereo Layer preset in Alt+S, and live automation of Start/End and Delay.
