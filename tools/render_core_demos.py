#!/usr/bin/env python3
"""Render the TOPOS rack demos through the compiled C++ core and write WAVs.

Each demo gets an excitation chosen to expose what that demo is demonstrating.
The C++ CLI reads mono raw float32 and writes interleaved stereo raw float32.
"""
import os, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_tools as at

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "build", "topos_cli")
TMP = os.path.join(ROOT, "build", "render")
OUT = os.path.join(ROOT, "audio")
SR = 48000

os.makedirs(TMP, exist_ok=True)
os.makedirs(OUT, exist_ok=True)


def excite(name, dur):
    """excitation per demo"""
    if name.startswith("01"):        # resonator: needs impulsive excitation
        return at.impulse_train(dur, 0.5) * 0.9
    if name.startswith("02"):        # subtractive filter: needs harmonic content
        return at.saw(dur, 82.41)
    if name.startswith("03"):        # dispersion: needs transients, loud and quiet
        x = at.plucks(dur, 0.4)
        env = np.interp(np.arange(len(x)) / SR, [0, dur * 0.5, dur], [1.0, 1.0, 0.12])
        return x * env
    if name.startswith("04"):        # formants: needs a voice-like source
        return at.speechlike(dur, 110)
    if name.startswith("05"):        # thermal: level history matters, so ramp level
        x = at.saw(dur, 61.74)
        a = np.arange(len(x)) / SR / dur
        env = np.where(a < 0.33, 0.15, np.where(a < 0.72, 1.0, 0.15))
        return x * env
    if name.startswith("06"):        # series chain: percussive harmonic bursts
        return at.perc_harmonic(dur, 110, 0.5)
    if name.startswith("07"):        # parallel lanes: broadband + tonal
        return 0.6 * at.saw(dur, 98.0) + 0.4 * at.noise_bursts(dur, 0.5, 0.05)
    return at.perc_harmonic(dur, 82.41, 0.6)   # 08 hybrid


DUR = {"01": 10.0, "02": 8.0, "03": 8.0, "04": 8.0,
       "05": 9.0, "06": 10.0, "07": 9.0, "08": 10.0}

names = [l.split()[0] for l in subprocess.run([CLI, "--list-demos"], capture_output=True,
                                              text=True).stdout.strip().splitlines()]
ok = True
for name in names:
    dur = DUR[name[:2]]
    x = at.fade(excite(name, dur).astype(np.float32))
    inp = os.path.join(TMP, name + "_in.f32")
    raw = os.path.join(TMP, name + "_out.f32")
    at.write_raw(inp, x)

    r = subprocess.run([CLI, "--render", name, "--in", inp, "--out", raw, "--sr", str(SR)],
                       capture_output=True, text=True)
    sys.stdout.write(r.stderr)
    if r.returncode != 0:
        print(f"  !! {name} FAILED rc={r.returncode} {r.stdout}")
        ok = False
        continue

    y = at.read_raw(raw, channels=2)
    if not np.all(np.isfinite(y)):
        print(f"  !! {name} produced non-finite samples")
        ok = False
        continue
    peak = float(np.max(np.abs(y)))
    if peak < 1e-4:
        print(f"  !! {name} is effectively silent (peak={peak:.2e})")
        ok = False
        continue
    at.write_wav(os.path.join(OUT, "topos_" + name + ".wav"), at.fade(y, 20))
    print(f"  -> audio/topos_{name}.wav  peak={peak:.3f} "
          f"centroidL={at.spectral_centroid(y[:, 0]):.0f} Hz")

print("ALL_CORE_DEMOS_OK" if ok else "CORE_DEMOS_HAD_FAILURES")
sys.exit(0 if ok else 1)
