# Audio review — 2026-10-17

The review covers **12 engines, 39 individual modes and 17 factory presets**. Primary sources and model decisions for every mode are in [DSP models](DSP-models.md). Raw musical-render measurements are in [audio-metrics.csv](audio-metrics.csv).

## Musical render procedure

`Tests/generate-demo.py` creates an original deterministic eight-second stereo loop at 48 kHz / 120 BPM with pitched keys, bass, kick, snare and hats. No external recordings or copyrighted sample libraries are used. `Tests/AudioReview.cpp` renders each mode independently at 70% Amount and every factory preset, using two seconds of input warm-up and four seconds of silent tail. PITCH is shifted by +7 semitones; TIME uses non-neutral speed; filter/pan/width have explicit non-neutral controls. The exact test settings are executable in the harness.

Every output is written as 24-bit WAV in `build/audio-review/`. A single linked stereo gain matches the eight-second body RMS to the dry loop; a full-file −1 dBFS sample-peak cap takes priority. The CSV contains the applied gain and the **raw** RMS, peak, finite-window mean, correlation and largest body-only adjacent-sample step, before that offline gain. No limiter hides the measured raw peaks. Body RMS matching is not an integrated-LUFS or perceptual loudness measurement.

**Result: 56 renders, 0 failed safety checks.** Largest raw sample peak:-3.10 dBFS. Largest absolute stereo-averaged finite-window mean:0.001061; this measurement includes the original loop's nonzero finite-window mean and does not by itself identify DC leakage. No file clipped before or after matching. Measured stereo correlation remained positive on this source; that does not prove arbitrary settings are mono-compatible.

Offline render time was5.3–13.3 % of audio duration in this run. This is a machine-specific wall-time observation including processing and instrumentation, not a DAW deadline guarantee. Maximum adjacent steps on music include ordinary transients and intentional distortion; dedicated continuous-tone automation tests assess switching clicks separately.

## Signal tests and implementation corrections

The complete core run records 819 assertions in [signal-metrics.txt](signal-metrics.txt). Selected measurements:

| Property | Result / test condition |
| --- | --- |
| Tape magnetic loop | Enclosed normalized major-loop area0.614929 |
| Tape head controls | Age HF difference−10.70 dB;95 Hz Bump difference+6.03 dB |
| Plate decay | Schroeder estimate0.97s versus4.72s at Decay25%/70% |
| Heat folded energy |24.2–33.5 dB lower than naive transfer in the5.84 kHz hard-drive test |
| Aggressive Fold residual |−18.1 dBc in that stress test; not alias-free |
| Dust spectral focus | LF rejection28.76 dB in the focused-noise test |
| Distant transport | Repeat RMS error below 7.2×10⁻⁹ at PPQ 120000064.375 versus 64.375, 123/999 BPM |

TIME's maximum feedback can remain audible well beyond35 seconds. Host tail metadata therefore adds a conservative feedback-decay estimate based on the whole preallocated history, plus downstream reverb/padding. Normal character sounds retain35 seconds; maximum active TIME feedback reports about558 seconds. The four-second musical tail files illustrate sound, not the complete worst-case feedback decay; dedicated long-tail tests cover that case. A 540-second 8 kHz render at maximum feedback remains bounded in all three TIME modes. Repeat RMS falls from−31.1 dBFS at35 seconds to−80.0 dBFS at 175 seconds and−209.3 dBFS at535 seconds in the stress fixture.

- Magnetic major-loop area verifies memory rather than a renamed static saturator. Tape Age and Bump have separate frequency-response tests; dropouts use seeded Poisson timing.
- Heat uses 4× internal static nonlinear processing with pre/post reconstruction filters and ADAA. Folded-harmonic energy is compared with a naive transfer at equal test conditions. Residual aliases remain under aggressive folding; the magnetic branch has its own normalized hysteresis approximation.
- Plate is tested with impulse diffusion, damping and measured Schroeder decay. It uses a separate cross-coupled Dattorro tank; Room/Bloom use an orthogonal FDN.
- Pitch tests −12, +7, +12 semitones, fractional tuning, mode quantization, stereo coherence, duration and above-band attenuation. Automation tests include changing pitch, grain and mode.
- Time tests sustained reverse reads, repeated impulses, silent stop, block-size invariance, host seeks, negative pre-roll and multi-cycle alignment. Long jumps crossfade read banks.
- Volume tests floor closure and distinct Pump/Tremolo/Gate curves. Filter tests LP/BP/HP response, resonance decay, pre-drive continuity and hot input. Pan tests actual shifted custom-waveform history, including 20 Hz at 8 kHz. Width tests mono-sum preservation and bass-side attenuation.
- Host transport tests cover stop/resume, tempo/meter changes, missing PPQ and preset transactions. State and AU tests cover all 1445 parameters and preservation of the original 66 automation positions and legacy three-choice drive normalization.

## Listening artifacts

The generated dry reference and mode/preset WAVs are available locally after running the documented command. Compare the same passage and match gain from the CSV when inspecting raw files. Suggested contrasts: Cassette/Reel/VHS; Wow/Flutter; Vinyl/Hiss/Mechanical; all six Heat transfers; Room/Plate/Bloom; all Grit modes; all three Pitch/Time modes; each VCA/filter/pan/width mode.

This is an implementation and signal review with reproducible listening files. No subjective human listening-panel verdict or calibrated hardware A/B is claimed. A device-specific authenticity claim needs recorded hardware responses, operating levels and tape/plate/transport identification. The implemented mechanisms and their limits are stated per mode instead.
