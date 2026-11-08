# Stack 5150

A circuit-level model of a 120 W high-gain tube head (VST3 and standalone), built with JUCE.

## How it works

The plugin models the amplifier stage by stage, using its real component values:

- Rhythm and lead preamp paths, switched the way the amp's relays switch them
- Cascaded 12AX7 gain stages with grid current and bias shift, solved sample by sample
- Passive Low, Mid and High tone stack, solved as a circuit
- Long-tail-pair phase inverter with loop feedback, Resonance and Presence
- Four 6L6GC power tubes in push-pull

The amp runs at 4x oversampling. It processes a mono signal and sends its output to both channels.

The amp front is rendered procedurally, with no image files.

## Controls

| Control | Purpose |
|---|---|
| Rhythm Pre, Lead Pre | Preamp gain for each channel |
| Low, Mid, High | Tone stack |
| Rhythm Post, Lead Post | Output level for each channel |
| Resonance, Presence | Low and high response of the power amp |
| Channel | Selects rhythm or lead |
| Crunch | Adds gain to the rhythm channel |
| Standby, Power | Both must be on for the amp to make sound |
| Cabinet | Off, built-in cabinet filter, or a loaded impulse response |
| Load IR | Loads a WAV, AIFF or FLAC impulse response; files can also be dropped onto the amp |
| Input Cal | Sets how hot the DAW signal looks to the circuit; 0 dB means 0 dBFS = 1 V peak |

Presets are saved in `Documents/Stack 5150 Presets` and include every control and the impulse response's file path.

## Building

Requires CMake 3.22 or newer, Git and a C++17 compiler. JUCE is downloaded automatically during configuration.

```
cmake -S . -B build
cmake --build build --config Release --target Stack5150_VST3 Stack5150_Standalone
```

The plugin is written to `build/Stack5150_artefacts/Release/VST3`.

To run the checks:

```
cmake --build build --config Release --target ModelTest IrTest PresetTest
build/Release/ModelTest
build/IrTest_artefacts/Release/IrTest
build/PresetTest_artefacts/Release/PresetTest
```
