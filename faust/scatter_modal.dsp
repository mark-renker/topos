declare name "TOPOS SCATTER — Coupled Modal Scattering Network";
declare author "Renky Denk Audio";
declare version "0.3";
declare description "N-branch digital waveguide network terminated in a shared N-port
scattering junction written in NORMALISED (power) wave variables. The unit-norm vector u
is the topology control: concentrating u on one branch gives a STAR network (independent
resonators), spreading it uniformly gives a MESH network (fully diffusive, plate-like mode
mixing). Passivity is structural and instantaneous, so the topology is safe to modulate at
audio rate: the scattering matrix is exactly the Householder reflection S = 2*u*u' - I,
which is orthogonal (||S|| = 1) for ANY branch admittance distribution, every branch loop
gain is < 1, and the junction filter is positive real. Compare scatter_modal_scatN.dsp,
the literal fi.scatN form, whose plain-L2 gain reaches 5.47x in the star limit.";

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

//======================== topology vector ==================================
// w(i) sums to exactly 1 for any coupling value, hence ||u|| = 1 exactly and the
// scattering matrix below is an exact Householder reflection.
w(i) = coup/float(N) + (1.0-coup)*(i == 0);
u(i) = sqrt(max(0.0, w(i)));

//======================== branch waveguides ===============================
branch(i) = de.fdelay(MAXDEL, dl(i)-1.0) : fi.lowpass(1, bright) : *(lg(i));

// N-port junction in normalised wave variables:
//     j     = sum_i u(i)*in_i     (single junction variable)
//     out_i = 2*loss*u(i)*H(j) - in_i
// H is a first-order lowpass at unity DC gain. A one-pole lowpass is positive
// real (Nyquist locus = circle through 0 and 1 in the right half plane), so
// |2H - 1| <= 1 and the junction stays non-expansive while becoming frequency
// dependent: high partials couple across branches less than low ones.
project  = par(i, N, *(u(i))) :> fi.lowpass(1, jfc) : *(2.0*loss)
                              <: par(i, N, *(u(i)));

// (project , -identity) merged modulo N gives out_i = 2*loss*u(i)*H(j) - in_i
junction = si.bus(N) <: (project, par(i, N, *(0.0-1.0))) :> si.bus(N);

network = (si.bus(2*N) :> si.bus(N) : junction : par(i, N, branch(i))) ~ si.bus(N);

//======================== pickups ==========================================
wl(i) = (i % 2 == 0)*0.95 + (i % 2 == 1)*0.35;
wr(i) = (i % 2 == 0)*0.35 + (i % 2 == 1)*0.95;

pickups = si.bus(N) <: (par(i,N,*(wl(i))) :> _), (par(i,N,*(wr(i))) :> _);

excite  = _ <: par(i, N, *(1.0/sqrt(float(N))));

core    = *(drive) : ma.tanh : excite : network : pickups : (*(0.5), *(0.5));

process = _ <: ((*(1.0-wet) <: _,_), (core : (*(wet), *(wet)))) :> _,_;
