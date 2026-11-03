# TOPOS — Modular Filter Matrix

Renky Denk Audio. An expandable filter-matrix plugin: five unconventional filter
engines that can be placed in six slots, mixed across three parallel lanes, and
reordered freely inside each lane. Built as a JUCE VST3/AU wrapper around a
JUCE-free, header-only DSP core, with a validation suite that runs offline and an
appended-registry expansion path for the next twenty filter types.

Verified in this build: Faust 2.81.10 prototypes all compile; the DSP core passes
34 offline checks; the VST3 links and loads on Linux (JUCE 8.0.4). AU was **not**
built — see [Validation status](#validation-status).

---

## The scattering step that started this

The previous prototype's "topology morph" was frequency-template interpolation:
eight resonators whose ratios crossfaded between harmonic, plate, and inharmonic
templates. That is timbre morphing, not topology change. The requested next step
was a real coupled `fi.scatN` scattering network, and doing it properly turned up
something worth reporting.

The literal `fi.scatN` implementation is in `faust/scatter_modal_scatN.dsp`. It
compiles and it is physically correct: it realises the unnormalised
Kelly-Lochbaum parallel junction

```
p     = sum_j alpha_j * in_j          with sum_j alpha_j = 2
out_i = p - in_i
```

which is lossless in the **admittance-weighted** norm `sum_i Y_i |p_i|^2`. The
delay loops wrapped around it, however, are ordinary unit-gain-normalised
waveguides, so the norm that actually governs stability is plain L2 — and in
plain L2 this junction is expansive for every non-uniform coupling setting:

| coupling | topology | ‖unnormalised (fi.scatN)‖₂ | ‖normalised (Householder)‖₂ |
|---|---|---|---|
| 0.00 | star | **5.474** | 1.000000 |
| 0.25 | — | **4.206** | 1.000000 |
| 0.50 | — | **2.981** | 1.000000 |
| 0.90 | — | **1.299** | 1.000000 |
| 1.00 | mesh | 1.000 | 1.000000 |

Measured by `tools/junction_passivity_proof.py` (N = 8). The offline suite caught
the consequence directly: peaks of 65x at parameter extremes and an energy ratio
of 1.105 under 40 Hz topology modulation.

The fix is to work in **normalised (power) wave variables**. Scaling each wave by
`sqrt(Y_i)` turns the same physical junction into a Householder reflection:

```
u_i   = sqrt(Y_i / sum_j Y_j)        so ||u|| = 1
j     = sum_i u_i * in_i
out_i = 2 * loss * u_i * H(j) - in_i        =>  S = 2 u u' - I
```

`S` is orthogonal, so `‖S‖ = 1` **exactly**, for any admittance distribution, at
every sample. That is what makes the topology control safe to modulate at audio
rate rather than merely stable at rest. With a loss factor `L ≤ 1` the singular
values are `|2L − 1| ≤ 1` and `1`. `H` is a one-pole lowpass at unity DC gain,
which is positive real (its Nyquist locus is the circle through 0 and 1 in the
right half plane), so `|2H − 1| ≤ 1` and the junction becomes frequency dependent
— high partials couple across branches less than low ones, as at a real loaded
junction — without giving up passivity.

`‖S Sᵀ − I‖∞` measures 1.11e-16 for an arbitrary admittance spread.

The topology control is now genuinely structural. Same excitation, same modal
ratios, same decay and brightness, only the coupling changed
(`tools/topology_ab_test.py`):

```
star (coupling 0) top partials Hz: 146.2 194.5 242.8 291.0 338.8 386.5 438.8 731.0
mesh (coupling 1) top partials Hz: 107.5 208.8 260.8 365.8 478.0 553.8 648.2 692.8

star partials surviving in the mesh (within 1.5%): 0 of 8
spectral centroid: star 999 Hz -> mesh 1340 Hz
```

Zero of eight partials survive. A morphing resonator bank would have kept them
and only changed their levels. In the star limit `S = diag(+1, −1, …, −1)` — eight
independent loops, and the −1 reflections put their partials on half-integer
multiples of the branch frequency, which is exactly the 49 Hz spacing visible
above for f0 = 98 Hz. In the mesh limit every branch loads every other one and the
mode set is renegotiated.

Measured ring time, requested T60 = 3.0 s: 2.60 s at star, 2.46 s mid, 2.35 s at
mesh. Shorter than requested because the junction filter and branch damping add
loss the T60 formula does not account for; the mesh is shortest because coupling
spreads energy into the more heavily damped high branches.

---

## The five engines

Each engine is a header in `dsp/engines/`, a Faust prototype in `faust/`, and a
descriptor with up to eight parameters. The first three are the prior thread's
prototypes, rebuilt; the last two are the additions.

### SCATTER — coupled modal scattering network
Eight waveguide branches on a shared normalised N-port junction, as above.
Coupling re-wires star ↔ mesh; Ratio Morph moves the branch tuning through
harmonic → plate → golden-inharmonic; Junction LP sets how frequency-dependent
the coupling is. `Losslessness` is the junction loss factor, and it is bounded at
1.0 by construction.

### ROUTE — energy-routed multimode filter
Parallel LP/BP/HP taps off one TPT state-variable pole pair, blended by a
**unit-norm** routing vector derived from two spherical angles rather than by
linear crossfade, so the perceived level does not bulge in the middle of a morph.
Measured RMS spread across a full LP→BP→HP morph: 1.03x. A second, orthogonal
routing vector drives the opposite channel, which produces stereo width out of
the same pole pair instead of out of a delay.

### DISPERSE — envelope-dependent nonlinear dispersion
A twelve-section allpass lattice where each section is a 2×2 **rotation**
(`out = −s·v + c·z`, `z = c·v + s·z`, `c = √(1−s²)`), whose transfer function is
exactly `(z⁻¹ − s)/(1 − s·z⁻¹)`. Being orthogonal, it is norm-preserving under
arbitrary coefficient modulation — a naïvely modulated direct-form allpass
parametrically amplifies instead. Coefficients follow the input envelope, so
transients and decay tails get different group-delay curves and the material seems
to change with how hard you hit it. Opposite coefficient polarity per channel
gives stereo divergence with no chorusing. Verified allpass: LTI energy ratio
1.0000.

### VOWEL — hysteretic formant filter
Four formants whose centre frequencies slew at **direction-dependent** rates, so
opening a vowel does not retrace the path that closing it took. Real articulators
behave this way — tongue and jaw have asymmetric mass and muscle response — and
the asymmetry is what stops a vowel sweep from sounding like an LFO on a bandpass.
Plus a growl drag term and a nasal anti-formant. Measured difference between the
opening and closing paths through the same vowel sequence: 137%.

### THERMAL — electrothermal ladder
A four-stage nonlinear ladder in which the exponential converter's scale factor
drifts with a modelled junction temperature, so cutoff and resonance move while
the filter is driven hard and recover slowly afterwards; the supply rail sags
under drive with a slow recovery, making the distortion character depend on level
*history* rather than instantaneous level; and per-stage device mismatch breaks
the ladder's ideal odd symmetry. Measured: the same input yields 8.9% different
RMS cold versus warm.

The mismatch is applied as `tanh(y + off·y·|y|)`, not as a DC offset. A DC offset
is arguably more faithful to the circuit but it makes the plugin thump and settle
every time the host resumes processing — the offline suite caught exactly that as
a 2.33e-3 idle output. The level-scaled form keeps the asymmetry and its even
harmonics while going exactly to zero at silence.

---

## Architecture: how it stays expandable

```
Source/            JUCE wrapper   (PluginProcessor, PluginEditor, ParameterLayout)
dsp/               DSP core       (JUCE-free, header-only, no allocation in process)
  ToposCore.h        ParamSpec, EngineDescriptor, FilterEngine, DSP building blocks
  EngineRegistry.h   append-only engine table
  ToposRack.h        6 slots, 3 lanes, ordering, oversampling, dry/wet
  engines/*.h        one header per engine
faust/             prototypes + offline harness
tests/topos_cli.cpp offline validation suite and demo renderer
tools/*.py         renderers, passivity proof, A/B measurement
audio/             rendered examples
```

**Routing.** Each of the six slots picks an engine, a lane (0–2), and a position
within that lane. Slots in a lane run in series in position order; the three lanes
run in parallel and are summed with per-lane level and mute; each slot has its own
drive, wet/dry mix, level and pan; the whole rack has input trim, output trim,
global dry/wet, and 1x/2x/4x oversampling. Series, parallel, and hybrid graphs are
all reachable, and order is changeable while audio runs (demo 06 swaps THERMAL in
front of SCATTER mid-render).

A lane only joins the parallel sum if it holds at least one non-empty,
non-bypassed slot. Without that rule, empty lanes each pass the input through and
three of them add about +9.5 dB to a rack that should be neutral. The suite now
pins this: a rack with all slots empty, and a rack with all slots loaded but
bypassed, both reproduce the input with 0.00e+00 deviation.

**Adding an engine** — the expansion contract, stated in `ToposCore.h`:

1. Add `dsp/engines/YourEngine.h` implementing `FilterEngine`, with a descriptor
   carrying a permanently stable string `id` and up to `kMaxEngineParams` (8)
   `ParamSpec`s.
2. **Append** it to the table in `EngineRegistry.h` and bump `count()`. Never
   reorder or rename existing entries — index and `id` are what saved sessions
   reference.
3. Done. No plugin-side change is needed.

That last point is the design's whole payoff. The plugin's parameters are
**generic**: `slot{n}_p0` … `slot{n}_p7` are plain normalised 0–1 floats, and the
processor maps them onto the currently selected engine's `ParamSpec` range,
honouring skew and stepping. The editor reads the descriptor at runtime for
labels, units and ranges. So the parameter ID set — 106 parameters — is fixed no
matter how many engines get added, host automation lanes never shift, and old
sessions keep working. The screenshot evidence: the same slider row reads
`Decay 16.254 s` under SCATTER and `Poles 2 x2` under ROUTE.

The rack pre-allocates every engine for every slot at `prepare()`, so changing
engines mid-session never allocates on the audio thread. Oversampling changes do
allocate, so the processor applies them from a message-thread timer inside
`suspendProcessing()`.

---

## Validation status

**Passing, run in this sandbox.**

| Gate | Result |
|---|---|
| Faust prototypes compile (`faust -lang cpp`) | 6 / 6 |
| Faust offline renders stable, non-finite = 0 | 5 / 5 |
| DSP core offline suite | **34 / 34** |
| Passivity proof | `PASSIVITY_PROOF_OK` |
| Topology A/B | `TOPOLOGY_AB_OK` |
| Rack demo renders | 8 / 8, non-finite = 0 |
| VST3 configure + build (JUCE 8.0.4, VST3 SDK 3.7.12) | success, 0 warnings from `Source/` |
| Plugin smoke: all 6 engines × 6 slots, random params, finite out | pass |
| Plugin smoke: 4x oversample switch, mono layout, APVTS round-trip | pass |
| Editor renders headless at 760×480 / 1080×800 / 1600×1000 | no clipping or overlap |

The suite covers registry integrity, per-engine stability and DC content,
parameter extremes, 6 kHz parameter modulation, SCATTER passivity under 40 Hz
topology modulation, DISPERSE allpass energy, VOWEL hysteresis, THERMAL thermal
memory, ROUTE morph level flatness, rack routing/reorder/oversampling, neutral
unity gain, and silence-in-silence-out.

Artefact: `build_plugin/TOPOS_artefacts/Release/VST3/TOPOS.vst3` — 4.9 MB bundle,
`Contents/x86_64-linux/TOPOS.so` = 5,100,584 bytes.

**Not done, and not claimable.**

- **AU was not built.** The Audio Unit format can only be built on macOS; this
  sandbox is Linux. The CMake target adds AU under `if(APPLE)`, so it should
  build on your Mac, but it is untested.
- **No `pluginval`, no DAW test, no listening test.** Every audio judgement here
  is a measurement, not an opinion. The engines are verified stable, bounded, and
  demonstrably doing what they claim structurally; whether they sound *good* is
  unverified.
- **No presets, no parameter smoothing pass on the wrapper side**, no MIDI or
  modulation sources, no GUI design work beyond a functional layout.
- The `-flto` build emits a "serial LTRANS jobs" note. Harmless, but link times
  are long on two cores.

Because AU validation is impossible here, `SPEC_CLAUDE_CODE.md` contains the
full handoff specification with a validation-gauntlet prompt for finishing on the
Mac.

---

## Build

```bash
# prototypes and offline validation
cd topos
g++ -O2 -std=c++17 tests/topos_cli.cpp -o build/topos_cli
./build/topos_cli --selftest
python3 tools/junction_passivity_proof.py
python3 tools/topology_ab_test.py
python3 tools/render_core_demos.py          # -> audio/topos_*.wav
python3 tools/render_faust_demos.py         # needs faust; -> audio/faust_*.wav

# plugin (VST3 everywhere, AU additionally on macOS)
cmake -S . -B build_plugin -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_plugin -j2
```

JUCE 8.0.4 is expected at `../build_deps/JUCE`; adjust the `add_subdirectory` path
in `CMakeLists.txt` if yours lives elsewhere.

## Audio examples

`audio/` — 48 kHz, 24-bit, stereo. Peak levels are pre-normalisation figures from
the render log.

Rack demos, rendered through the actual C++ core:

| File | What it shows |
|---|---|
| `topos_01_scatter_star_to_mesh.wav` | ratio morph, then coupling star → mesh |
| `topos_02_route_energy_morph.wav` | cutoff sweep with unit-norm LP→BP→HP morph |
| `topos_03_disperse_env_material.wav` | dispersion sweep, then envelope depth; loud and quiet differ |
| `topos_04_vowel_hysteresis.wav` | one vowel path up then down — the two do not match |
| `topos_05_thermal_drift.wav` | quiet, hard-driven, then quiet again |
| `topos_06_series_chain.wav` | SCATTER → DISPERSE → THERMAL, order swapped mid-render |
| `topos_07_parallel_lanes.wav` | VOWEL ǀ ROUTE ǀ SCATTER in three lanes, cross-fading |
| `topos_08_hybrid_matrix.wav` | two series lanes plus one single-slot lane |

Faust prototype demos: `faust_scatter_modal.wav`,
`faust_energy_routed_multimode.wav`, `faust_nonlinear_dispersion.wav`,
`faust_hysteretic_formant.wav`, `faust_electrothermal_ladder.wav`.

## References

- Faust `filters.lib` scattering junctions (`scatN`) and allpass structures —
  [Faust libraries documentation](https://faustlibraries.grame.fr/libs/filters/)
- Julius O. Smith III, *Physical Audio Signal Processing*, on Kelly-Lochbaum
  junctions and normalised wave variables —
  [ccrma.stanford.edu/~jos/pasp/](https://ccrma.stanford.edu/~jos/pasp/)
- JUCE 8 `AudioProcessorValueTreeState` —
  [JUCE documentation](https://docs.juce.com/master/classAudioProcessorValueTreeState.html)
