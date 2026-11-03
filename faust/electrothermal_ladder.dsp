declare name "TOPOS THERMAL — Electrothermal Ladder Filter";
declare author "Renky Denk Audio";
declare version "0.2";
declare description "A nonlinear transistor-ladder low-pass wrapped in slow physical state:
junction temperature integrates dissipated signal power and shifts the exponential converter's
scale factor (so cutoff and resonance drift while the filter is driven hard); rail sag reduces
available headroom with a slow recovery; and fixed per-stage device mismatch breaks the ideal
symmetry of the ladder. These are the device-level memory effects that ordinary virtual-analog
filters leave out — the filter's tuning depends on what you played a second ago.";

import("stdfaust.lib");

//======================== controls =========================================
fcHz   = hslider("[01]Cutoff[unit:Hz][scale:log]", 900, 20, 18000, 0.1) : si.smoo;
res    = hslider("[02]Resonance", 0.5, 0, 1.05, 0.0001) : si.smoo;
drive  = hslider("[03]Drive", 1.0, 0.1, 20, 0.01) : si.smoo;
thermK = hslider("[04]Thermal Depth", 0.5, 0, 1, 0.001) : si.smoo;
thermT = hslider("[05]Thermal Time[unit:ms][scale:log]", 300, 5, 5000, 0.1);
sagAmt = hslider("[06]Rail Sag", 0.4, 0, 1, 0.001) : si.smoo;
sagT   = hslider("[07]Sag Recovery[unit:ms][scale:log]", 60, 1, 2000, 0.1);
mismat = hslider("[08]Mismatch", 0.3, 0, 1, 0.001) : si.smoo;
wet    = hslider("[09]Mix", 1.0, 0, 1, 0.001) : si.smoo;

//======================== slow physical states ==============================
tcoef(ms)     = exp(-1.0 / max(1.0, ms * 0.001 * ma.SR));
onepole(c, x) = y letrec { 'y = (1.0 - c)*x + c*y; };

// dissipated-power proxy -> junction temperature; |x| -> rail sag
temp(x) = onepole(tcoef(thermT), x*x) : min(4.0);
sag(x)  = onepole(tcoef(sagT), abs(x)) : min(1.0);

// exponential converter drifts with temperature: warmer -> lower cutoff and Q
fcMod(t)  = max(20.0, min(0.45*ma.SR, fcHz * (1.0 - 0.35*thermK*t)));
resMod(t) = res * (1.0 - 0.20*thermK*t);
headroom(s) = 1.0 - 0.7*sagAmt*s;    // rail sag eats available swing

// deterministic per-stage mismatch asymmetry ("this particular unit").
// Applied as sa(y,o) = tanh(y + o*y*|y|) rather than as a DC offset, so it scales
// with signal level and is exactly zero at silence — a DC offset makes the plugin
// thump-and-settle every time the host resumes processing.
o(i)     = ba.take(i+1, (0.013, -0.021, 0.008, -0.006)) * mismat;
sa(y, o) = ma.tanh(y + o*y*abs(y));

//======================== nonlinear ladder ==================================
// Four saturating one-pole stages inside a global resonance loop. Stage states
// are updated in one letrec, so the resonance path carries exactly one sample of
// delay (the standard unit-delay-resolved ladder). Every stage is bounded by its
// own tanh, which is what keeps the structure stable at high resonance and drive.
lad(x, t, s) = y4
letrec {
    'y1 = bound(y1 + G*(ma.tanh((x*drive - 4.0*resMod(t)*y4) * headroom(s)) - sa(y1, o(0))));
    'y2 = bound(y2 + G*(ma.tanh(y1) - sa(y2, o(1))));
    'y3 = bound(y3 + G*(ma.tanh(y2) - sa(y3, o(2))));
    'y4 = bound(y4 + G*(ma.tanh(y3) - sa(y4, o(3))));
}
with {
    G        = min(1.0, 2.0*ma.PI*fcMod(t)/ma.SR);
    bound(v) = max(-8.0, min(8.0, v));
};

core = _ <: (_, _, _) : (_, temp, sag) : lad : fi.dcblocker;

process = _ <: ((*(1.0-wet) <: _,_), (core <: (*(wet), *(wet)))) :> _,_;
