#!/usr/bin/env python3
"""Why the TOPOS scattering junction uses NORMALISED wave variables.

A parallel N-port junction with branch admittances Y_i can be written two ways.

UNNORMALISED (Kelly-Lochbaum, what Faust's fi.scatN implements):
    alpha_i = 2 * Y_i / sum_j Y_j          (so sum_i alpha_i = 2)
    p       = sum_j alpha_j * in_j
    out_i   = p - in_i                      =>  A = 1 alpha^T - I

    This is lossless in the ADMITTANCE-WEIGHTED norm sum_i Y_i |p_i|^2. It is NOT
    non-expansive in the plain L2 norm unless the admittances are equal. Since the
    branch loops around it are ordinary unit-gain-normalised delay lines, plain L2
    is the norm that actually governs stability, so a resonant network built on
    this form runs away as soon as the topology becomes non-uniform.

NORMALISED / power waves (what TOPOS ships):
    u_i     = sqrt(Y_i / sum_j Y_j)         (so ||u|| = 1)
    j       = sum_i u_i * in_i
    out_i   = 2 * u_i * j - in_i            =>  S = 2 u u^T - I

    S is a Householder reflection: orthogonal, so ||S|| = 1 EXACTLY for every
    admittance distribution. Passivity therefore holds instantaneously, which is
    what makes the topology control safe to modulate at audio rate. With a loss
    factor L <= 1 the singular values are |2L - 1| <= 1 and 1, and with a
    positive-real junction filter H (a one-pole lowpass at unity DC gain, whose
    Nyquist locus is the circle through 0 and 1 in the right half plane) the
    reflection eigenvalue |2H - 1| <= 1 as well.

Run this file to see the spectral norms.
"""
import numpy as np

N = 8
print(f"N = {N} branches\n")
print(f"{'coupling':>9}  {'sum(w)':>7}  {'||KL||_2':>9}  {'||Householder||_2':>18}  verdict")
worst_kl = 0.0
worst_hh = 0.0
for coup in [0.0, 0.05, 0.125, 0.25, 0.5, 0.75, 0.9, 1.0]:
    # topology weight vector: star (all weight on branch 0) -> mesh (uniform).
    # sums to exactly 1 for every coupling value.
    w = np.array([coup / N + (1.0 - coup) * (i == 0) for i in range(N)])

    A = np.tile(2.0 * w, (N, 1)) - np.eye(N)      # unnormalised Kelly-Lochbaum
    S = 2.0 * np.outer(np.sqrt(w), np.sqrt(w)) - np.eye(N)   # normalised Householder

    nk = np.linalg.norm(A, 2)
    nh = np.linalg.norm(S, 2)
    worst_kl = max(worst_kl, nk)
    worst_hh = max(worst_hh, nh)
    print(f"{coup:9.3f}  {w.sum():7.3f}  {nk:9.4f}  {nh:18.6f}  "
          f"{'KL EXPANSIVE' if nk > 1.0 + 1e-9 else 'both passive'}")

print(f"\nworst-case unnormalised (fi.scatN) gain : {worst_kl:.4f}   -> not passive")
print(f"worst-case normalised (Householder) gain: {worst_hh:.6f}   -> passive")

# orthogonality of the shipped form, to machine precision
w = np.array([0.4, 0.25, 0.15, 0.1, 0.05, 0.03, 0.015, 0.005])
u = np.sqrt(w / w.sum())
S = 2.0 * np.outer(u, u) - np.eye(N)
err = np.max(np.abs(S @ S.T - np.eye(N)))
print(f"\narbitrary admittance spread, ||S S^T - I||_inf = {err:.2e}  (S is orthogonal)")

assert worst_hh < 1.0 + 1e-9 and err < 1e-12
print("\nPASSIVITY_PROOF_OK")
