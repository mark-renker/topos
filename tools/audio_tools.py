"""Excitation generation + raw<->wav conversion + analysis for TOPOS validation."""
import numpy as np, wave, struct, sys, os

SR = 48000

def write_raw(path, x):
    np.asarray(x, dtype='<f4').tofile(path)

def read_raw(path, channels=2):
    x = np.fromfile(path, dtype='<f4')
    n = len(x) // channels
    return x[:n * channels].reshape(n, channels)

def write_wav(path, x, sr=SR, bits=24):
    x = np.atleast_2d(np.asarray(x, dtype=np.float64))
    if x.shape[0] < x.shape[1]:
        x = x.T                      # (frames, channels)
    peak = np.max(np.abs(x)) or 1.0
    if peak > 0.99:
        x = x * (0.99 / peak)
    ch = x.shape[1]
    with wave.open(path, 'wb') as w:
        w.setnchannels(ch); w.setsampwidth(3); w.setframerate(sr)
        q = np.clip(np.round(x * 8388607.0), -8388608, 8388607).astype(np.int32).ravel()
        b = bytearray()
        for v in q:
            b += struct.pack('<i', int(v))[0:3]
        w.writeframes(bytes(b))
    return path

def fade(x, ms=8, sr=SR):
    n = int(sr * ms / 1000)
    w = np.ones(len(x)); w[:n] = np.linspace(0, 1, n); w[-n:] = np.linspace(1, 0, n)
    if x.ndim == 2:
        w = w[:, None]
    return x * w

# ---------- excitations ----------
def impulse_train(dur, period, sr=SR, amp=0.9, width=3):
    x = np.zeros(int(dur * sr))
    for t in np.arange(0, dur, period):
        i = int(t * sr)
        if i + width < len(x):
            x[i:i + width] = amp * np.hanning(width * 2)[width:]
    return x

def noise_bursts(dur, period, blen=0.03, sr=SR, amp=0.5, seed=3):
    rng = np.random.default_rng(seed)
    x = np.zeros(int(dur * sr))
    nb = int(blen * sr)
    for t in np.arange(0.02, dur, period):
        i = int(t * sr)
        if i + nb < len(x):
            env = np.exp(-np.linspace(0, 6, nb))
            x[i:i + nb] += amp * rng.standard_normal(nb) * env
    return x

def plucks(dur, period, sr=SR, amp=0.8):
    """short filtered clicks: broadband but not white"""
    x = np.zeros(int(dur * sr))
    n = int(0.004 * sr)
    rng = np.random.default_rng(11)
    for t in np.arange(0.02, dur, period):
        i = int(t * sr)
        if i + n < len(x):
            c = rng.standard_normal(n) * np.exp(-np.linspace(0, 8, n))
            x[i:i + n] += amp * c
    return x

def saw(dur, f0, sr=SR, amp=0.6, nharm=64):
    t = np.arange(int(dur * sr)) / sr
    y = np.zeros_like(t)
    for k in range(1, nharm + 1):
        if k * f0 > 0.45 * sr: break
        y += ((-1) ** (k + 1)) / k * np.sin(2 * np.pi * k * f0 * t)
    return amp * y / np.max(np.abs(y))

def perc_harmonic(dur, f0, period, sr=SR, amp=0.7):
    t = np.arange(int(dur * sr)) / sr
    y = np.zeros_like(t)
    for k in (1, 2, 3, 5, 7):
        y += np.sin(2 * np.pi * f0 * k * t) / k
    y /= np.max(np.abs(y))
    env = np.zeros_like(t)
    for s in np.arange(0.02, dur, period):
        i = int(s * sr); n = int(min(period * 0.9, 0.5) * sr)
        if i + n < len(env):
            env[i:i + n] += np.exp(-np.linspace(0, 7, n))
    return amp * y * np.clip(env, 0, 1)

def speechlike(dur, f0=120, sr=SR, amp=0.6):
    """buzzy glottal-ish pulse train for formant tests"""
    t = np.arange(int(dur * sr)) / sr
    ph = 2 * np.pi * f0 * t + 0.6 * np.sin(2 * np.pi * 4.0 * t)
    y = np.zeros_like(t)
    for k in range(1, 40):
        if k * f0 > 0.45 * sr: break
        y += np.sin(k * ph) / (k ** 1.05)
    return amp * y / np.max(np.abs(y))

# ---------- analysis ----------
def report(x, sr=SR, label=""):
    x = np.atleast_2d(x)
    if x.shape[0] < x.shape[1]: x = x.T
    peak = float(np.max(np.abs(x)))
    rms = float(np.sqrt(np.mean(x ** 2)))
    dc = float(np.mean(x))
    bad = int(np.sum(~np.isfinite(x)))
    tail = float(np.sqrt(np.mean(x[-int(0.1 * sr):] ** 2))) if len(x) > 0.1 * sr else 0.0
    print(f"{label:36s} peak={peak:8.4f} rms={rms:8.5f} dc={dc:+.2e} tailrms={tail:.2e} nonfinite={bad}")
    return dict(peak=peak, rms=rms, dc=dc, tail=tail, nonfinite=bad)

def spectral_centroid(x, sr=SR):
    x = np.asarray(x).ravel()
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / sr)
    return float(np.sum(f * X) / (np.sum(X) + 1e-20))

def peak_freqs(x, sr=SR, n=8):
    x = np.asarray(x).ravel()
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / sr)
    idx = np.argsort(X)[::-1]
    out, used = [], []
    for i in idx:
        if any(abs(f[i] - u) < 15 for u in used): continue
        used.append(f[i]); out.append(round(float(f[i]), 1))
        if len(out) >= n: break
    return sorted(out)
