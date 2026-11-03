declare name "TOPOS VOWEL — Hysteretic Formant Filter";
declare author "Renky Denk Audio";
declare version "0.2";
declare description "A four-formant resonator bank in which every formant frequency and
bandwidth reaches its target through a direction-dependent (asymmetric) slew, so opening
and closing gestures follow different trajectories. That gives the bank articulatory
memory: the same vowel position sounds different depending on where it came from, and
loud input drags formants toward a wider, growl-like configuration. A nasal anti-formant
notch is included.";

import("stdfaust.lib");

NF = 4;   // number of formants

//======================== controls =========================================
vowel  = hslider("[01]Vowel", 0.0, 0, 4, 0.001);            // A E I O U path
tract  = hslider("[02]Tract Scale", 1.0, 0.5, 2.0, 0.001) : si.smoo;
openms = hslider("[03]Open Slew[unit:ms][scale:log]", 12, 0.5, 500, 0.01);
closems= hslider("[04]Close Slew[unit:ms][scale:log]", 90, 0.5, 500, 0.01);
growl  = hslider("[05]Growl (env->formant)", 0.35, 0, 1, 0.001) : si.smoo;
bwscale= hslider("[06]Bandwidth", 1.0, 0.25, 4.0, 0.001) : si.smoo;
nasal  = hslider("[07]Nasal Antiformant", 0.0, 0, 1, 0.001) : si.smoo;
drive  = hslider("[08]Drive", 1.0, 0.1, 8, 0.01) : si.smoo;
wet    = hslider("[09]Mix", 1.0, 0, 1, 0.001) : si.smoo;

//======================== direction-dependent slew ==========================
// one-pole smoother whose coefficient depends on whether the target is rising
// or falling: the source of the hysteresis.
cOpen  = exp(-1.0/max(1.0, openms  * 0.001 * ma.SR));
cClose = exp(-1.0/max(1.0, closems * 0.001 * ma.SR));

asym(x) = y letrec { 'y = x + (y - x) * ba.if(x > y, cOpen, cClose); };

//======================== vowel formant table ===============================
// F1..F4 for A, E, I, O, U (male-ish averages), Hz
fA(i) = ba.take(i+1, ( 730, 1090, 2440, 3400));
fE(i) = ba.take(i+1, ( 530, 1840, 2480, 3500));
fI(i) = ba.take(i+1, ( 270, 2290, 3010, 3700));
fO(i) = ba.take(i+1, ( 570,  840, 2410, 3300));
fU(i) = ba.take(i+1, ( 300,  870, 2240, 3200));
bA(i) = ba.take(i+1, (  60,   90,  120,  160));

seg(v, a, b) = (1.0 - v)*a + v*b;

// piecewise morph through the five vowels
ftab(i) = (vowel < 1)*seg(vowel,       fA(i), fE(i))
        + (vowel >= 1)*(vowel < 2)*seg(vowel-1, fE(i), fI(i))
        + (vowel >= 2)*(vowel < 3)*seg(vowel-2, fI(i), fO(i))
        + (vowel >= 3)*seg(min(1.0, vowel-3),   fO(i), fU(i));

env = abs : an.amp_follower_ar(0.005, 0.15) : min(1.0);

// target formant frequency: table * tract scale, dragged upward in bandwidth
// and downward in frequency by level (growl)
ftarget(i, e) = ftab(i) * tract * (1.0 - 0.25*growl*e);
btarget(i, e) = bA(i) * bwscale * (1.0 + 3.0*growl*e);

// each formant parameter passes through the asymmetric slew
fsm(i, e) = asym(ftarget(i, e)) : max(40.0) : min(0.45*ma.SR);
bsm(i, e) = asym(btarget(i, e)) : max(10.0) : min(2000.0);

// resonator gains: lower formants louder, classic vocal tilt
gf(i) = ba.take(i+1, (1.0, 0.65, 0.4, 0.22));

formant(i, e) = fi.resonbp(fsm(i, e), fsm(i, e)/max(10.0, bsm(i, e)), gf(i));

bank(e) = _ <: par(i, NF, formant(i, e)) :> _;

// nasal anti-formant: notch near 1 kHz scaled with the tract
antif = fi.notchw(180 * bwscale, 1000 * tract);
nasalStage = _ <: (*(1.0 - nasal), (antif : *(nasal))) :> _;

core = _ <: (_, env) : \(x, e).(x : *(drive) : ma.tanh : bank(e)) : nasalStage : fi.dcblocker;

process = _ <: ((*(1.0-wet) <: _,_), (core <: (*(wet), *(wet)))) :> _,_;
