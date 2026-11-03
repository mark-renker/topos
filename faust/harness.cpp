/* TOPOS Faust offline render harness (Faust architecture file).
   Usage:  faust -a harness.cpp x.dsp -o x_harness.cpp
           g++ -O2 -std=c++17 x_harness.cpp -o x_harness
           ./x_harness --list
           ./x_harness --score score.txt --in in.f32 --out out.f32
   Score file grammar (one command per line, '#' comments):
       sr   <hz>
       dur  <seconds>
       set  <param-path-substring> <value>
       ramp <t0> <t1> <param-path-substring> <v0> <v1>
*/
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "faust/dsp/dsp.h"
#include "faust/gui/meta.h"
#include "faust/gui/MapUI.h"

<<includeIntrinsic>>
<<includeclass>>

struct RampCmd { double t0, t1; float* zone; double v0, v1; };

int main(int argc, char* argv[])
{
    std::string scorePath, inPath, outPath;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list")) listOnly = true;
        else if (!strcmp(argv[i], "--score") && i + 1 < argc) scorePath = argv[++i];
        else if (!strcmp(argv[i], "--in")    && i + 1 < argc) inPath    = argv[++i];
        else if (!strcmp(argv[i], "--out")   && i + 1 < argc) outPath   = argv[++i];
    }

    double sr = 48000.0, dur = 4.0;
    std::vector<std::string> setP; std::vector<double> setV;
    struct RawRamp { double t0, t1; std::string p; double v0, v1; };
    std::vector<RawRamp> rawRamps;

    if (!scorePath.empty()) {
        std::ifstream f(scorePath);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string cmd; ss >> cmd;
            if (cmd == "sr")  ss >> sr;
            else if (cmd == "dur") ss >> dur;
            else if (cmd == "set") { std::string p; double v; ss >> p >> v; setP.push_back(p); setV.push_back(v); }
            else if (cmd == "ramp") { RawRamp r; ss >> r.t0 >> r.t1 >> r.p >> r.v0 >> r.v1; rawRamps.push_back(r); }
        }
    }

    mydsp DSP;
    DSP.init((int) sr);
    MapUI ui;
    DSP.buildUserInterface(&ui);

    // resolve a parameter path by substring match (case sensitive)
    auto resolve = [&](const std::string& needle) -> std::string {
        for (int i = 0; i < (int) ui.getParamsCount(); ++i) {
            std::string p = ui.getParamAddress(i);
            if (p.find(needle) != std::string::npos) return p;
        }
        fprintf(stderr, "harness: no parameter matching '%s'\n", needle.c_str());
        return {};
    };

    if (listOnly) {
        printf("inputs=%d outputs=%d sr=%d\n", DSP.getNumInputs(), DSP.getNumOutputs(), (int) sr);
        for (int i = 0; i < (int) ui.getParamsCount(); ++i) {
            std::string p = ui.getParamAddress(i);
            printf("%-56s init=%g\n", p.c_str(), ui.getParamValue(p));
        }
        return 0;
    }

    for (size_t i = 0; i < setP.size(); ++i) {
        std::string p = resolve(setP[i]);
        if (!p.empty()) ui.setParamValue(p, (FAUSTFLOAT) setV[i]);
    }
    std::vector<RampCmd> ramps;
    for (auto& r : rawRamps) {
        std::string p = resolve(r.p);
        if (p.empty()) continue;
        ramps.push_back({ r.t0, r.t1, ui.getParamZone(p), r.v0, r.v1 });
    }

    const int nIn  = DSP.getNumInputs();
    const int nOut = DSP.getNumOutputs();
    const long total = (long) (dur * sr);

    // input: raw interleaved float32 mono, looped/zero-padded
    std::vector<float> inBuf;
    if (!inPath.empty()) {
        std::ifstream f(inPath, std::ios::binary);
        f.seekg(0, std::ios::end); long n = (long) f.tellg() / 4; f.seekg(0);
        inBuf.resize(std::max(1L, n), 0.f);
        if (n > 0) f.read((char*) inBuf.data(), n * 4);
    } else inBuf.assign(1, 0.f);

    const int BS = 64;
    std::vector<std::vector<float>> ins(std::max(1, nIn), std::vector<float>(BS, 0.f));
    std::vector<std::vector<float>> outs(std::max(1, nOut), std::vector<float>(BS, 0.f));
    std::vector<float*> inPtr, outPtr;
    for (auto& v : ins)  inPtr.push_back(v.data());
    for (auto& v : outs) outPtr.push_back(v.data());

    std::ofstream of(outPath, std::ios::binary);
    std::vector<float> inter(BS * std::max(1, nOut));
    double peak = 0.0, rms = 0.0; long nonFinite = 0, written = 0;

    for (long pos = 0; pos < total; pos += BS) {
        double t = (double) pos / sr;
        for (auto& r : ramps) {
            double a = (t <= r.t0) ? 0.0 : (t >= r.t1 ? 1.0 : (t - r.t0) / (r.t1 - r.t0));
            *r.zone = (FAUSTFLOAT) (r.v0 + a * (r.v1 - r.v0));
        }
        int n = (int) std::min((long) BS, total - pos);
        for (int c = 0; c < nIn; ++c)
            for (int i = 0; i < n; ++i) {
                long idx = pos + i;
                ins[c][i] = (idx < (long) inBuf.size()) ? inBuf[idx] : 0.f;
            }
        DSP.compute(n, inPtr.data(), outPtr.data());
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < nOut; ++c) {
                float v = outs[c][i];
                if (!std::isfinite(v)) { ++nonFinite; v = 0.f; }
                inter[i * nOut + c] = v;
                peak = std::max(peak, (double) std::fabs(v));
                rms += (double) v * v;
            }
        of.write((char*) inter.data(), n * nOut * 4);
        written += n;
    }
    of.close();
    fprintf(stderr, "rendered %ld frames x %d ch | peak=%.6f rms=%.6f nonfinite=%ld\n",
            written, nOut, peak, std::sqrt(rms / std::max(1L, written * nOut)), nonFinite);
    return nonFinite ? 2 : 0;
}
