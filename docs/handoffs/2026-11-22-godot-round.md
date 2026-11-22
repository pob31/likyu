# Handoff: Likyu in Go.dot, the first round

2026-11-22, after 0.1.0. The installers' handoff
([`2026-11-22-phase7-installers.md`](2026-11-22-phase7-installers.md)) still holds for the
release. This note covers what Go.dot does with Likyu, and the round that edited both
repositories. Figures: [`docs/bench/2026-11-22-godot.md`](../bench/2026-11-22-godot.md). Go.dot's
side: its namespace draft §142.

## Where it stands

- **Likyu runs in Go.dot's plugin child as a real plugin, measured.**
  - **Cost:** stereo and 8 channels, eight bands in Dynamic and Track, 2 % of a block or less.
  - **State:** loads in half a millisecond, a second voice untouched.
  - **Recall:** exact.
  - **Sidechain:** a key from the child is heard, cut by the band's range.
- **This repository:** two tests in `tests/plugin/ProcessorTests.cpp`, Go.dot's recall order and
  its `layKey` sequence; the silent-sidechain badge removed (`presentation::badgeOf` has no host
  argument now); the PRD's Go.dot rows amended; the devplan's Phase 8 record and Phase 9 note.
- **Go.dot:**
  - `plugin/ScratchBlock.h`: no buffer view per block, so nothing is allocated at 32 channels or
    more;
  - `WFG_ENGINE_FOLDER`, `WFG_REAL_STATE`, `WFG_M86_CHANNELS` and `WFG_M35_PARAM` for its
    real-plugin cases;
  - a keyed real-plugin case.
- **Open, the author's (Go.dot §142, BHZ):** a VST3 wider than 8 channels is laid out stereo by
  Go.dot's ladder, and its cue passes dry.

## Things that will bite

- **A hand-made state for Go.dot must be the host's bytes**, not Likyu's JSON. A JUCE host keeps
  a VST3's state as `copyXmlToBinary` of `<VST3PluginState><IComponent>` in JUCE's base64.
  Anything else is refused without a word, Go.dot reports no problem, and the case measures an
  empty plugin. States a cue keeps are the editing helper's captures and are right.
- **Likyu's parameter 0 is Bypass.** A case that moves "the first parameter" to 0.2 leaves Likyu
  as it was, so no state changes and nothing is captured. M35 takes `WFG_M35_PARAM=1` (Output
  Gain).
- **An edit to a file MSBuild is compiling can be lost.** The build that is reading it finishes,
  its object is newer than the edit, and the next build skips the file. A "before" binary of this
  round drove 2 channels into a 34-channel lane that way: its 34-channel figures are not used.
  Edit, then build, never both at once.
- **Go.dot's real-plugin cases are skipped by default** (`--no-skip`), and they read the
  developer's own plugin list unless `WFG_ENGINE_FOLDER` is set. Use a scan of Likyu alone:
  `wfg plugins --engine-folder=<dir> --scan=vst3 --retry=<path>\Likyu.vst3`.
- **In Go.dot the editor is not on the instance that sounds.** The editing helper hosts a second
  instance, so Listen, the live line, the meters and the analyser there show a plugin fed one
  silent block a capture. That is Phase 8 design.

## Next

1. The author's answer to BHZ in Go.dot, then the widths past 8 measured again.
2. The same cases on the Mac farm (AU, whose discrete layouts reach 32 channels and more) and
   the NUC (LV2, mono and stereo).
3. The rest of Phase 8: a show with several instances recalled through a cue list, by ear; the
   surfaces; Listen through the editing helper; the beta's decision.
