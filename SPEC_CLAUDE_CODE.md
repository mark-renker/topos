# TOPOS — Modular Filter Matrix · Claude Code handoff specification

**Purpose of this document.** A working, validated Linux VST3 build and a
34-check DSP validation suite already exist in this repository. What could not be
done off a Mac is the AU build, `pluginval`, and any real-host test. This
specification hands that off, and specifies the full target product — a
Filterverse-style filter matrix that scales to many filter types — as a
validation-gated loop.

**Run this prompt from the repository root.** It assumes macOS with Xcode command
line tools, CMake ≥ 3.22, Ninja, and Python 3 with NumPy.

> Note on convention: this is written in the validation-gauntlet style — numbered
> gates, each with a machine-checkable pass condition, and an explicit instruction
> to loop until every gate is green rather than to report progress and stop. If
> your own Gauntlet Loop template differs in wording, the gates below are the part
> that matters and they transplant cleanly.

---

## 0 · The prompt

```
You are working in the TOPOS repository, a JUCE audio plugin with a JUCE-free
header-only DSP core. Read README.md, dsp/ToposCore.h (especially the EXPANSION
CONTRACT comment), dsp/EngineRegistry.h, dsp/ToposRack.h, and
PLUGIN_BUILD_REPORT.md before writing any code.

Your objective is to take TOPOS from "validated Linux VST3" to "AU + VST3 that
passes pluginval strictly on macOS, with eight filter engines, presets, and a
release build", without breaking the expansion contract or the existing 34-check
suite.

Work as a gauntlet loop:

  1. Run the ENTIRE gate list in section 2, in order, and record each result.
  2. Pick the LOWEST-numbered failing gate. Fix only that.
  3. Re-run the ENTIRE gate list from gate 1. Earlier gates regress easily —
     particularly the passivity and unity-gain gates, which are load-bearing.
  4. Repeat until every gate reports PASS. Do not stop at the first green run of
     a single gate, and do not report success until a single uninterrupted pass
     over the full list is clean.

Hard rules:
  - Never widen a gate's tolerance, skip a gate, or delete a test to make it
    pass. If you believe a gate is wrong, stop and explain why in
    VALIDATION_LOG.md rather than editing it.
  - Never reorder or rename entries in EngineRegistry.h. Append only. Existing
    engine indices and string ids are referenced by saved host sessions.
  - Never add per-engine plugin parameters. The generic slotN_pM scheme is what
    makes the plugin expandable; adding an engine must add zero parameter IDs.
  - No allocation, locking, file I/O, or logging inside processBlock.
  - After each gate run, append a dated entry to VALIDATION_LOG.md with the
    command, the result, and the numbers. This file is the deliverable that
    proves the work, so it must be honest, including about failures.
  - If a gate cannot be satisfied, say so plainly and state what would be needed.
    Do not describe an unverified result as verified.
```

---

## 1 · What already exists

Do not rebuild these. Verify them, then build on them.

| Area | State |
|---|---|
| `faust/` | 6 prototypes, all compile under Faust 2.81.10, plus an offline harness (`harness.cpp`, score-driven) |
| `faust/scatter_modal_scatN.dsp` | the literal `fi.scatN` junction, kept as a documented counter-example: its plain-L2 gain reaches 5.474 in the star limit |
| `faust/scatter_modal.dsp` | the shipping form — normalised power waves, Householder junction, ‖S‖ = 1 exactly |
| `dsp/ToposCore.h` | `ParamSpec`, `EngineDescriptor`, `FilterEngine`, `OnePole`, `AsymSmooth`, `EnvFollower`, `DCBlock`, `LP1`, `SVF` (TPT), `FracDelay`, `fastTanh`; `kMaxEngineParams = 8`, `kNumSlots = 6`, `kNumLanes = 3` |
| `dsp/EngineRegistry.h` | append-only table: index 0 = `none`, then `scatter`, `route`, `disperse`, `vowel`, `thermal` |
| `dsp/engines/` | five engine headers |
| `dsp/ToposRack.h` | 6 slots, 3 lanes, in-lane ordering, 1x/2x/4x halfband oversampling, per-slot drive/mix/level/pan, per-lane level/mute, global trim and dry/wet; all engines pre-allocated per slot at `prepare()` |
| `tests/topos_cli.cpp` | 34-check offline suite (`--selftest`) plus 8 automated demo renders (`--render`) |
| `tools/` | `junction_passivity_proof.py`, `topology_ab_test.py`, `render_core_demos.py`, `render_faust_demos.py`, `audio_tools.py` |
| `Source/` | JUCE 8 wrapper: `ParameterLayout.h` (106 generic parameters), `PluginProcessor`, `PluginEditor` (descriptor-driven labels, resizable, scrolling) |
| `build_plugin/` | Linux VST3 built and smoke-tested |
| `audio/` | 8 rack demos + 5 Faust demos, 48 kHz 24-bit stereo |

Known gaps, all real: AU unbuilt, `pluginval` never run, no DAW test, no
listening test, no presets, no parameter smoothing in the wrapper, no modulation
sources, functional-but-plain GUI.

---

## 2 · The gauntlet

Each gate lists the command and the pass condition. A gate with no numeric
condition passes only on a clean exit code.

### Gate 1 — Core suite

```bash
g++ -O2 -std=c++17 -Wall -Wextra tests/topos_cli.cpp -o build/topos_cli
./build/topos_cli --selftest
```

**Pass:** `ALL CORE TESTS PASSED (0 failures)`, and the count of `PASS` lines is
≥ 34 and never decreases across the project. Compiles with no warnings from
`dsp/` or `tests/`.

### Gate 2 — Junction passivity

```bash
python3 tools/junction_passivity_proof.py
```

**Pass:** `PASSIVITY_PROOF_OK`; normalised-form worst-case gain ≤ 1 + 1e-9 for
every coupling value; `‖S Sᵀ − I‖∞ < 1e-12`.

This gate exists because the naïve implementation of this filter is expansive and
the failure is quiet — it only shows up as a runaway once the topology is
modulated. Any new engine that contains a feedback network must come with an
equivalent proof or an equivalent test.

### Gate 3 — Topology is structural, not tonal

```bash
python3 tools/render_faust_demos.py    # builds the harness the A/B test needs
python3 tools/topology_ab_test.py
```

**Pass:** `TOPOLOGY_AB_OK`; at most 4 of 8 star partials survive in the mesh
within 1.5% (currently 0 of 8); the lossless junction never rings shorter than
the lossy one.

### Gate 4 — Faust prototypes compile

```bash
for f in faust/*.dsp; do faust -lang cpp "$f" -o /dev/null || echo "FAIL $f"; done
```

**Pass:** no `FAIL` lines. Install Faust with `brew install faust` if absent. If
Faust genuinely cannot be installed, mark this gate SKIPPED in
`VALIDATION_LOG.md` with the reason — do not mark it passed.

### Gate 5 — Demo renders

```bash
python3 tools/render_core_demos.py
```

**Pass:** `ALL_CORE_DEMOS_OK`; every demo reports `nonfinite=0`; every demo's
pre-normalisation peak is < 2.0. (Peaks above 2.0 mean the demo automation is
driving the rack into clipping and should be re-levelled, not tolerated.)

### Gate 6 — Universal binary build, both formats

Edit `CMakeLists.txt` to set `CMAKE_OSX_ARCHITECTURES` to `arm64;x86_64` and
`CMAKE_OSX_DEPLOYMENT_TARGET` to `10.15`, then:

```bash
cmake -S . -B build_mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_mac
```

**Pass:** both `TOPOS.vst3` and `TOPOS.component` exist under
`build_mac/TOPOS_artefacts/Release/`; `lipo -info` on each binary reports both
architectures; zero warnings originating in `Source/` or `dsp/`.

### Gate 7 — pluginval, strictness 10

```bash
# download pluginval for macOS from https://github.com/Tracktion/pluginval/releases
pluginval --strictness-level 10 --validate-in-process --skip-gui-tests \
  build_mac/TOPOS_artefacts/Release/VST3/TOPOS.vst3
pluginval --strictness-level 10 --validate-in-process --skip-gui-tests \
  build_mac/TOPOS_artefacts/Release/AU/TOPOS.component
```

**Pass:** `ALL TESTS PASSED` for both formats. Then re-run both **without**
`--skip-gui-tests` and require the same.

Strictness 10 exercises automated parameter thrashing, non-standard block sizes,
sample-rate changes mid-stream, and denormal handling. Expect real findings here;
they are the point of the gate. Fix them in `Source/` or `dsp/`, not by lowering
strictness.

### Gate 8 — AU registration and a real host

```bash
cp -R build_mac/TOPOS_artefacts/Release/AU/TOPOS.component ~/Library/Audio/Plug-Ins/Components/
cp -R build_mac/TOPOS_artefacts/Release/VST3/TOPOS.vst3 ~/Library/Audio/Plug-Ins/VST3/
killall -9 AudioComponentRegistrar 2>/dev/null; auval -a | grep -i topos
auval -v aufx Tpos Rdka
```

**Pass:** `auval` reports the component and `AU VALIDATION SUCCEEDED`. Then load
both formats in a real host, and in `VALIDATION_LOG.md` record: the host and
version, that automation of `slot0_p0` works, that the state survives a
save/reload of the session, that switching a slot's engine while audio runs does
not click or drop out, and that the CPU cost at 4x oversampling with six slots
loaded is bearable at 128-sample blocks.

Report host findings honestly. "Loaded and did not crash" is a valid, useful
result; do not inflate it.

### Gate 9 — Two new engines

Add exactly two engines, following the expansion contract. Suggested, in
descending order of how well they fit the set:

- **`shift` — frequency-shifting comb.** A Hilbert/quadrature single-sideband
  shifter inside a comb feedback loop. The shift breaks harmonic re-entry, so the
  loop never lands on a fixed pitch and instead produces inharmonic, endlessly
  climbing or falling resonances. Passivity requires the loop gain below unity
  after the shifter, which is not the same bound as for an ordinary comb — prove
  it or test it.
- **`grain` — spectral-freeze resonator.** A short STFT with per-bin magnitude
  hold and a bin-wise decay, giving a filter whose resonances are *learned* from
  the input and then held. This one introduces latency; report it via
  `setLatencySamples()` and add a gate-1 test that latency is reported correctly.

Either choice must come with: a Faust prototype in `faust/`, a header in
`dsp/engines/`, an appended registry entry, at least three new tests in
`tests/topos_cli.cpp` (stability at defaults, bounded at parameter extremes,
stable under 6 kHz parameter modulation), a demo added to `demos()`, and a README
section stating what is measured about it.

**Pass:** gate 1 count is ≥ 43; `slot{n}` parameter count is unchanged at 106
total plugin parameters; the editor labels the new engines correctly with no
code changes in `PluginEditor.cpp` other than none at all.

That last clause is the real test of the architecture. If adding an engine
requires touching the editor or the parameter layout, the expansion contract has
been broken and the fix belongs in the contract, not in a special case.

### Gate 10 — Presets

Add at least twelve factory presets exercised across the routing space: at least
three series chains, three parallel-lane blends, three hybrids, and three
single-engine showcases. Store them as embedded APVTS XML with a version tag, and
add a test that every factory preset loads, produces finite output, and stays
below a peak of 4.0 on a −6 dBFS full-band input.

**Pass:** the preset test is in gate 1 and green.

### Gate 11 — Release hygiene

**Pass:** all of —
- `VALIDATION_LOG.md` shows one uninterrupted clean pass over gates 1–10.
- README's validation table is updated to match reality, with AU and `pluginval`
  moved out of the "not claimable" section only if they genuinely passed.
- Version number set in `CMakeLists.txt`, and the plugin's reported version
  matches.
- `git status` is clean apart from intentional build directories, and
  `build_plugin/`, `build_mac/`, `build_faust/`, `build/` are gitignored.
- Code signing: state in the log whether the AU and VST3 are signed and
  notarised, or explicitly that they are not and therefore need a Gatekeeper
  override on other machines.

---

## 3 · Target product, beyond the gauntlet

The gauntlet gets to a solid eight-engine release. The product this is aimed at is
larger, so keep these in view while making structural decisions.

**A filter matrix, not a filter.** The reference point is Polyverse Filterverse:
many filter characters, freely combined. TOPOS's differentiator should be the
character of the engines — physically motivated structures that do things a
biquad cannot: topology change, hysteresis, thermal memory, envelope-dependent
dispersion — rather than a large count of ordinary filter types.

**Scaling the slot count.** Six slots and three lanes are constants in
`ToposCore.h`. Raising them is mechanical for the DSP but multiplies plugin
parameters (currently 16 per slot). Before raising, decide whether to move to a
dynamic node graph with a serialised topology instead of fixed slots. If so, the
generic-parameter trick still applies per node; what changes is that the graph
becomes state rather than parameters, and automation reaches only the nodes that
exist. Do not do this inside the gauntlet.

**Modulation.** Per-slot parameters should eventually accept modulation from LFOs,
envelope followers, MIDI, and a step sequencer. The `slot{n}_pM` normalised
representation makes this straightforward: modulate in normalised space, then map
through the descriptor. Keep the mapping in one place so it stays consistent.

**Things worth adding as engines later**, all from the same "structures a biquad
cannot express" family: a waveguide mesh with adjustable node count; a coupled
Duffing-oscillator bank; a filter whose coefficients come from a small learned
model conditioned on the input's spectral envelope; an FDN whose feedback matrix
is a controllable rotation rather than a fixed Hadamard; a transmission-line
model with frequency-dependent characteristic impedance; a filter with
per-partial phase-locked decay for controllable inharmonic sustain.

**Non-goals for now.** MIDI-note-tracked pitch, a spectrum analyser display,
skinning, and any cloud or licensing infrastructure. They will pull effort away
from the engines, which are the reason for the plugin.

---

## 4 · Facts a fresh agent will need

- Plugin name `TOPOS`, product `TOPOS — Modular Filter Matrix`, vendor
  `Renky Denk Audio`, manufacturer code `Rdka`, plugin code `Tpos`, effect
  (`IS_SYNTH FALSE`), no MIDI input.
- JUCE 8.0.4, expected at `../build_deps/JUCE` via `add_subdirectory`.
  `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`. C++17.
- 106 plugin parameters: 4 global (`in_db`, `out_db`, `dry_wet`, `oversample`),
  6 lane (`lane{l}_level_db`, `lane{l}_mute`), 96 slot (6 × 16:
  `slot{n}_engine`, `_bypass`, `_lane`, `_pos`, `_drive_db`, `_mix`, `_level_db`,
  `_pan`, `_p0`…`_p7`).
- Normalised-to-real mapping: `value = min + (max−min)·pow(norm, skew)`, rounded
  if `ParamSpec::stepped`. This lives in the processor and must stay the single
  source of truth.
- `ToposRack::prepare()` prepares engines at 1x and sets `preparedOsRate = 1`,
  matching `RackState::oversample`'s default. Buffers are sized for 4x so that
  switching never allocates on the audio thread, but `setOversampleRate()` itself
  re-prepares and therefore must not be called from `processBlock`.
- A lane joins the parallel sum only if it contains at least one non-empty,
  non-bypassed slot. Removing this rule silently adds about +9.5 dB to a neutral
  rack. There is a gate-1 test for it.
- Faust harness score grammar: `sr <n>`, `dur <seconds>`,
  `set <address-substring> <value>`, `ramp <t0> <t1> <address-substring> <v0> <v1>`.
- Faust gotchas already paid for: the harness needs
  `#include "faust/gui/meta.h"`; `MapUI` exposes no `getParamMin`/`getParamMax`;
  a unary minus directly before a function call fails, so write `0.0 - sin(x)`;
  `letrec` bodies cannot reference letrec-bound signals from an enclosing `with{}`
  block.

## 5 · References

- Faust `filters.lib`, scattering junctions and allpass structures —
  [faustlibraries.grame.fr/libs/filters/](https://faustlibraries.grame.fr/libs/filters/)
- Julius O. Smith III, *Physical Audio Signal Processing*, Kelly-Lochbaum
  junctions and normalised wave variables —
  [ccrma.stanford.edu/~jos/pasp/](https://ccrma.stanford.edu/~jos/pasp/)
- `pluginval` — [github.com/Tracktion/pluginval](https://github.com/Tracktion/pluginval)
- JUCE `AudioProcessorValueTreeState` —
  [docs.juce.com](https://docs.juce.com/master/classAudioProcessorValueTreeState.html)
- Apple `auval` usage — [developer.apple.com Audio Unit validation](https://developer.apple.com/library/archive/technotes/tn2247/_index.html)
