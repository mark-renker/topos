#!/usr/bin/env python3
"""A/B evidence that SCATTER's Coupling control re-wires the network rather than
re-tuning it.

Identical excitation, identical modal ratios, identical decay/brightness. Only the
topology vector changes: star (coupling = 0, eight independent resonators) versus
mesh (coupling = 1, uniform Householder mixing). If Coupling were a timbre tilt the
partial set would stay put and only levels would move. What we expect instead is a
different SET of resonant peaks, because the branches now load each other.

Also measures the ring time at each topology. Note that the peak level here is set by
the input strike passing through the wet path, so peak is NOT a useful discriminator in
this test - the spectra and the decay are. Passivity under audio-rate topology modulation
is tested separately, with sustained excitation, in tests/topos_cli.cpp
("SCATTER: passive under 40 Hz topology modulation").
"""
import os, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_tools as A

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build_faust")
TMP = os.path.join(ROOT, "build", "ab")
os.makedirs(TMP, exist_ok=True)
SR = 48000
EXE = os.path.join(BUILD, "scatter_modal_harness")

if not os.path.exists(EXE):
    sys.exit(f"missing {EXE} — run tools/render_faust_demos.py first")

# single strike, then let it ring: a clean look at the network's own modes
x = np.zeros(int(SR * 4.0), dtype=np.float32)
x[100:103] = 0.9
inp = os.path.join(TMP, "strike.f32")
A.write_raw(inp, x)


def run(coupling, loss=0.9995):
    score = os.path.join(TMP, f"c{coupling}_{loss}.txt")
    with open(score, "w") as f:
        f.write(f"""sr {SR}
dur 4.0
set Fundamental 98
set Ratio_Morph 0.0
set Coupling {coupling}
set Decay_T60 3.0
set Brightness 8000
set Junction_LP 12000
set Losslessness {loss}
set Drive 1.0
set Mix 1.0
""")
    out = os.path.join(TMP, f"c{coupling}_{loss}.f32")
    r = subprocess.run([EXE, "--score", score, "--in", inp, "--out", out],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"render failed: {r.stderr}")
    return A.read_raw(out, channels=2)


star = run(0.0)
mesh = run(1.0)

fs = A.peak_freqs(star[:, 0], n=8)
fm = A.peak_freqs(mesh[:, 0], n=8)
print("star  (coupling 0) top partials Hz:", [f"{v:.1f}" for v in fs])
print("mesh  (coupling 1) top partials Hz:", [f"{v:.1f}" for v in fm])
print(f"star  centroid {A.spectral_centroid(star[:,0]):8.1f} Hz   "
      f"rms {np.sqrt(np.mean(star**2)):.5f}")
print(f"mesh  centroid {A.spectral_centroid(mesh[:,0]):8.1f} Hz   "
      f"rms {np.sqrt(np.mean(mesh**2)):.5f}")

# how many of the star partials survive in the mesh, within 1.5%?
kept = sum(1 for a in fs if any(abs(a - b) / max(a, 1.0) < 0.015 for b in fm))
print(f"\nstar partials that survive in the mesh (within 1.5%): {kept} of {len(fs)}")

# decay-tail comparison: mesh should redistribute energy across branches
def tail_rms(y, t0, t1):
    return float(np.sqrt(np.mean(y[int(t0 * SR):int(t1 * SR)] ** 2)))

print(f"tail rms 2.0-4.0 s   star {tail_rms(star[:,0],2,4):.6f}   "
      f"mesh {tail_rms(mesh[:,0],2,4):.6f}")

# ring time: measure decay of the resonant part, skipping the input strike itself.
def t60(y):
    """seconds for the smoothed envelope to fall 60 dB below its post-strike peak"""
    env = np.abs(y[int(0.05 * SR):])
    k = int(0.02 * SR)
    env = np.convolve(env, np.ones(k) / k, mode="same")
    ref = float(np.max(env))
    below = np.where(env < ref * 1e-3)[0]
    return float(below[0]) / SR if below.size else float(len(env)) / SR


for c in (0.0, 0.35, 1.0):
    lossy = run(c, loss=0.9990)
    tight = run(c, loss=1.0)
    tl, tt = t60(lossy[:, 0]), t60(tight[:, 0])
    print(f"coupling {c:4.2f}: T60 at loss 0.999 = {tl:.2f} s   "
          f"at loss 1.000 = {tt:.2f} s   (requested 3.0 s)")
    assert np.all(np.isfinite(lossy)) and np.all(np.isfinite(tight))
    # a lossless junction must never ring SHORTER than a lossy one
    assert tt >= tl - 0.05, "loss control is inverted"

assert kept <= 4, "coupling barely changed the partial set — topology control is weak"
print("\nTOPOLOGY_AB_OK")
