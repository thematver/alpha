# alpha: DSP research and model review

The design calls for a common grammar: amount is **how much**, four character controls are **how**, the focus band is **where**, and motion is **when**. The implementation uses published mechanisms and original code. Names such as Cassette, Plate and Tube describe a model class; they do not assert a measured match to a particular commercial machine. No proprietary ShaperBox algorithms or GPL implementation code are copied.

## Core engines and their modes

| Engine / mode | Mechanism implemented and research basis | Audible property to verify |
| --- | --- | --- |
| TAPE / Cassette | Biased-effective magnetic hysteresis, record/playback bandwidth loss, resonant head bump, soft compression, seeded stochastic dropouts. [Chowdhury 2019](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf) motivates the separation of magnetic memory, bias and head losses. | Input-dependent harmonics and compression, tape memory, high-frequency loss and low-frequency bump; no free noise on silence. |
| TAPE / Reel-to-reel | The same magnetic mechanism with a brighter transport/head-loss profile and different low-frequency balance. | More open high frequencies than cassette at matched controls and RMS. |
| TAPE / VHS | A narrower, more compressed linear/longitudinal-track VHS audio profile. | Distinct bandwidth and dynamics; it does not model VHS Hi-Fi FM audio, companding or the helical-scan RF path. |
| DRIFT / Wow + flutter | Cubic fractional delay combines slow cyclic drift, faster irregular movement and stereo spread; a smooth 1–11 ms excursion bound avoids a hard causal read-head clamp at extreme settings. [Smith's fractional-delay treatment](https://www.dsprelated.com/freebooks/pasp/Fractional_Delay_Filtering.html) and [Lagrange interpolation](https://www.dsprelated.com/freebooks/pasp/Lagrange_Interpolation.html). | Continuous pitch motion without zipper steps; dry/out-of-band sound stays stable. |
| DRIFT / Wow | The slow component dominates. | Slow excursions around a steady tone, with rate and stereo spread controlling the intended components. |
| DRIFT / Flutter | The fast component dominates. | Fast pitch sidebands rather than simply amplitude modulation. |
| DUST / Vinyl | Poisson crackle with RIAA-shaped synthesized surface impulses, coloured noise and rumble. [Cycling '74's noise specification](https://docs.cycling74.com/legacy/max5/refpages/msp-ref/pink~.html) distinguishes spectral colour from white noise. The click event distribution and transport mixture are original models. | Sparse clicks and rumble, controllable event density, spectral tilt, envelope following and independent stereo texture. |
| DUST / Hiss | A filtered noise process with spectral colour. | Continuous high-frequency texture distinct from click events, gated by the configured input follower. |
| DUST / Mechanical | Coloured friction/clatter and 50/100/150 Hz transport harmonics. | Rumble and mechanical harmonics, distinct from a renamed white-noise generator. |
| HEAT / Soft clip | Rational soft clipping with antiderivative antialiasing (ADAA) and a 4× internal nonlinear path. [Parker, Zavalishin and Le Bivic 2016](https://www.dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf). | Soft onset, predominantly odd harmonics when symmetric, reduced folded energy compared with naive clipping. |
| HEAT / Tube | A biased smooth transfer, DC removal and envelope-dependent sag (up to 9 dB, with 2 ms attack / 80 ms recovery). | Even/odd harmonic balance follows bias; transient drive recovers over time. It is a tube-coloured waveshaper, not a circuit model of a named valve amplifier. |
| HEAT / Diode | A clean-room implicit Shockley/series-resistor transfer and antiderivative treatment, without junction capacitance. [Yeh, Abel and Smith 2007](https://www.dafx.de/paper-archive/2007/Papers/p197.pdf); [related circuit approximation](https://www.dafx.de/paper-archive/2007/Papers/p189.pdf). | Asymmetric exponential knee rather than a tanh algorithm renamed diode. |
| HEAT / Fold | Antialiased periodic folding. | Additional folds change harmonic structure with drive; it remains a creative folding model. |
| HEAT / Fuzz | Asymmetric hard clipping and a tail gate, smoothed through mode changes. | Abrupt, dense harmonics and gated low-level tails. It is not a coupled-BJT Fuzz Face circuit. |
| HEAT / Tape | A separate magnetic-memory stage, not the Tube transfer under another label. | Hysteresis and dynamic memory, with independent state from the TAPE module. |
| GRIT / Crush | Sample-clock reduction and quantization with independent jitter and reconstruction smoothing. | Quantization steps and intentional sampling images; zero amount is exact dry. |
| GRIT / Decimate | Sample-and-hold clock reduction with a finer word length. | Clock imaging and staircase timing dominate over bit truncation. |
| GRIT / Digital | Continuous μ=255 companding, variable quantization and expansion. [ITU-T G.711](https://www.itu.int/rec/T-REC-G.711) supplies the companding reference. | Level-dependent quantization; this is a variable-word-length creative compander, not a bit-exact telephony codec or MDCT codec. |
| SPACE / Room | Stable orthogonal feedback delay network (FDN), early diffusion and damped feedback. [Rocchesso and Smith](https://www.di.univr.it/documenti/ArticoloRivista/allegato/allegato574545.pdf). | Dense, stable late decay; no geometrically measured room is claimed. |
| SPACE / Plate | A distinct Dattorro lattice/tank topology: four input diffusers, two cross-coupled modulated tanks, damping and published stereo output taps. [Dattorro, Effect Design, Part 1](https://freeverb3-vst.sourceforge.io/doc/EffectDesignPart1.pdf). | Rapid diffusion and a smooth, modulated tail distinct from the Room FDN. A digital plate-class topology is not a measured EMT plate or a physical modal plate simulation. |
| SPACE / Bloom | Extended, diffuse and swelling FDN behaviour. | Longer evolving ambience. No shimmer pitch-feedback path is claimed unless that path is actually implemented and measured. |

## Added shapers

| Engine / modes | Implementation and primary references | Tests and limits |
| --- | --- | --- |
| PITCH / Chromatic, Smooth, Octave | Complementary windowed variable-delay heads, four-point interpolation and conservative steady-tone splice alignment. Ratio is `2^(semitones/12)`; the three modes respectively quantize to semitones, keep continuous tuning, or quantize octaves. [Puckette's derivation](https://msp.ucsd.edu/~msp/techniques/latest/book-html/node115.html), [worked example](https://msp.ucsd.edu/techniques/v0.11/book-html/node125.html), [ADI audio tutorial](https://www.analog.com/media/en/technical-documentation/application-notes/21065L_Audio_Tutorial.pdf). | Test positive/negative and fractional shifts, stereo coherence, duration, above-band rejection and changing grain windows. A granular shifter can smear transients and modulate complex material; this implementation does not preserve vocal formants. |
| TIME / Slip, Reverse, Repeat | Four-second read/slice windows in preallocated stereo history, with moving read heads, separate read configurations, complementary splice windows and complete-bank crossfades for long jumps. [Smith on large delay changes](https://www.dsprelated.com/freebooks/pasp/Large_Delay_Changes.html). | Test neutral identity, silent stop, negative read runs, repeated impulses, deterministic block boundaries and automation steps. Length is expressed in motion-cycle multiples and capped at four seconds. |
| VOLUME / Pump, Tremolo, Gate | A VCA multiplying the input by respectively power, raised-cosine/power and logistic envelopes; floor and separate edge slews. [Puckette, chapter 4, Envelope Generators](https://msp.ucsd.edu/techniques/v0.07/book.pdf). | Test complete closure, Curve in every mode and attack/recovery semantics. It is a modulation VCA, not an external-sidechain compressor. Modulation creates sidebands; edge smoothing reduces high-frequency products. |
| FILTER / Lowpass, Bandpass, Highpass | Two trapezoidal integrator states following [Simper's optimized SVF](https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf), with `g=tan(pi*fc/fs)` and `k=1/Q`; optional continuous ADAA pre-drive. | Test frequency rejection, BP centre, resonance decay, Drive continuity and mode crossfades. This is a TPT SVF with pre-drive, not a nonlinear transistor ladder. LP/HP resonance can increase peak level; BP is peak-normalized. |
| PAN / Balance, Auto pan, Rotate | Independent stereo attenuation; normalized sine/cosine constant-power gains; an orthogonal two-channel matrix. The actual LFO history supplies phase-shifted custom shapes. [CMU pan-law derivation](https://www.cs.cmu.edu/~music/icm-online/readings/panlaws/). | Test endpoints, centre, phase, wave shape and matrix energy. Auto pan uses up to +3 dB at an end to retain unity stereo centre. Vector energy and perceived loudness are different quantities; rotation does not preserve the mono sum. |
| WIDTH / Mid-side, Haas, Mono | Mid/side scaling; an antisymmetric short-delay difference decorrelator; side removal. A continuously warmed side high-pass anchors bass. [Das 2024 on stereo decorrelation](https://www.dafx.de/paper-archive/search?author%5B%5D=Das%2C+O.&years%5B%5D=2024). | Test the mono-sum null, low-frequency side attenuation, automation and correlation. The Haas mode is deliberately mono-sum-preserving MS decorrelation, not a physical HRTF or plain one-channel delay. Extreme width can create negative correlation and extra headroom requirements. |

### Individual shaper mode decisions

All eighteen added modes were reviewed separately. [Cableguys' official ShaperBox manual](https://downloads.cableguys.com/Cableguys-ShaperBox-3-Manual.pdf) is a reference for the interaction vocabulary, not an implementation or a claim of compatibility.

| Mode | Concrete model decision | Verification |
| --- | --- | --- |
| PITCH / Chromatic | Round transpose to semitones, then add fine cents. | Positive and negative tuning accuracy. |
| PITCH / Smooth | Continuous semitone ratio. | Fractional shift and changing grain windows. |
| PITCH / Octave | Round transpose to octaves, then add fine cents. | −12 / 0 / +12 semitone tuning. |
| TIME / Slip | Moving delay read position at 0…2× speed, including silent stop. | Neutral identity, stop and automation. |
| TIME / Reverse | Read captured history backwards with complementary splice windows. | Sustained negative read direction and splice continuity. |
| TIME / Repeat | Re-anchor captured windows to unwrapped host cycles. | 1/8…4-cycle alignment, seeks and post-seek convergence. |
| VOLUME / Pump | Power curve ducks toward Floor; Attack closes the duck and Release restores level. | Closure, Curve and separate closing/recovery rates. |
| VOLUME / Tremolo | Raised-cosine envelope followed by the Curve power. | Periodic modulation distinct from Pump. |
| VOLUME / Gate | Logistic transfer; Curve changes transition steepness. | Open/closed levels and edge slews; no input-threshold detector. |
| FILTER / Lowpass | TPT state-variable `v2` output. | Passband, high-frequency rejection, Q and decay. |
| FILTER / Bandpass | Damped/normalized `k*v1` output. | Centre frequency and peak normalization. |
| FILTER / Highpass | `input-k*v1-v2` output. | Low-frequency rejection and resonance. |
| PAN / Balance | Attenuate the channel opposite Position without crossfeed. | Centre unity and endpoint attenuation. |
| PAN / Auto pan | Sine/cosine gains normalized to unity stereo centre. | Gain law, actual custom-waveform phase and fast-clock coverage. |
| PAN / Rotate | Orthogonal stereo matrix, ±45° rotation. | Sample-wise stereo energy and cross-channel signs. |
| WIDTH / Mid-side | Scale side while retaining mid. | Exact mono sum, width endpoints and bass attenuation. |
| WIDTH / Haas | Opposite-polarity delayed-mid differences added to side. | Mono-sum null, delay automation and correlation. |
| WIDTH / Mono | Remove side, blended by Amount. | Equal outputs at full Amount and unchanged mid. |

PAN phase uses causal history: a shifted custom shape needs up to one cycle after reset or seek. Until that history exists it uses the current waveform value. DUST is input-gated even at Follow zero; zero means steady texture while the gate is open. Increasing Follow ties its level more closely to the input envelope. WIDTH's delay-difference topology is original and simpler than Das's allpass/velvet-noise design.

## Review method

A physical source can span several modules. A cassette combines TAPE/Cassette's magnetic/head response, DRIFT/Wow+flutter's transport movement and a low DUST/Hiss level. Vinyl crackle belongs to a vinyl texture; it is not cassette hiss. Pocket Cassette therefore selects Hiss, while Sunday Tape Loop deliberately mixes Vinyl with tape as a creative lo-fi preset. Similarly, a plate's diffusion/decay comes from SPACE/Plate; additional PITCH feedback is not silently called part of that plate model.

1. Test each mode separately at 44.1, 48 and 96 kHz, in mono and stereo where relevant.
2. Test neutral and disabled paths, parameter units, deterministic reset, malformed state and legacy parameter identities.
3. Measure sine response, harmonics/aliasing, impulse diffusion/decay, DC, peak gain, stereo energy and automation discontinuities.
4. Render original musical material into level-matched A/B examples. Keep the dry reference and document compensation, rather than preferring an effect merely because it is louder.
5. Compare printed controls with the DSP mapping and check that frequency/motion gestures change the stated signal path.

`Tests/DSPTests.cpp`, `Tests/TimePitchTests.cpp`, `Tests/ShaperTests.cpp` and `Tests/ProcessorTests.cpp` are the executable evidence. The review does not turn a perceptual goal such as “ideal” into a guaranteed objective result. A measured device match requires calibrated hardware captures, operating levels and tape/formulation information that are not present in this project.
