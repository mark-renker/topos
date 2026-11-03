#!/usr/bin/env python3
"""Build each Faust prototype with the offline harness, render an automated demo,
convert to WAV, and print validation metrics."""
import os, subprocess, sys, shutil
sys.path.insert(0, os.path.dirname(__file__))
import audio_tools as A
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FA = os.path.join(ROOT, 'faust')
BUILD = os.path.join(ROOT, 'build_faust')
OUT = os.path.join(ROOT, 'audio')
os.makedirs(BUILD, exist_ok=True); os.makedirs(OUT, exist_ok=True)

def sh(cmd, **kw):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)
    return r

DEMOS = {
 'scatter_modal': dict(
    excite=lambda: A.plucks(10.0, 1.25),
    score="""sr 48000
dur 10.0
set Fundamental 98
set Decay_T60 3.5
set Brightness 7500
set Losslessness 0.9995
set Drive 1.4
set Mix 1.0
ramp 0.0 4.5 Ratio_Morph 0 2
ramp 4.5 9.5 Coupling 0.0 1.0
ramp 0.0 10.0 Junction_LP 14000 2500
"""),
 'energy_routed_multimode': dict(
    excite=lambda: A.saw(9.0, 82.4),
    score="""sr 48000
dur 9.0
set Resonance 9
set Ortho_Width 0.55
set Mix 1.0
ramp 0.0 4.0 Cutoff 120 6000
ramp 4.0 9.0 Cutoff 6000 150
ramp 0.5 4.5 Route_Theta 0 1
ramp 4.5 8.5 Route_Phi 0 1
ramp 2.0 9.0 Feedback_Color 0 0.85
"""),
 'nonlinear_dispersion': dict(
    excite=lambda: A.perc_harmonic(9.0, 146.8, 0.7),
    score="""sr 48000
dur 9.0
set Stages 8
set Env_Attack 2.5
set Env_Release 140
set Stereo_Spread 0.7
set Mix 1.0
ramp 0.0 4.5 Dispersion -0.9 0.9
ramp 4.5 9.0 Env_Depth 0.0 0.95
ramp 0.0 9.0 Coeff_Tilt -0.9 0.9
"""),
 'hysteretic_formant': dict(
    excite=lambda: A.speechlike(10.0, 116),
    score="""sr 48000
dur 10.0
set Tract_Scale 1.0
set Open_Slew 8
set Close_Slew 260
set Growl 0.7
set Nasal_Antiformant 0.25
set Drive 1.6
set Mix 1.0
ramp 0.2 3.0 Vowel 0 4
ramp 3.2 6.0 Vowel 4 0
ramp 6.2 8.0 Vowel 0 3
ramp 8.0 9.8 Vowel 3 1
ramp 0.0 10.0 Bandwidth 0.6 1.8
"""),
 'electrothermal_ladder': dict(
    excite=lambda: A.saw(10.0, 55.0) * np.concatenate([
        np.full(48000 * 3, 0.15), np.full(48000 * 4, 1.0), np.full(48000 * 3, 0.15)]),
    score="""sr 48000
dur 10.0
set Cutoff 700
set Resonance 0.85
set Drive 6
set Thermal_Depth 0.9
set Thermal_Time 400
set Rail_Sag 0.7
set Sag_Recovery 90
set Mismatch 0.6
set Mix 1.0
ramp 0.0 5.0 Cutoff 260 2400
ramp 5.0 10.0 Cutoff 2400 200
"""),
}

results = {}
for name, cfg in DEMOS.items():
    hcpp = os.path.join(BUILD, name + '_harness.cpp')
    hbin = os.path.join(BUILD, name + '_harness')
    r = sh(f'faust -a {FA}/harness.cpp -lang cpp {FA}/{name}.dsp -o {hcpp}')
    if r.returncode: print(f'[{name}] faust FAILED\n{r.stderr[:500]}'); continue
    r = sh(f'g++ -O2 -std=c++17 {hcpp} -o {hbin}')
    if r.returncode: print(f'[{name}] g++ FAILED\n{r.stderr[:1200]}'); continue

    x = cfg['excite']()
    inraw = os.path.join(BUILD, name + '_in.f32')
    A.write_raw(inraw, x)
    scorep = os.path.join(BUILD, name + '.score')
    open(scorep, 'w').write(cfg['score'])
    outraw = os.path.join(BUILD, name + '_out.f32')
    r = sh(f'{hbin} --score {scorep} --in {inraw} --out {outraw}')
    print(f'[{name}] {r.stderr.strip()}')
    y = A.read_raw(outraw)
    m = A.report(y, label=name)
    wav = os.path.join(OUT, f'faust_{name}.wav')
    A.write_wav(wav, y)
    m['wav'] = wav
    m['stable'] = (m['nonfinite'] == 0 and m['peak'] < 50.0)
    results[name] = m

print('\n=== summary ===')
ok = True
for k, v in results.items():
    print(f"{k:30s} stable={v['stable']} peak={v['peak']:.3f}")
    ok &= v['stable'] and v['rms'] > 1e-5
print('ALL_FAUST_DEMOS_OK' if ok and len(results) == len(DEMOS) else 'FAUST_DEMOS_INCOMPLETE')
