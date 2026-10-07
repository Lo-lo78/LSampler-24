# LSampler-24 VST3 automation contract (TEST79)

Source-only implementation; no configure, compile, binary generation or runtime audio tests were performed.

## Fixed parameter registry

6,629 new host parameters: 5 global + 24 slots × 276. Each Slot has 140 catalog parameters, 90 loop parameters (10 × 9), 13 Slice globals, one variation mode, and 32 Sample Set velocity bounds (16 × 2). No previous AudioProcessorParameters existed.

IDs are `global_<persistent_key>`, `slotNN_<persistent_key>`, `slotNN_loopNN_<persistent_key>`, `slotNN_slice_<persistent_key>`, and `slotNN_sampleNN_velocity_low/high`. NN starts at 01. They never depend on selection, translation, visibility, loaded files, or grid indices. The parameter registration order is frozen in VST3_AUTOMATION_MANIFEST.json. Add future parameters at the END of registration, never insert/reorder/remove existing ones, reuse a retired ID, or change its meaning/range. ParameterID version hint is 1.

Names differ from the internal UI where useful: input_gain = Volume; lp_cutoff = Filter Cutoff; lp_resonance = Filter Resonance. HP retains its separate HP name. IDs preserve old persistent keys; no preset schema replacement.

## Canonical values and threads

HostParameter holds the canonical real value in a lock-free atomic double. Host setValue only sanitises/publishes atomics and revision counters. It does not acquire stateLock, post messages, notify the host, touch samples, scan audio or call editor code. UI/state reads materialise changed host values under the existing control lock. UI setters use the existing paths; the publisher detects actual control changes against a mirror and notifies the host with beginChangeGesture / sendValueChangedMessageToListeners / endChangeGesture. This is the notification portion of setValueNotifyingHost without a lossy double→normalised float→double writeback. Message-thread controls notify immediately; worker changes use AsyncUpdater. Automation never schedules this updater. Deferred notifications read the latest canonical value and cannot replay an older UI edit over DAW automation.

The audio thread checks 24 generation counters per block. HostSlicePreparation.h prepares already-sanitised Slice metadata without allocating JUCE Strings; frozen TEST59 shared sources remain byte-identical. Only changed Slots rebuild bounded scalar coefficients/loop settings and (when needed) Slice metadata in preallocated storage. Existing prepare functions and voice update logic are reused. Samples remain owned by the existing triple-buffer snapshots and retirement scheme; no audio-thread ownership/allocation/file operations are added. Changes are block-granular under the existing JUCE processBlock interface, not claimed sample-accurate. No new smoothing or DSP algorithms were added. Dense simultaneous automation requires later CPU testing.

## Position and sync semantics

Sample Start/End automation changes non-destructive playback bounds. Sample Play Start remains the note-on/scrub anchor used by the current engine; changing it does not continuously seek an already running voice. End below Start produces a safe minimum-length playback window, with frame clamping; the independent host values are not rewritten by the audio thread. Loop bounds retain the existing safe frame conversion.

Start/End automation bypasses threshold scanning and uses raw percentage bounds. Other automated controls preserve the threshold-derived bounds of the published sample. Threshold scans remain UI-only. When Start/End automation has changed the control-side window, its cached playback bounds follow that raw window; editing a threshold explicitly rescans through the original UI path. This distinction avoids unbounded scans in processBlock.

LFO frequency uses the existing real range; when BPM sync is enabled the canonical rate is quantised to 1/8 with minimum 0.125, as in the Grid setter. Cutoff keeps the original 0..1 logarithmic DSP mapping (20 × 500^value Hz); it is not reinterpreted as a new 20..22000 Hz range. Continuous values retain their precision and original ranges. Integers/notes/enums expose proper discrete step counts; exact Off/On pairs are booleans. Text conversion supports the catalog labels.

Slice division is an existing musical setting. Automating it uses setDivision, resetting normalised boundary layout exactly as the editor does; step data remain intact. Individual sequencer steps, custom boundary editing, zero-crossing commands and undo data are not host parameters. Slice accessibility reads current values on demand without an announcement on every automation point. Existing Grid/Value refresh continues through uiRevision without explicit speech in automation callbacks.

## Compatibility

Existing Parameters/Loops/SampleSet/Slice trees and version-2 aliases remain authoritative for persistence. Saved state and slot/bank presets read canonical host values. Restore explicitly publishes every value of the restored Slot, including values equal to former defaults, without forcing untouched Slots or creating touch/write gestures. Legacy sessions/presets retain their existing migration and sample resolution logic. No sample paths, names or audio buffers enter the automation registry.

## Intentionally excluded

Start Threshold, End Threshold (sample scans); End Preview Length (editing audition preference). All file/browser/preset/Sample Set loading/saving, import/export, selected Slot/page/grid position/focus/dialogs, names/paths, Help/About, destructive Zero Crossing/trim/commit, copy/paste, undo/redo/delete and per-step sequencer records remain non-automatable. No fabricated per-sample volume/pan properties or generic selected-control parameters were added. Round Robin and both Random modes use the existing variationMode engine.

## Global catalog

| ID | Host name | Range | Default | Unit / choices |
|---|---|---|---|---|
| global_master_output_gain | Master Output Gain | -120 .. 24 | 0 | dB |
| global_output_stage | Output Stage | 0 .. 1 | 1 | Off, LR-608 |
| global_bus_glue | Bus Glue | 0 .. 100 | 35 | % |
| global_bus_soft_drive | Bus Soft Drive | 0 .. 100 | 0 | % |
| global_output_ceiling | Output Ceiling | 0.1 .. 1 | 0.98 |  |

## Slot 1 catalog (same suffixes/ranges for Slots 2–24)

| ID | Host name | Range | Default | Unit / choices |
|---|---|---|---|---|
| slot01_input_gain | Slot 1 - Volume | -120 .. 24 | 0 | dB |
| slot01_polyphony | Slot 1 - Voice Mode | 0 .. 1 | 1 | Mono, Poly |
| slot01_slot_polyphony | Slot 1 - Slot Polyphony | 0 .. 24 | 0 | Auto, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 |
| slot01_poly_drift | Slot 1 - Poly Drift | 0 .. 1 | 0 |  |
| slot01_portamento | Slot 1 - Portamento | 0 .. 1000 | 0 | ms |
| slot01_legato | Slot 1 - Legato | 0 .. 1 | 1 | Off, On |
| slot01_sustain_pedal | Slot 1 - Sustain Pedal | 0 .. 1 | 0 | Off, On |
| slot01_same_note_replace | Slot 1 - Same Note Replace | 0 .. 1 | 0 | Off, On |
| slot01_pitch_bend_range | Slot 1 - Pitch Bend Range | 0 .. 48 | 2 | semitones |
| slot01_mode | Slot 1 - Audio | 0 .. 1 | 1 | Mono, Stereo |
| slot01_output_route | Slot 1 - Slot Output | 0 .. 24 | 0 | Main 1/2, Out 3/4, Out 5/6, Out 7/8, Out 9/10, Out 11/12, Out 13/14, Out 15/16, Out 17/18, Out 19/20, Out 21/22, Out 23/24, Out 25/26, Out 27/28, Out 29/30, Out 31/32, Out 33/34, Out 35/36, Out 37/38, Out 39/40, Out 41/42, Out 43/44, Out 45/46, Out 47/48, Out 49/50 |
| slot01_ram_stereo_width | Slot 1 - Audio Width | 0 .. 200 | 100 | % |
| slot01_ram_swap_lr | Slot 1 - Audio Swap | 0 .. 1 | 0 | Left Right, Right Left |
| slot01_ram_downsample | Slot 1 - RAM Downsample | 0 .. 10 | 0 | Off, 32 kHz 16 bit, 22 kHz 12 bit, 22 kHz 8 bit, 12 kHz 8 bit, 12 kHz 4 bit, 11 kHz 12 bit, 11 kHz 8 bit, 8 kHz 12 bit, 8 kHz 8 bit, 8 kHz 4 bit |
| slot01_pan | Slot 1 - Pan | -100 .. 100 | 0 | % |
| slot01_low | Slot 1 - Low Key | 0 .. 127 | 0 |  |
| slot01_high | Slot 1 - High Key | 0 .. 127 | 127 |  |
| slot01_root | Slot 1 - Original Pitch | 0 .. 127 | 60 |  |
| slot01_octave | Slot 1 - Octave | -4 .. 4 | 0 |  |
| slot01_voice_pitch | Slot 1 - Fine Pitch | -9.6 .. 9.6 | 0 | semitones |
| slot01_vel_volume_depth | Slot 1 - Velocity Volume Depth | -1 .. 1 | 1 |  |
| slot01_velocity_low | Slot 1 - Velocity Low | 0 .. 127 | 0 |  |
| slot01_velocity_high | Slot 1 - Velocity High | 0 .. 127 | 127 |  |
| slot01_choke_trigger | Slot 1 - Choke Trigger Note | -1 .. 127 | -1 |  |
| slot01_choke_target | Slot 1 - Choke Target Note | -1 .. 127 | -1 |  |
| slot01_choke_mode | Slot 1 - Choke Mode | 0 .. 1 | 0 | Kill, Release |
| slot01_sample_start | Slot 1 - Sample Start | 0 .. 100 | 0 | % |
| slot01_sample_end | Slot 1 - Sample End | 0 .. 100 | 100 | % |
| slot01_sample_play_start | Slot 1 - Sample Play Start | 0 .. 100 | 0 | % |
| slot01_global_one_shot | Slot 1 - Global One Shot | 0 .. 2 | 1 | Off, On, On Release |
| slot01_start_end_fade | Slot 1 - Start End Fade | 0 .. 100 | 0 | ms |
| slot01_stereo_delay_left | Slot 1 - Stereo Delay Left | 0 .. 50 | 0 | ms |
| slot01_stereo_delay_right | Slot 1 - Stereo Delay Right | 0 .. 50 | 0 | ms |
| slot01_stretch_amount | Slot 1 - Stretch Amount | -64 .. 64 | 0 |  |
| slot01_stretch_frequency | Slot 1 - Stretch Frequency | 10 .. 20000 | 220 | Hz |
| slot01_normalize_on | Slot 1 - Normalize | 0 .. 1 | 0 | Off, On |
| slot01_normalize_target | Slot 1 - Normalize Target | -24 .. 0 | -6 | dB |
| slot01_transient_shape | Slot 1 - Transient Shape | -100 .. 100 | 0 | % |
| slot01_transient_speed | Slot 1 - Transient Speed | 1 .. 500 | 25 | ms |
| slot01_transient_mix | Slot 1 - Transient Mix | 0 .. 100 | 100 | % |
| slot01_ram_fade_in | Slot 1 - Fade In | 0 .. 50 | 0 | ms |
| slot01_ram_fade_out | Slot 1 - Fade Out | 0 .. 50 | 0 | ms |
| slot01_ram_reverse | Slot 1 - Reverse | 0 .. 1 | 0 | Off, On |
| slot01_dc_remove | Slot 1 - DC Remove | 0 .. 1 | 0 | Off, On |
| slot01_machine_character | Slot 1 - Machine Character | 0 .. 7 | 0 | Off, Clean, L900, LEmax, L3200, LItalian, LBroken, LFuture |
| slot01_character_depth | Slot 1 - Character Depth | 0 .. 10000 | 0 |  |
| slot01_character_input_drive | Slot 1 - Input Drive | 0 .. 10000 | 0 |  |
| slot01_character_converter | Slot 1 - Converter Color | 0 .. 10000 | 0 |  |
| slot01_character_playback_life | Slot 1 - Playback Life | 0 .. 10000 | 0 |  |
| slot01_character_repeat_life | Slot 1 - Repeat Life | 0 .. 10000 | 0 |  |
| slot01_character_air_body | Slot 1 - Air Body | 0 .. 10000 | 0 |  |
| slot01_character_output_glue | Slot 1 - Output Glue | 0 .. 10000 | 0 |  |
| slot01_attack | Slot 1 - Env Attack | 0 .. 20 | 0 | s |
| slot01_decay | Slot 1 - Env Decay | 0 .. 20 | 0.1 | s |
| slot01_sustain | Slot 1 - Env Sustain | 0 .. 1 | 1 |  |
| slot01_release | Slot 1 - Env Release | 0 .. 20 | 0.1 | s |
| slot01_retrigger_smooth | Slot 1 - No Retrigger Smooth | 0 .. 10 | 0 | ms |
| slot01_pitch_env | Slot 1 - Pitch Env | -48 .. 48 | 0 | semitones |
| slot01_pan_env | Slot 1 - Pan Env | -100 .. 100 | 0 | % |
| slot01_vel_attack_depth | Slot 1 - Velocity Sample Start Depth | 0 .. 100 | 0 | % |
| slot01_lp_on | Slot 1 - LP | 0 .. 1 | 0 | Off, On |
| slot01_lp_cutoff | Slot 1 - Filter Cutoff | 0 .. 1 | 1 |  |
| slot01_lp_resonance | Slot 1 - Filter Resonance | 0 .. 1 | 0 |  |
| slot01_lp_env_amount | Slot 1 - LP Env | -96 .. 96 | 0 | semitones |
| slot01_lp_env_attack | Slot 1 - LP Env Attack | 0 .. 20 | 0.005 |  |
| slot01_lp_env_decay | Slot 1 - LP Env Decay | 0 .. 20 | 0.1 |  |
| slot01_lp_env_sustain | Slot 1 - LP Env Sustain | 0 .. 1 | 1 |  |
| slot01_lp_env_release | Slot 1 - LP Env Release | 0 .. 20 | 0.1 |  |
| slot01_lp_vel_amount | Slot 1 - LP Velocity | -1 .. 1 | 0 |  |
| slot01_lp_key_follow | Slot 1 - LP Key Follow | -1 .. 1 | 0 |  |
| slot01_hp_on | Slot 1 - HP | 0 .. 1 | 0 | Off, On |
| slot01_hp_cutoff | Slot 1 - HP Cutoff | 0 .. 1 | 0 |  |
| slot01_hp_resonance | Slot 1 - HP Resonance | 0 .. 1 | 0 |  |
| slot01_hp_env_amount | Slot 1 - HP Env | -96 .. 96 | 0 | semitones |
| slot01_hp_env_attack | Slot 1 - HP Env Attack | 0 .. 20 | 0.005 |  |
| slot01_hp_env_decay | Slot 1 - HP Env Decay | 0 .. 20 | 0.1 |  |
| slot01_hp_env_sustain | Slot 1 - HP Env Sustain | 0 .. 1 | 1 |  |
| slot01_hp_env_release | Slot 1 - HP Env Release | 0 .. 20 | 0.1 |  |
| slot01_hp_vel_amount | Slot 1 - HP Velocity | -1 .. 1 | 0 |  |
| slot01_lfo1_rate | Slot 1 - LFO 1 Frequency | 0.001 .. 512 | 4 |  |
| slot01_lfo1_mode | Slot 1 - LFO 1 Mode | 0 .. 1 | 0 | Free, Trigger |
| slot01_lfo1_bpm_sync | Slot 1 - LFO 1 BPM Sync | 0 .. 1 | 0 | Off, On |
| slot01_lfo1_one_shot | Slot 1 - LFO 1 One Shot | 0 .. 1 | 0 | Off, On |
| slot01_lfo1_wave | Slot 1 - LFO 1 Waveform | 0 .. 5 | 0 | Sine, Triangle, Saw Up, Saw Down, Square, Sample And Hold |
| slot01_lfo1_delay | Slot 1 - LFO 1 Delay | 0 .. 10 | 0 | s |
| slot01_lfo1_smoothing | Slot 1 - LFO 1 Smoothing | 0 .. 1 | 0 |  |
| slot01_lfo1_volume_depth | Slot 1 - LFO 1 Volume Depth | -1 .. 1 | 0 |  |
| slot01_lfo1_pan_depth | Slot 1 - LFO 1 Pan Depth | -1 .. 1 | 0 |  |
| slot01_lfo1_lp_depth | Slot 1 - LFO 1 LP Depth | -96 .. 96 | 0 | semitones |
| slot01_lfo1_hp_depth | Slot 1 - LFO 1 HP Depth | -96 .. 96 | 0 | semitones |
| slot01_lfo1_pitch_depth | Slot 1 - LFO 1 Pitch Depth | -96 .. 96 | 0 | semitones |
| slot01_lfo1_sample_depth | Slot 1 - LFO 1 Sample Move Depth | -100 .. 100 | 0 | % |
| slot01_lfo2_rate | Slot 1 - LFO 2 Frequency | 0.001 .. 512 | 4 |  |
| slot01_lfo2_mode | Slot 1 - LFO 2 Mode | 0 .. 1 | 0 | Free, Trigger |
| slot01_lfo2_bpm_sync | Slot 1 - LFO 2 BPM Sync | 0 .. 1 | 0 | Off, On |
| slot01_lfo2_one_shot | Slot 1 - LFO 2 One Shot | 0 .. 1 | 0 | Off, On |
| slot01_lfo2_wave | Slot 1 - LFO 2 Waveform | 0 .. 5 | 0 | Sine, Triangle, Saw Up, Saw Down, Square, Sample And Hold |
| slot01_lfo2_delay | Slot 1 - LFO 2 Delay | 0 .. 10 | 0 | s |
| slot01_lfo2_smoothing | Slot 1 - LFO 2 Smoothing | 0 .. 1 | 0 |  |
| slot01_lfo2_volume_depth | Slot 1 - LFO 2 Volume Depth | -1 .. 1 | 0 |  |
| slot01_lfo2_pan_depth | Slot 1 - LFO 2 Pan Depth | -1 .. 1 | 0 |  |
| slot01_lfo2_lp_depth | Slot 1 - LFO 2 LP Depth | -96 .. 96 | 0 | semitones |
| slot01_lfo2_hp_depth | Slot 1 - LFO 2 HP Depth | -96 .. 96 | 0 | semitones |
| slot01_lfo2_pitch_depth | Slot 1 - LFO 2 Pitch Depth | -96 .. 96 | 0 | semitones |
| slot01_lfo2_mod_pitch_depth | Slot 1 - Mod Wheel LFO 2 Pitch Depth | -24 .. 24 | 2 | semitones |
| slot01_lfo2_sample_depth | Slot 1 - LFO 2 Sample Move Depth | -100 .. 100 | 0 | % |
| slot01_global_release_loops | Slot 1 - Release Loop | 0 .. 1 | 0 | Off, On |
| slot01_loop_crossfade | Slot 1 - Loop Crossfade | 0 .. 200 | 0 | ms |
| slot01_drive_type | Slot 1 - Drive RAM | 0 .. 3 | 0 | Off, Tape, Soft Clip, Digital Clip |
| slot01_drive_amount | Slot 1 - Drive Amount | 0 .. 100 | 0 | % |
| slot01_drive_position | Slot 1 - Drive Position | 0 .. 1 | 0 | Before Comp Gate, After Comp Gate |
| slot01_comp_on | Slot 1 - Comp | 0 .. 1 | 0 | Off, On |
| slot01_comp_threshold | Slot 1 - Comp Threshold | -60 .. 0 | -18 | dB |
| slot01_comp_ratio | Slot 1 - Comp Ratio | 1 .. 20 | 4 |  |
| slot01_comp_attack | Slot 1 - Comp Attack | 0.1 .. 100 | 5 | ms |
| slot01_comp_release | Slot 1 - Comp Release | 1 .. 10000 | 80 | ms |
| slot01_comp_makeup | Slot 1 - Comp Makeup | 0 .. 24 | 0 | dB |
| slot01_comp_mix | Slot 1 - Comp Mix | 0 .. 100 | 100 | % |
| slot01_comp_delta | Slot 1 - Comp Delta Solo | 0 .. 1 | 0 | Off, On |
| slot01_dynamics_order | Slot 1 - Comp Order | 0 .. 1 | 0 | Comp Before Gate, Comp After Gate |
| slot01_gate_on | Slot 1 - Gate | 0 .. 1 | 0 | Off, On |
| slot01_gate_threshold | Slot 1 - Gate Threshold | -90 .. 0 | -60 | dB |
| slot01_gate_attack | Slot 1 - Gate Attack | 0.1 .. 100 | 1 | ms |
| slot01_gate_release | Slot 1 - Gate Release | 1 .. 2000 | 120 | ms |
| slot01_gate_hold | Slot 1 - Gate Hold | 0 .. 10000 | 0 | ms |
| slot01_gate_depth | Slot 1 - Gate Depth | 0 .. 120 | 80 | dB |
| slot01_gate_mix | Slot 1 - Gate Mix | 0 .. 100 | 100 | % |
| slot01_gate_delta | Slot 1 - Gate Delta Solo | 0 .. 1 | 0 | Off, On |
| slot01_ring_mode | Slot 1 - Ring | 0 .. 2 | 0 | Off, Fixed, Follow |
| slot01_ring_wave | Slot 1 - Ring Wave | 0 .. 5 | 0 | Sine, Triangle, Saw Up, Saw Down, Square, Noise |
| slot01_ring_amount | Slot 1 - Ring Amount | 0 .. 1 | 0 |  |
| slot01_ring_freq | Slot 1 - Ring Freq | 1 .. 20000 | 440 | Hz |
| slot01_degrade_amount | Slot 1 - Degrade Amount | 0 .. 100 | 0 | % |
| slot01_degrade_bits | Slot 1 - Degrade Bits | 1 .. 16 | 8 | bits |
| slot01_degrade_hold | Slot 1 - Degrade Hold | 1 .. 64 | 4 | samples |
| slot01_degrade_jitter | Slot 1 - Degrade Jitter | 0 .. 100 | 0 | % |
| slot01_fm_amount | Slot 1 - FM Amount | 0 .. 100 | 0 | % |
| slot01_fm_ratio | Slot 1 - FM Ratio | 0.125 .. 64 | 1 |  |
| slot01_fm_wave | Slot 1 - FM Wave | 0 .. 5 | 0 | Sine, Triangle, Saw Up, Saw Down, Square, Noise |
| slot01_fm_feedback | Slot 1 - FM Feedback | 0 .. 100 | 0 | % |
| slot01_loop01_loop_start | Slot 1 - Loop 1 - Start | 0 .. 100 | 0 | % |
| slot01_loop01_loop_end | Slot 1 - Loop 1 - End | 0 .. 100 | 100 | % |
| slot01_loop01_loop_repeats | Slot 1 - Loop 1 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop01_loop_fade_in | Slot 1 - Loop 1 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop01_loop_fade_out | Slot 1 - Loop 1 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop01_loop_pitch_down | Slot 1 - Loop 1 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop01_loop_lp_down | Slot 1 - Loop 1 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop01_loop_hp_down | Slot 1 - Loop 1 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop01_loop_one_shot | Slot 1 - Loop 1 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop02_loop_start | Slot 1 - Loop 2 - Start | 0 .. 100 | 0 | % |
| slot01_loop02_loop_end | Slot 1 - Loop 2 - End | 0 .. 100 | 100 | % |
| slot01_loop02_loop_repeats | Slot 1 - Loop 2 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop02_loop_fade_in | Slot 1 - Loop 2 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop02_loop_fade_out | Slot 1 - Loop 2 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop02_loop_pitch_down | Slot 1 - Loop 2 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop02_loop_lp_down | Slot 1 - Loop 2 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop02_loop_hp_down | Slot 1 - Loop 2 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop02_loop_one_shot | Slot 1 - Loop 2 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop03_loop_start | Slot 1 - Loop 3 - Start | 0 .. 100 | 0 | % |
| slot01_loop03_loop_end | Slot 1 - Loop 3 - End | 0 .. 100 | 100 | % |
| slot01_loop03_loop_repeats | Slot 1 - Loop 3 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop03_loop_fade_in | Slot 1 - Loop 3 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop03_loop_fade_out | Slot 1 - Loop 3 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop03_loop_pitch_down | Slot 1 - Loop 3 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop03_loop_lp_down | Slot 1 - Loop 3 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop03_loop_hp_down | Slot 1 - Loop 3 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop03_loop_one_shot | Slot 1 - Loop 3 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop04_loop_start | Slot 1 - Loop 4 - Start | 0 .. 100 | 0 | % |
| slot01_loop04_loop_end | Slot 1 - Loop 4 - End | 0 .. 100 | 100 | % |
| slot01_loop04_loop_repeats | Slot 1 - Loop 4 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop04_loop_fade_in | Slot 1 - Loop 4 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop04_loop_fade_out | Slot 1 - Loop 4 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop04_loop_pitch_down | Slot 1 - Loop 4 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop04_loop_lp_down | Slot 1 - Loop 4 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop04_loop_hp_down | Slot 1 - Loop 4 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop04_loop_one_shot | Slot 1 - Loop 4 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop05_loop_start | Slot 1 - Loop 5 - Start | 0 .. 100 | 0 | % |
| slot01_loop05_loop_end | Slot 1 - Loop 5 - End | 0 .. 100 | 100 | % |
| slot01_loop05_loop_repeats | Slot 1 - Loop 5 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop05_loop_fade_in | Slot 1 - Loop 5 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop05_loop_fade_out | Slot 1 - Loop 5 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop05_loop_pitch_down | Slot 1 - Loop 5 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop05_loop_lp_down | Slot 1 - Loop 5 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop05_loop_hp_down | Slot 1 - Loop 5 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop05_loop_one_shot | Slot 1 - Loop 5 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop06_loop_start | Slot 1 - Loop 6 - Start | 0 .. 100 | 0 | % |
| slot01_loop06_loop_end | Slot 1 - Loop 6 - End | 0 .. 100 | 100 | % |
| slot01_loop06_loop_repeats | Slot 1 - Loop 6 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop06_loop_fade_in | Slot 1 - Loop 6 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop06_loop_fade_out | Slot 1 - Loop 6 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop06_loop_pitch_down | Slot 1 - Loop 6 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop06_loop_lp_down | Slot 1 - Loop 6 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop06_loop_hp_down | Slot 1 - Loop 6 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop06_loop_one_shot | Slot 1 - Loop 6 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop07_loop_start | Slot 1 - Loop 7 - Start | 0 .. 100 | 0 | % |
| slot01_loop07_loop_end | Slot 1 - Loop 7 - End | 0 .. 100 | 100 | % |
| slot01_loop07_loop_repeats | Slot 1 - Loop 7 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop07_loop_fade_in | Slot 1 - Loop 7 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop07_loop_fade_out | Slot 1 - Loop 7 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop07_loop_pitch_down | Slot 1 - Loop 7 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop07_loop_lp_down | Slot 1 - Loop 7 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop07_loop_hp_down | Slot 1 - Loop 7 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop07_loop_one_shot | Slot 1 - Loop 7 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop08_loop_start | Slot 1 - Loop 8 - Start | 0 .. 100 | 0 | % |
| slot01_loop08_loop_end | Slot 1 - Loop 8 - End | 0 .. 100 | 100 | % |
| slot01_loop08_loop_repeats | Slot 1 - Loop 8 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop08_loop_fade_in | Slot 1 - Loop 8 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop08_loop_fade_out | Slot 1 - Loop 8 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop08_loop_pitch_down | Slot 1 - Loop 8 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop08_loop_lp_down | Slot 1 - Loop 8 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop08_loop_hp_down | Slot 1 - Loop 8 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop08_loop_one_shot | Slot 1 - Loop 8 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop09_loop_start | Slot 1 - Loop 9 - Start | 0 .. 100 | 0 | % |
| slot01_loop09_loop_end | Slot 1 - Loop 9 - End | 0 .. 100 | 100 | % |
| slot01_loop09_loop_repeats | Slot 1 - Loop 9 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop09_loop_fade_in | Slot 1 - Loop 9 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop09_loop_fade_out | Slot 1 - Loop 9 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop09_loop_pitch_down | Slot 1 - Loop 9 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop09_loop_lp_down | Slot 1 - Loop 9 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop09_loop_hp_down | Slot 1 - Loop 9 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop09_loop_one_shot | Slot 1 - Loop 9 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_loop10_loop_start | Slot 1 - Loop 10 - Start | 0 .. 100 | 0 | % |
| slot01_loop10_loop_end | Slot 1 - Loop 10 - End | 0 .. 100 | 100 | % |
| slot01_loop10_loop_repeats | Slot 1 - Loop 10 - Repeats | 0 .. 128 | 0 |  |
| slot01_loop10_loop_fade_in | Slot 1 - Loop 10 - Fade In | 0 .. 200 | 0 | ms |
| slot01_loop10_loop_fade_out | Slot 1 - Loop 10 - Fade Out | 0 .. 200 | 0 | ms |
| slot01_loop10_loop_pitch_down | Slot 1 - Loop 10 - Repeat Pitch Amount | -48 .. 48 | 0 | semitones |
| slot01_loop10_loop_lp_down | Slot 1 - Loop 10 - Repeat LP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop10_loop_hp_down | Slot 1 - Loop 10 - Repeat HP Amount | -96 .. 96 | 0 | semitones |
| slot01_loop10_loop_one_shot | Slot 1 - Loop 10 - One Shot | 0 .. 1 | 0 | Off, On |
| slot01_slice_mode | Slot 1 - Slice Mode | 0 .. 7 | 0 | Off, Reverse, Pendulum, Random, Random No Repeat, Center Out, Edges In, Sequencer |
| slot01_slice_division | Slot 1 - Slice Division | 0 .. 13 | 5 | 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128 |
| slot01_slice_pitchTime | Slot 1 - Slice Pitch Step Time | 0 .. 1 | 0 | Original Pitch Step, Pitch Resizes Step |
| slot01_slice_random | Slot 1 - Slice Seq Random | 0 .. 2 | 0 | Off, Random, Random No Repeat |
| slot01_slice_shuffle | Slot 1 - Slice Shuffle | 0 .. 100 | 0 |  |
| slot01_slice_shufflePitch | Slot 1 - Slice Shuffle Pitch | 0 .. 1 | 0 | Off, On |
| slot01_slice_fadeIn | Slot 1 - Slice Fade In | 0 .. 1000 | 0 |  |
| slot01_slice_fadeOut | Slot 1 - Slice Fade Out | 0 .. 1000 | 0 |  |
| slot01_slice_panMode | Slot 1 - Slice Pan Mode | 0 .. 2 | 0 | Off, Follow Slice Mode, Follow Slice Mode Reverse |
| slot01_slice_panDepth | Slot 1 - Slice Pan Depth | 0 .. 1 | 1 |  |
| slot01_slice_pitchMode | Slot 1 - Slice Pitch Mode | 0 .. 2 | 0 | Off, Follow Slice Mode, Follow Slice Mode Reverse |
| slot01_slice_pitchDepth | Slot 1 - Slice Pitch Depth | 0 .. 48 | 12 |  |
| slot01_slice_midiMap | Slot 1 - Slice MIDI Map | 0 .. 1 | 0 | Off, On |
| slot01_variation_mode | Slot 1 - Variation Mode | 0 .. 3 | 0 | Off, Round Robin, Random, Random No Repeat |
| slot01_sample01_velocity_low | Slot 1 - Sample 1 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample01_velocity_high | Slot 1 - Sample 1 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample02_velocity_low | Slot 1 - Sample 2 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample02_velocity_high | Slot 1 - Sample 2 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample03_velocity_low | Slot 1 - Sample 3 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample03_velocity_high | Slot 1 - Sample 3 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample04_velocity_low | Slot 1 - Sample 4 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample04_velocity_high | Slot 1 - Sample 4 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample05_velocity_low | Slot 1 - Sample 5 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample05_velocity_high | Slot 1 - Sample 5 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample06_velocity_low | Slot 1 - Sample 6 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample06_velocity_high | Slot 1 - Sample 6 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample07_velocity_low | Slot 1 - Sample 7 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample07_velocity_high | Slot 1 - Sample 7 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample08_velocity_low | Slot 1 - Sample 8 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample08_velocity_high | Slot 1 - Sample 8 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample09_velocity_low | Slot 1 - Sample 9 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample09_velocity_high | Slot 1 - Sample 9 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample10_velocity_low | Slot 1 - Sample 10 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample10_velocity_high | Slot 1 - Sample 10 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample11_velocity_low | Slot 1 - Sample 11 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample11_velocity_high | Slot 1 - Sample 11 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample12_velocity_low | Slot 1 - Sample 12 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample12_velocity_high | Slot 1 - Sample 12 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample13_velocity_low | Slot 1 - Sample 13 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample13_velocity_high | Slot 1 - Sample 13 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample14_velocity_low | Slot 1 - Sample 14 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample14_velocity_high | Slot 1 - Sample 14 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample15_velocity_low | Slot 1 - Sample 15 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample15_velocity_high | Slot 1 - Sample 15 - Velocity High | 1 .. 127 | 127 |  |
| slot01_sample16_velocity_low | Slot 1 - Sample 16 - Velocity Low | 1 .. 127 | 1 |  |
| slot01_sample16_velocity_high | Slot 1 - Sample 16 - Velocity High | 1 .. 127 | 127 |  |

## Later checks in REAPER

Compile later through the user's GitHub Actions. Confirm count/names and Slot independence with simultaneous Volume/LP envelopes on different Slots; test Grid/Value touch/write and playback readback with NVDA, without speech flooding. Save/reopen a session and load old slot/bank/Sample Set presets. Test crossed Start/End at 0 and 100, Sample Play Start on new notes, ten loops, all enum/boolean labels, synced LFO rate, variation modes and per-sample velocity ranges. Test Slice division with custom boundaries and all sequencer modes, preserving steps. Compare sound with automation disabled and CPU under sparse/dense multi-Slot automation. Existing HP integrator reset behaviour is retained; evaluate rapid HP envelopes for transients.
