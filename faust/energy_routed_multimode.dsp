declare name "TOPOS ROUTE — Energy-Routed Multimode Filter";
declare author "Renky Denk Audio";
declare version "0.2";
declare description "A single conjugate pole pair is tapped as LP / BP / HP and recombined
through a unit-norm routing vector derived from two spherical angles. Because the routing
vector always has L2 norm 1, morphing between responses cannot produce the gain bulge of
naive LP/BP/HP crossfading. The orthogonal complement of the routing vector feeds the
second channel, giving decorrelated width from the same pole pair.";

import("stdfaust.lib");

//======================== controls =========================================
fc    = hslider("[01]Cutoff[unit:Hz][scale:log]", 800, 20, 18000, 0.1) : si.smoo;
q     = hslider("[02]Resonance", 3.0, 0.5, 40, 0.001) : si.smoo;
theta = hslider("[03]Route Theta", 0.0, 0, 1, 0.0001) : si.smoo;   // LP <-> BP plane
phi   = hslider("[04]Route Phi", 0.0, 0, 1, 0.0001) : si.smoo;     // ... <-> HP axis
width = hslider("[05]Ortho Width", 0.35, 0, 1, 0.001) : si.smoo;
color = hslider("[06]Feedback Color", 0.0, 0, 1, 0.001) : si.smoo;  // soft clip in loop
drive = hslider("[07]Drive", 1.0, 0.1, 8, 0.01) : si.smoo;
wet   = hslider("[08]Mix", 1.0, 0, 1, 0.001) : si.smoo;

//======================== pole pair, three taps =============================
// same normalized pole pair for all three responses
lp = fi.resonlp(fc, q, 1.0);
bp = fi.resonbp(fc, q, 1.0);
hp = fi.resonhp(fc, q, 1.0);

taps = _ <: (lp, bp, hp);

//======================== unit-norm routing =================================
th = theta * ma.PI * 0.5;
ph = phi   * ma.PI * 0.5;

c1 = cos(th) * cos(ph);
c2 = sin(th) * cos(ph);
c3 = sin(ph);

// an orthogonal companion vector (exactly perpendicular to c, unit norm)
d1 = 0.0 - sin(th);
d2 =  cos(th);
d3 =  0.0;

routeMain = (*(c1), *(c2), *(c3)) :> _;
routeOrth = (*(d1), *(d2), *(d3)) :> _;

// nonlinear coloration applied inside the resonant path, not after it
sat(x) = (1.0-color)*x + color*ma.tanh(x*(1.0+3.0*color));

core = *(drive) : sat : taps <: (routeMain, routeOrth) : (_, *(width))
     : (_ <: _,_), (_ <: *(1.0), *(-1.0)) :> _,_ : (fi.dcblocker, fi.dcblocker);

process = _ <: ((*(1.0-wet) <: _,_), (core : (*(wet), *(wet)))) :> _,_;
