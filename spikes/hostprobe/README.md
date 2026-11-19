# spikes/hostprobe/

Throwaway. Devplan Phase 1, the host bench.

**The question:** what does each host do with a sidechain bus, a latency change, 284 parameters, a
saved state, a layout beyond stereo and a touch, as the plugin sees it? The answers go in
[`docs/spikes/phase1-hosts.md`](../../docs/spikes/phase1-hosts.md), which also holds the procedure
for each row.

**What this is:** the real `LikyuProcessor` with an observer derived from it and an editor that
shows what the host does and exports it as Markdown. It is a separate plugin to a host, "Likyu
Probe" (plugin code `Lkyp`, LV2 URIs under `https://pixetbel.org/lv2/likyu-probe/`), so it sits
beside Likyu and no session holds the product's IDs through it. Same buses, same parameters, same
latency.

**Build and install:**

```text
Windows          spikes\hostprobe\install.ps1
macOS, Linux     bash spikes/hostprobe/install.sh
```

Both build the probe and the product in Release under `build/hostprobe/` and copy the bundles to
the per-user plugin folders; `-Uninstall` and `--uninstall` remove them. By hand:
`cmake -S spikes/hostprobe -B build/hostprobe`, then
`cmake --build build/hostprobe --config Release`.

`likyu_hostprobe_snapshot <absolute folder>`, a third target, runs the probe without a host and
writes its report and a picture of its editor there.

`auhost.c`, macOS only and built by hand, is a minimal AU host that renders the probe or the
product with chosen widths; its header says how (finding 9 of the report).

This project is not built by the root `CMakeLists.txt` and not by CI. It is deleted when the
report is complete; `LikyuProcessor` then becomes `final` again (`src/shims/juce/PluginProcessor.h`).
