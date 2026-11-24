# LSampler-24 0.99.29 TEST190 — HP Key Follow and BP Cutoff

Experimental source build on the published 0.99.29 base. Existing parameter IDs and descriptors 0–173 and host-registration order are unchanged.

- **HP Key Follow** (after HP Velocity): -1.000 to +1.000, default 0.000. Applies the same note-minus-Original-Pitch semitone tracking as LP Key Follow, independently for each slot and voice.
- **BP Cutoff** (after HP Cutoff): a physical linked edit of LP Cutoff and HP Cutoff, not another filter or an audio-rate modulation offset. Both existing cutoff values move by the same amount on their logarithmic 0–1 scale, so their frequency ratio stays constant. The macro reads the actual midpoint of the two values and stops both at the first boundary. Independently changing either cutoff immediately recalibrates the midpoint. A full-width pair (LP=1, HP=0) cannot translate until narrowed; crossed cutoffs are allowed. No filter activation, resonance, envelope, LFO or Key Follow changes are made.
- Both parameters are per-slot host parameters. Editor BP changes publish the underlying cutoff parameters using the existing host notification path. Host BP automation changes both canonical cutoff atomics, without sending dependent host notifications from the audio callback. Avoid simultaneous automation of BP and the two individual cutoff controls, which would compete for the same values. BP is not an Alt+K bank macro.
- Existing preset loads use the saved LP/HP values as authority; BP is derived, and missing HP tracking defaults to zero. No extra DSP stage.

Verification: standalone C++ macro tests and frozen catalog checks; added processor/DSP regression cases for the Windows JUCE test suite. Actual Windows VST3 build and REAPER audition are still required.

Suggested test: set LP=.600, HP=.300, BP reads .450. Set BP=.550; LP=.700, HP=.400. At the maximum achievable BP=.850, LP=1.000 and HP=.700. One downward step moves both again. With HP alone enabled, play an octave above/below Original Pitch at HP Key Follow 0, +1 and -1; compare the direction and intensity of the filter movement.
