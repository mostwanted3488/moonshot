#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <vector>
#include "ClipperDSP.h"

using namespace moonshot;
static int failures = 0;
#define CHECK(cond, ...) do { char _m[256]; std::snprintf (_m, sizeof _m, __VA_ARGS__); \
    if (!(cond)) { std::printf ("FAIL: %s\n", _m); ++failures; } else std::printf ("ok:   %s\n", _m); } while (0)

static const double SR = 48000.0;
using Buf = juce::AudioBuffer<float>;

static Buf sine (int n, float amp, float freq)
{
    Buf b (2, n);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            b.setSample (ch, i, amp * std::sin (juce::MathConstants<float>::twoPi * freq * (float) i / (float) SR));
    return b;
}

// Band-limited noise (one-pole low-pass, ~2 kHz), scaled to a target RMS in dBFS.
static Buf noise (int n, float rmsDb, unsigned seed = 1)
{
    juce::Random r (seed);
    Buf b (2, n);
    for (int ch = 0; ch < 2; ++ch)
    {
        float z = 0;
        for (int i = 0; i < n; ++i) { z += 0.25f * ((r.nextFloat() * 2 - 1) - z); b.setSample (ch, i, z); }
    }
    double e = 0; for (int i = 0; i < n; ++i) e += b.getSample (0, i) * b.getSample (0, i);
    const float cur = (float) std::sqrt (e / n), want = juce::Decibels::decibelsToGain (rmsDb);
    b.applyGain (want / cur);
    return b;
}

static float peak (const Buf& b, int from = 0)
{
    float p = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = from; i < b.getNumSamples(); ++i) p = std::max (p, std::abs (b.getSample (ch, i)));
    return p;
}
static double rmsDb (const Buf& b, int from = 2000)
{
    double e = 0; int c = 0;
    for (int i = from; i < b.getNumSamples(); ++i) { e += b.getSample (0, i) * b.getSample (0, i); ++c; }
    return 10.0 * std::log10 (e / c + 1e-20);
}

static Buf run (float build, float mix, Mode m, const Buf& in)
{
    ClipperDSP d; d.prepare ({ SR, 512, 2 });
    // jump straight to the target state (the smoothers are primed by a short warm-up of silence)
    Buf warm (2, 512); warm.clear();
    for (int k = 0; k < 64; ++k) { d.setParameters (build, mix, m); d.process (warm); }
    auto out = in;
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        const int n = std::min (512, out.getNumSamples() - pos);
        Buf sub (out.getArrayOfWritePointers(), 2, pos, n);
        d.setParameters (build, mix, m);
        d.process (sub);
    }
    return out;
}

int main()
{
    const float C = juce::Decibels::decibelsToGain (ClipperDSP::ceilingDb);
    const int N = 96000;

    std::printf ("--- transfer curves ---\n");
    CHECK (std::abs (buildToDriveDb (0.f)) < 1e-6f && std::abs (buildToDriveDb (1.f) - 20.f) < 1e-4f, "build 0%% = 0 dB, 100%% = +20 dB drive");
    CHECK (hardClip (0.7f, C) == 0.7f && hardClip (3.f, C) == C && hardClip (-3.f, C) == -C, "hard: transparent below ceiling, flat above");
    {   const float T = kneeFromBuild (0.6f, C);
        CHECK (softClip (0.5f * T, T, C) == 0.5f * T, "soft: linear below the knee");
        const float e = 1e-3f, s1 = (softClip (T - e, T, C) - softClip (T - 2 * e, T, C)) / e, s2 = (softClip (T + 2 * e, T, C) - softClip (T + e, T, C)) / e;
        CHECK (std::abs (s1 - s2) < 0.02f, "soft: slope continuous at the knee (%.3f vs %.3f)", s1, s2);
        bool mono = true, bounded = true, odd = true; float prev = -1;
        for (float u = 0; u < 200.f; u += 0.01f) { const float y = softClip (u, T, C); mono &= y >= prev - 1e-6f; prev = y; bounded &= y <= C + 1e-6f; odd &= std::abs (softClip (-u, T, C) + y) < 1e-6f; }
        CHECK (mono && bounded && odd, "soft: monotonic, odd-symmetric, never above the ceiling"); }
    CHECK (std::abs (kneeFromBuild (0.f, C) - C) < 1e-6f && std::abs (kneeFromBuild (1.f, C) - 0.5f * C) < 1e-6f, "knee moves from ceiling (0%%) to ceiling/2 (100%%)");

    std::printf ("--- transparency at BUILD 0%% ---\n");
    for (auto m : { Mode::hard, Mode::soft })
    {
        // two tones (110 Hz + 1.7 kHz), peak 0.9 : right under the ceiling, so nothing may change
        auto in = sine (N, 0.6f, 110.f), t2 = sine (N, 0.3f, 1700.f);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < N; ++i) in.setSample (ch, i, in.getSample (ch, i) + t2.getSample (ch, i));
        auto out = run (0.f, 1.f, m, in);
        ClipperDSP d; d.prepare ({ SR, 512, 2 }); const float latF = d.getLatencySamples();   // fractional on purpose
        float err = 0;
        for (int i = 6000; i < N - 10; ++i)
        {
            const float tt = ((float) i - latF) / (float) SR, ref = 0.6f * std::sin (juce::MathConstants<float>::twoPi * 110.f * tt) + 0.3f * std::sin (juce::MathConstants<float>::twoPi * 1700.f * tt);
            err = std::max (err, std::abs (out.getSample (0, i) - ref));
        }
        const double gainDb = rmsDb (out) - rmsDb (in);
        std::printf ("      %s: input peak %.3f, max deviation %.5f, level change %.3f dB\n", m == Mode::hard ? "hard" : "soft", peak (in), err, gainDb);
        CHECK (peak (in) < C && err < 0.004f && std::abs (gainDb) < 0.05, "%s @0%%: unity gain, waveform untouched", m == Mode::hard ? "hard" : "soft");
    }

    std::printf ("--- output ceiling (extreme input, all builds/modes) ---\n");
    {   bool ok = true; float worst = 0;
        auto hot = noise (N / 2, +6.f, 7); hot.applyGain (4.f);
        for (float b : { 0.f, .1f, .25f, .5f, .75f, 1.f }) for (auto m : { Mode::hard, Mode::soft }) { auto o = run (b, 1.f, m, hot); worst = std::max (worst, peak (o)); ok &= peak (o) <= C + 1e-4f; }
        std::printf ("      worst peak %.4f (ceiling %.4f = %.2f dBFS)\n", worst, C, juce::Decibels::gainToDecibels (C));
        CHECK (ok, "never exceeds -0.3 dBFS, even with +18 dBFS input"); }

    std::printf ("--- levels (noise at -18 dB RMS, MIX 100%%) ---\n");
    std::printf ("      build   hard RMS    soft RMS    hard peak   soft peak   (dBFS)\n");
    double prevH = -200, prevS = -200; bool monoH = true, monoS = true; double worstDiff = 0;
    auto in = noise (N, -18.f);
    for (float b : { 0.f, .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f, .9f, 1.f })
    {
        auto h = run (b, 1.f, Mode::hard, in), s = run (b, 1.f, Mode::soft, in);
        const double rh = rmsDb (h), rs = rmsDb (s);
        std::printf ("      %3d%%   %8.2f    %8.2f    %8.2f    %8.2f\n", (int) std::round (b * 100), rh, rs, juce::Decibels::gainToDecibels (peak (h, 2000)), juce::Decibels::gainToDecibels (peak (s, 2000)));
        monoH &= rh >= prevH - 0.05; monoS &= rs >= prevS - 0.05; prevH = rh; prevS = rs; worstDiff = std::max (worstDiff, std::abs (rh - rs));
    }
    CHECK (monoH && monoS, "level only goes up as BUILD goes up (no dips)");
    CHECK (prevH < -0.5 && prevS < -0.5, "100%% stays below 0 dBFS RMS (%.2f / %.2f dB)", prevH, prevS);
    CHECK (worstDiff < 4.0, "hard and soft within %.2f dB of each other (no big jump when switching)", worstDiff);

    std::printf ("--- sanity ---\n");
    {   Buf z (2, 20000); z.clear(); auto o = run (1.f, 1.f, Mode::soft, z); CHECK (peak (o) < 1e-6f, "silence in -> silence out"); }
    {   auto o = run (1.f, 1.f, Mode::hard, sine (N, 0.5f, 220.f)); double dc = 0; for (int i = 4000; i < N; ++i) dc += o.getSample (0, i); dc /= (N - 4000);
        std::printf ("      DC offset %.6f\n", dc); CHECK (std::abs (dc) < 1e-3, "no DC offset after clipping a sine"); }
    {   auto inM = sine (N / 2, 0.8f, 330.f); auto o = run (0.8f, 1.f, Mode::soft, inM); float diff = 0; for (int i = 0; i < N / 2; ++i) diff = std::max (diff, std::abs (o.getSample (0, i) - o.getSample (1, i)));
        CHECK (diff < 1e-6f, "left and right channels processed identically"); }
    {   Buf x (2, 8192); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 8192; ++i) x.setSample (ch, i, (i % 2 ? 1.f : -1.f) * 1.0e6f);
        auto o = run (1.f, 1.f, Mode::soft, x); bool fin = true; for (int i = 0; i < 8192; ++i) fin &= std::isfinite (o.getSample (0, i)); CHECK (fin && peak (o) <= C + 1e-4f, "huge input stays finite and capped"); }
    {   auto a = run (1.f, 1.f, Mode::hard, sine (N, 0.5f, 220.f)), d = run (1.f, 0.f, Mode::hard, sine (N, 0.5f, 220.f));
        ClipperDSP dd; dd.prepare ({ SR, 512, 2 }); const int lat = juce::roundToInt (dd.getLatencySamples()); auto in2 = sine (N, 0.5f, 220.f);
        float err = 0; for (int i = 6000; i < 20000; ++i) err = std::max (err, std::abs (d.getSample (0, i) - in2.getSample (0, i - lat)));
        CHECK (err < 0.01f && rmsDb (a) > rmsDb (d), "MIX 0%% = dry (delay-matched), MIX 100%% = clipped"); }

    std::printf ("--- aliasing ---\n");
    {   auto hi = sine (N, 0.5f, 10000.f); auto o = run (1.f, 1.f, Mode::hard, hi);
        auto mag = [&] (float f) { double re = 0, im = 0; for (int i = N / 4; i < N; ++i) { re += o.getSample (0, i) * std::cos (2 * juce::MathConstants<double>::pi * f * i / SR); im += o.getSample (0, i) * std::sin (2 * juce::MathConstants<double>::pi * f * i / SR); } return std::sqrt (re * re + im * im); };
        const float r = (float) (mag (2000.f) / mag (10000.f)); std::printf ("      alias @2 kHz / fundamental = %.4f\n", r); CHECK (r < 0.08f, "aliasing kept low with 4x oversampling"); }

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures;
}
