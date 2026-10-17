# alpha

A native character effect for Logic Pro. Audio Unit (AUv2), Apple Silicon, macOS 12 or later. Built with C++20 and JUCE 8.0.15.

## Play

In Logic, insert **Audio FX → Audio Units → Alpha Audio → alpha** on a mono or stereo track.

- **Character** scales the strength of all six modules. **Mix** blends the processed signal with the original; **Output** adjusts the final gain.
- **Tape → Wobble → Noise → Drive → Grit → Space** is the processing order. Each module has an amount, two character controls, three models and an independent on/off switch.
- **A / B** selects a scene for editing and moves the morph to that endpoint. Scene amounts and the two detail parameters are independent. Use MAIN to access morph, shared motion and output. Copy and Swap are in the settings menu. Models, enable switches and motion settings are shared between scenes.
- **Morph** continuously interpolates scene values. Copy duplicates a scene; Swap exchanges scenes and inverts the morph position to preserve the current sound.
- **Motion** has a shared LFO with four shapes, free or tempo-synced rate, a master depth, and a bipolar amount route to each module. The LFO keeps running independently of scene morphing.
- **Presets** contains ten factory starting points. The settings menu and preset browser Save/Open buttons store complete user presets as `.alphapreset` files. Logic projects and Logic's AU presets also preserve the full parameter state.
- Input/output meters display actual audio. The spectrum shows actual output audio. Coloured shapes show the relative engine amounts; they are not frequency filters. Noise follows the incoming signal; it does not run as a free noise generator on a silent track.

All 66 parameters are exposed to host automation with stable IDs. Modulation is applied internally over base values and does not write automation to those base parameters.

## Build and install

Requires Xcode with its macOS SDK, CMake, and Git. The build script retrieves the pinned official JUCE release if it is absent.

```sh
bash scripts/build.sh
bash scripts/install.sh
```

The build produces `build/Alpha_artefacts/Release/AU/alpha.component` and a standalone application for UI checks. The installer signs the local build ad hoc, preserves an earlier installed alpha in `build/installed-backups/`, installs to the current user's Audio Units directory, and runs Apple's strict AU validator. No system Audio Units or other plug-ins are modified.

If Logic was already running during a new installation, open Plug-in Manager and rescan alpha, or restart Logic after saving your project. The standalone effect requires audio input routing; it is not a sound generator.

## Verification

`alpha-dsp-tests` covers processing safety and behavior. `AlphaProcessorTests` covers bus support, factory presets, automation IDs, state restore, scene operations, host time signatures and user presets. `Tests/AUHostTests.mm` renders the installed component independently through Apple's AudioToolbox API. `auval -v aufx Alfa Alph -strict` validates the installed AU interface. Run `Tests/generate-demo.py` with Python 3 to generate an original four-bar test loop.

The Patina interface uses six engine tiles: Dust (Noise), Drift (Wobble), Heat (Drive), Grit, Space and Tape. Select a tile to edit its tone, detail, amount and motion route. The native interface scales from 75% to 200%, with bundled Archivo and Geist Mono fonts. Roll varies unlocked engines; the lock button protects an engine. Editor changes support undo/redo. Bypass passes the dry input; Delta monitors processed minus dry; Auto-gain smoothly matches wet and dry RMS within a bounded gain range. Grit adds Crush, Decimate and Digital models with Bits and Rate controls. Existing alpha plug-in identity, original 54 automation IDs/indices, and older project/preset files are preserved.

The plug-in uses a fixed module order, one modulation source and mono/stereo layouts. It has no sidechain input or MIDI learn. Saturation is a character algorithm, not an emulation of a named hardware product. Reverb and modulation tails continue as part of the processed path. Wobble intentionally delays its processed path by about 6 ms; Space size changes glide the delay read heads. The maximum calculated Bloom decay is about 10.7 seconds. The reported host tail is 15 seconds.

JUCE is a third-party dependency; its license and notices are in `third_party/JUCE/LICENSE.md` and `third_party/JUCE/docs/`.
