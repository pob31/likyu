# Likyū

A dynamic EQ and transient designer plugin: eight parametric bands, each of which chooses
independently how its **gain** behaves (Static, Dynamic, Transient) and how its **frequency**
behaves (Fixed, Env, Track). VST3, AU and LV2 from one codebase.

- **Dynamic:** a band cuts or boosts as its key gets louder, or quieter. A de-esser, a de-boomer,
  or a band of the music that opens room while someone speaks.
- **Transient:** a band shapes attack and sustain without a threshold. Soften a snare's attack in
  its own band and leave the rest alone.
- **Env:** a band's frequency follows the key's level. An envelope filter.
- **Track:** a band follows one spectral peak inside a range you set. Several Track bands keep to
  different peaks and do not swap when the peaks trade levels.
- **Each band has its own detector**, with its own filter, keyed from the plugin's input or from
  a sidechain.
- **A band at 0 dB is an exact bypass**: it nulls against the dry signal.
- **Latency is zero by default** and constant: one optional lookahead for the whole instance,
  never changed by what the bands do.

The name is a play on *l'EQ*.

**Status:** design only, nothing built yet. The requirements and the phase plan are written; the
next step is Phase 0 of the plan, the repository scaffold.

- [PRD](docs/PRD%20—%20Likyū,%20Dynamic%20EQ%20and%20Transient%20Designer%20Plugin%20(VST3%20AU%20LV2).md):
  what the plugin does, how the DSP works, and how each feature is accepted.
- [Development plan, draft 0.1](docs/Devplan%20—%20Likyū%20(draft%200.1).md): the phases, what each
  delivers and what it needs.

## What it is not

No linear phase, no FFT in the audio path, no multi-resonance suppression: one tracked peak per
band. No slopes steeper than 12 dB per octave in one band; stack bands for more. No per-band
lookahead, no mid-side, no oversampling in the first version.

## Building

There is nothing to build yet. When there is: CMake 3.22 or later, a C++20 compiler, JUCE 9.0.5
as a submodule. The DSP is a library of its own, `lib/dsp`, with no JUCE dependency, so that it
can be tested headless and linked outside the plugin.

## Licence

GPLv3. Copyright (C) 2026 Pierre-Olivier Boulant. See [LICENSE](LICENSE).
