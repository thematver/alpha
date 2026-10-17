# alpha

A native character and motion effect for Logic Pro. Audio Unit (AUv2) and Standalone, Apple Silicon, macOS 12 or later. C++20 / JUCE 8.0.15.

## Play

Insert **Audio FX → Audio Units → Alpha Audio → alpha** on a mono or stereo track. The opening sound is **Sunday Tape Loop**, matching the supplied interface reference.

- The **core** bank contains DUST, DRIFT, HEAT, GRIT, SPACE and TAPE. The **shapers** bank contains PITCH, TIME, VOLUME, FILTER, PAN and WIDTH. All twelve engines can run together.
- Drag a tile vertically to change Amount; Shift gives fine adjustment. Its dot enables the engine. Selecting a tile exposes its model and four character controls with physical units. Double-click restores the captured preset value; right-click offers engine operations.
- The coloured spectrum regions are real frequency focus bands. Drag their edges or bodies to change where each engine processes audio. The analyser and meters show actual audio.
- **Motion** provides eight shapes, editable curves with up to 32 nodes, free rate or seven host-synced divisions, a bipolar depth and a target chosen from Amount or four character controls. Each engine can have an independent profile. Follow Input retriggers motion on detected transients. Host PPQ anchors synced curves and TIME slices.
- **A / B** selects the editable scene and moves Morph to its endpoint. All five scene fields per engine are independent. Models, focus, enable switches and motion profiles are shared. Copy and Swap are in settings; Swap also inverts Morph to preserve the interpolated sound. Morph remains automatable by the host.
- **MAIN** exposes Flux, Tone, Width and Output. Flux adds deterministic slow variation; Roll creates a variation while respecting engine locks. Character scales engine amounts, Mix blends dry/wet, Delta monitors processed minus dry, and Auto-gain applies bounded, linked RMS compensation.
- **Presets** has seventeen factory sounds, hover audition with state restoration, and Save/Open for complete `.alphapreset` files. Keep Amount, Mix and Output preserve those controls during browsing. Editor changes support undo/redo.

All **1445 parameters** are exposed to automation. The original 66 IDs and indices remain at the start of the list. Modulation operates over base values without writing automation into them. State stores curves, scenes, focus bands and routes; missing parameters in older states receive declared defaults. The upgraded DSP changes the sound of older presets.

Fixed processing order: **Tape → Drift → Dust → Heat → Grit → Pitch → Time → Volume → Filter → Pan → Width → Space**. [DSP research](Research/DSP-models.md) documents primary sources, individual decisions for all 39 modes and measured limits. Models include magnetic hysteresis, a distinct Dattorro plate tank, an orthogonal room FDN, granular pitch, crossfaded time heads and a TPT filter. Names describe mechanisms and tonal families; no measured match to a named hardware unit is asserted.

## Build and install

Requires Xcode and its macOS SDK, CMake and Git. The build script retrieves the pinned official JUCE release if absent.

```sh
bash scripts/build.sh
bash scripts/install.sh
```

Outputs: `build/Alpha_artefacts/Release/AU/alpha.component` and `build/Alpha_artefacts/Release/Standalone/alpha.app`. The installer signs ad hoc, backs up an earlier alpha, installs to the current user's Audio Units directory and runs Apple's strict AU validator. Save the Logic project before rescanning or restarting Logic. Standalone processing requires audio input routing.

## Verify and compare audio

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 6
ctest --test-dir build --output-on-failure
build/AlphaProcessorTests_artefacts/Release/AlphaProcessorTests build/qa/final
python3 Tests/generate-demo.py
build/AlphaAudioReview_artefacts/Release/AlphaAudioReview build/qa/alpha-demo.wav build/audio-review
```

Tests exercise DSP response, aliasing, neutral paths, mode/parameter changes, sample rates, stereo/mono invariants, host sync, state, presets and actual native component bindings. Audio review renders every mode and preset with the same original four-bar loop, two seconds of warm-up and four seconds of tail. It writes 24-bit linked RMS-matched comparison WAVs and raw-level/DC/correlation/continuity/timing metrics. Applied listening gain is recorded; a peak cap can prevent exact RMS matching. Build and listening artifacts stay out of Git.

`Tests/AUHostTests.mm` loads the freshly built bundle in its own process and renders through Apple's AudioToolbox API without installing it. This CTest requires access to the system AudioComponent service. With no bundle argument it tests the installed component instead. `auval -v aufx Alfa Alph -strict` validates an installed AU. The native UI bundles Archivo/Geist Mono and scales from 75% to 200%.

There is no sidechain input, MIDI learn or engine reordering. DUST is input-gated on digital silence. Granular Pitch does not preserve vocal formants. Widening can require extra headroom. Nonlinear models retain residual aliasing at extreme settings. Maximum Bloom RT60 is 26 seconds. Host tail is 35 seconds for normal character sounds and extends conservatively with active TIME feedback, up to about 558 seconds at maximum feedback, so long repeats are not cut at 35 seconds. JUCE licensing and notices are in `third_party/JUCE/LICENSE.md` and `third_party/JUCE/docs/`.
