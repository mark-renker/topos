declare name "TOPOS SCATTER (fi.scatN reference variant)";
declare author "Renky Denk Audio";
declare version "0.2";
declare note "REFERENCE / SUPERSEDED. This is the literal fi.scatN Kelly-Lochbaum
implementation. It compiles and it is physically correct, but fi.scatN realises the
UNNORMALISED junction out_i = sum_j alpha_j in_j - in_i, which is lossless only in the
admittance-WEIGHTED norm. In the plain L2 norm that actually governs the surrounding
delay loops, its gain exceeds unity for EVERY non-uniform coupling setting: 1.30x at
coupling 0.9, 2.98x at 0.5, and 5.47x in the star limit for N = 8 (see
tools/junction_passivity_proof.py). The resonant network built around it duly runs away:
the offline test suite caught peaks of 65x at parameter extremes and an energy ratio of
1.105 under 40 Hz topology modulation, and both tests now pass on the normalised form.
The shipping engine (scatter_modal.dsp and dsp/engines/ScatterModal.h) therefore uses
NORMALISED wave variables, where the same junction becomes an exactly orthogonal
Householder reflection. Kept here because it documents the trap.";
declare description "N-branch digital waveguide network terminated in a shared N-port
scattering junction (fi.scatN). The alpha-parameter vector is the topology control:
concentrating alpha on one branch gives a STAR network (near-independent resonators),
spreading it uniformly gives a MESH network (fully diffusive, plate-like mode mixing).
Passivity is structural: sum(alpha) <= 2 and every branch loop gain <= 1, so the network
cannot blow up under audio-rate topology modulation.";

import("stdfaust.lib");

N      = 8;      // number of waveguide branches / scattering ports
MAXDEL = 8192;   // max branch delay in samples

//======================== controls =========================================
f0     = hslider("[01]Fundamental[unit:Hz][scale:log]", 110, 20, 2000, 0.01) : si.smoo;
topo   = hslider("[02]Ratio Morph", 0, 0, 2, 0.001) : si.smoo;   // harmonic -> plate -> inharmonic
coup   = hslider("[03]Coupling", 0.5, 0, 1, 0.001) : si.smoo;    // star -> mesh
decay  = hslider("[04]Decay T60[unit:s][scale:log]", 2.0, 0.05, 20, 0.01) : si.smoo;
bright = hslider("[05]Brightness[unit:Hz][scale:log]", 6000, 200, 18000, 1) : si.smoo;
jfc    = hslider("[06]Junction LP[unit:Hz][scale:log]", 9000, 500, 20000, 1) : si.smoo;
loss   = hslider("[07]Losslessness", 0.999, 0.90, 1.0, 0.0001) : si.smoo;
drive  = hslider("[08]Drive", 1, 0.1, 8, 0.01) : si.smoo;
wet    = hslider("[09]Mix", 1, 0, 1, 0.001) : si.smoo;

//======================== modal ratio templates ============================
// harmonic (ideal string), circular-plate-like, and inharmonic (golden) sets
rh(i) = i + 1.0;
rp(i) = ba.take(i+1, (1.0, 1.593, 2.135, 2.295, 2.653, 2.917, 3.155, 3.500));
rg(i) = pow(1.6180339887, i * 0.8);

ratio(i) = (topo < 1.0) * ((1.0-topo)*rh(i) + topo*rp(i))
         + (topo >= 1.0) * ((2.0-topo)*rp(i) + (topo-1.0)*rg(i));

// branch loop length in samples for the requested modal frequency
dl(i)  = max(4.0, min(MAXDEL-4.0, ma.SR / max(1.0, f0*ratio(i))));
// per-branch loop gain for the requested T60
lg(i)  = pow(0.001, dl(i) / max(1.0, decay*ma.SR));

//======================== alpha vector = topology ==========================
// w(i) sums to exactly 1 for any coupling value, so sum(alpha) = 2*loss <= 2.
w(i)     = coup/float(N) + (1.0-coup)*(i == 0);
alpha(i) = 2.0 * loss * w(i);

//======================== branch waveguides ===============================
branch(i) = de.fdelay(MAXDEL, dl(i)-1.0) : fi.lowpass(1, bright) : *(lg(i));

// N-port junction with a positive-real (first order lowpass) junction filter,
// making the junction reactive/dispersive while remaining passive.
junction = fi.scatN(N, par(i, N, alpha(i)), fi.lowpass(1, jfc));

network = (si.bus(2*N) :> si.bus(N) : junction : par(i, N, branch(i))) ~ si.bus(N);

//======================== pickups ==========================================
wl(i) = (i % 2 == 0)*0.95 + (i % 2 == 1)*0.35;
wr(i) = (i % 2 == 0)*0.35 + (i % 2 == 1)*0.95;

pickups = si.bus(N) <: (par(i,N,*(wl(i))) :> _), (par(i,N,*(wr(i))) :> _);

excite  = _ <: par(i, N, *(1.0/sqrt(float(N))));

core    = *(drive) : ma.tanh : excite : network : pickups : (*(0.5), *(0.5));

process = _ <: ((*(1.0-wet) <: _,_), (core : (*(wet), *(wet)))) :> _,_;
