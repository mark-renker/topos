declare name "TOPOS DISPERSE — Envelope-Dependent Nonlinear Dispersion";
declare author "Renky Denk Audio";
declare version "0.2";
declare description "A normalized allpass lattice whose reflection coefficients are driven by
the input envelope, so loud material and quiet material propagate with different group delay.
Magnitude response stays flat (allpass) while transient shape and perceived material change.
Normalized lattice form is used because it tolerates time-varying coefficients far better
than a naively modulated direct-form allpass chain. The two channels use different
coefficient polarity, producing stereo divergence with no chorus-style delay modulation.";

import("stdfaust.lib");

NAP = 8;   // allpass lattice order

//======================== controls =========================================
base   = hslider("[01]Dispersion", 0.55, -0.95, 0.95, 0.0001) : si.smoo;
depth  = hslider("[02]Env Depth", 0.6, -1, 1, 0.001) : si.smoo;
tilt   = hslider("[03]Coeff Tilt", 0.3, -1, 1, 0.001) : si.smoo;   // per-stage gradient
atkms  = hslider("[04]Env Attack[unit:ms][scale:log]", 3, 0.1, 200, 0.01);
relms  = hslider("[05]Env Release[unit:ms][scale:log]", 120, 1, 2000, 0.01);
stages = hslider("[06]Stages", NAP, 1, NAP, 1);
spread = hslider("[07]Stereo Spread", 0.5, 0, 1, 0.001) : si.smoo;
wet    = hslider("[08]Mix", 1.0, 0, 1, 0.001) : si.smoo;

//======================== envelope ==========================================
env = abs : an.amp_follower_ar(atkms/1000.0, relms/1000.0) : min(1.0);

//======================== coefficient field =================================
// per-stage base value with a tilt gradient, modulated by the envelope, and
// hard-limited to (-0.98, 0.98) so the lattice stays stable at every instant.
clampc(x) = max(-0.98, min(0.98, x));
coef(i, e, pol) = clampc(pol * (base * (1.0 + tilt*(2.0*i/float(NAP-1) - 1.0)) + depth*e))
                  * (i < stages);

lat(pol, e) = fi.allpassnn(NAP, par(i, NAP, coef(i, e, pol)));

// polarity of the right channel: +1 at zero spread (mono), -1 at full spread
polR = 1.0 - 2.0*spread;

process = _ <: (dry, disp) :> _,_
with {
    dry  = *(1.0 - wet) <: _,_;
    disp = _ <: (_, env) <: (routeL, routeR);
    routeL(x, e) = x : lat( 1.0, e) : *(wet);
    routeR(x, e) = x : lat(polR, e) : *(wet);
};
