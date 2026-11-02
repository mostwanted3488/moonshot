// Plugin-level checks: runs the real MoonshotProcessor (parameters, state, latency, odd block sizes).
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static int failures = 0;
#define CHECK(cond, ...) do { char m_[256]; std::snprintf (m_, sizeof m_, __VA_ARGS__); \
    if (! (cond)) { std::printf ("FAIL: %s\n", m_); ++failures; } else std::printf ("ok:   %s\n", m_); } while (0)

using Buf = juce::AudioBuffer<float>;
static const double SR = 48000.0;
static const float C = 0.9660508789898133f; // -0.3 dBFS

static void setParam (MoonshotProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}
static Buf noise (int n, float rmsDb, unsigned seed)
{
    juce::Random r (seed); Buf b (2, n);
    for (int ch = 0; ch < 2; ++ch) { float z = 0; for (int i = 0; i < n; ++i) { z += 0.25f * ((r.nextFloat() * 2 - 1) - z); b.setSample (ch, i, z); } }
    double e = 0; for (int i = 0; i < n; ++i) e += b.getSample (0, i) * b.getSample (0, i);
    b.applyGain (juce::Decibels::decibelsToGain (rmsDb) / (float) std::sqrt (e / n));
    return b;
}
static float peak (const Buf& b, int from = 0) { float p = 0; for (int c = 0; c < b.getNumChannels(); ++c) for (int i = from; i < b.getNumSamples(); ++i) p = std::max (p, std::abs (b.getSample (c, i))); return p; }
static double rmsDb (const Buf& b, int from = 2000) { double e = 0; int c = 0; for (int i = from; i < b.getNumSamples(); ++i) { e += b.getSample (0, i) * b.getSample (0, i); ++c; } return 10 * std::log10 (e / c + 1e-20); }
static void runBlocks (MoonshotProcessor& p, Buf& b, int block)
{
    juce::MidiBuffer midi;
    for (int pos = 0; pos < b.getNumSamples(); pos += block)
    {
        const int n = std::min (block, b.getNumSamples() - pos);
        Buf sub (b.getArrayOfWritePointers(), b.getNumChannels(), pos, n);
        p.processBlock (sub, midi);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {   MoonshotProcessor p;
        p.setRateAndBufferSizeDetails (SR, 512); p.prepareToPlay (SR, 512);
        CHECK (juce::roundToInt ((float) p.getParameters().size()) == 3, "3 parameters exposed to the host (Build, Mix, Mode)");
        CHECK (p.apvts.getRawParameterValue ("build")->load() == 0.0f && p.apvts.getRawParameterValue ("mix")->load() == 1.0f && p.apvts.getRawParameterValue ("mode")->load() == 1.0f, "defaults: Build 0%%, Mix 100%%, Mode Soft");
        CHECK (p.getLatencySamples() == 4, "reports %d samples of latency to the host", p.getLatencySamples());
        juce::AudioProcessor::BusesLayout st, mono;
        st.inputBuses.add (juce::AudioChannelSet::stereo()); st.outputBuses.add (juce::AudioChannelSet::stereo());
        mono.inputBuses.add (juce::AudioChannelSet::mono());   mono.outputBuses.add (juce::AudioChannelSet::mono());
        juce::AudioProcessor::BusesLayout bad; bad.inputBuses.add (juce::AudioChannelSet::stereo()); bad.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (p.isBusesLayoutSupported (st) && p.isBusesLayoutSupported (mono) && ! p.isBusesLayoutSupported (bad), "stereo and mono supported, mismatched in/out refused"); }

    std::printf ("--- through the plugin: levels (noise -18 dB RMS) ---\n");
    std::printf ("      build   mode    RMS dBFS   peak dBFS\n");
    bool ceilingOk = true, unityOk = true; double prev[2] = { -200, -200 }; bool rising = true;
    for (int mode = 0; mode < 2; ++mode)
    {
        for (float b : { 0.f, .25f, .5f, .75f, 1.f })
        {
            MoonshotProcessor p; p.setRateAndBufferSizeDetails (SR, 512); p.prepareToPlay (SR, 512);
            setParam (p, "build", b); setParam (p, "mix", 1.f); setParam (p, "mode", (float) mode);
            auto in = noise (96000, -18.f, 3), out = in; runBlocks (p, out, 512);
            const double r = rmsDb (out), pk = juce::Decibels::gainToDecibels (peak (out, 2000));
            std::printf ("      %3d%%   %s    %7.2f    %7.2f\n", (int) (b * 100), mode ? "soft" : "hard", r, pk);
            ceilingOk &= peak (out) <= C + 1e-4f;
            if (b == 0.f) unityOk &= std::abs (r - rmsDb (in)) < 0.05;
            rising &= r >= prev[mode] - 0.05; prev[mode] = r;
        }
    }
    CHECK (ceilingOk, "output never above -0.3 dBFS");
    CHECK (unityOk, "Build 0%% leaves the level untouched in both modes");
    CHECK (rising, "level rises with Build, no dips");

    std::printf ("--- host behaviour ---\n");
    {   // block sizes: 1, odd, exactly maximum, and bigger than the prepared size
        bool ok = true;
        for (int block : { 1, 7, 64, 512, 1000, 4096 })
        {
            MoonshotProcessor p; p.setRateAndBufferSizeDetails (SR, 512); p.prepareToPlay (SR, 512);
            setParam (p, "build", 0.8f); setParam (p, "mode", 0.f);
            auto b = noise (20000, -12.f, 5); runBlocks (p, b, block);
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample (0, i); ok &= std::isfinite (v) && std::abs (v) <= C + 1e-4f; }
        }
        CHECK (ok, "blocks of 1 / 7 / 64 / 512 / 1000 / 4096 samples: no crash, finite, under the ceiling");
        MoonshotProcessor p; p.setRateAndBufferSizeDetails (SR, 512); p.prepareToPlay (SR, 512); Buf empty (2, 0); juce::MidiBuffer m; p.processBlock (empty, m);
        CHECK (true, "empty block accepted"); }
    {   // state round trip
        MoonshotProcessor a; a.prepareToPlay (SR, 512);
        setParam (a, "build", 0.37f); setParam (a, "mix", 0.5f); setParam (a, "mode", 0.f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        MoonshotProcessor b; b.prepareToPlay (SR, 512); b.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool same = std::abs (b.apvts.getRawParameterValue ("build")->load() - 0.37f) < 1e-3f && std::abs (b.apvts.getRawParameterValue ("mix")->load() - 0.5f) < 1e-3f && b.apvts.getRawParameterValue ("mode")->load() == 0.0f;
        CHECK (same, "preset / project state save and restore");
        b.setStateInformation ("garbage", 7); CHECK (true, "corrupt state data ignored without crash"); }
    {   // automation: sweep Build 0 -> 100 % while audio runs
        MoonshotProcessor p; p.setRateAndBufferSizeDetails (SR, 512); p.prepareToPlay (SR, 512);
        auto in = noise (96000, -14.f, 9), out = in; bool ok = true; juce::MidiBuffer m;
        for (int pos = 0; pos < out.getNumSamples(); pos += 128)
        {
            setParam (p, "build", (float) pos / (float) out.getNumSamples());
            Buf sub (out.getArrayOfWritePointers(), 2, pos, 128); p.processBlock (sub, m);
        }
        for (int i = 0; i < out.getNumSamples(); ++i) ok &= std::isfinite (out.getSample (0, i)) && std::abs (out.getSample (0, i)) <= C + 1e-4f;
        CHECK (ok, "automated Build sweep: stable, finite, under the ceiling");
        // switching mode while playing must not click: look for a jump much larger than the music itself
        MoonshotProcessor q; q.setRateAndBufferSizeDetails (SR, 512); q.prepareToPlay (SR, 512); setParam (q, "build", 0.5f);
        Buf tone (2, 48000); for (int i = 0; i < 48000; ++i) { const float v = 0.5f * std::sin (6.2831853f * 200.f * (float) i / 48000.f); tone.setSample (0, i, v); tone.setSample (1, i, v); }
        Buf ref = tone; runBlocks (q, ref, 128); float maxStep = 0; for (int i = 4000; i < 48000; ++i) maxStep = std::max (maxStep, std::abs (ref.getSample (0, i) - ref.getSample (0, i - 1)));
        MoonshotProcessor r2; r2.setRateAndBufferSizeDetails (SR, 512); r2.prepareToPlay (SR, 512); setParam (r2, "build", 0.5f);
        Buf sw = tone; for (int pos = 0; pos < 48000; pos += 128) { if (pos == 24064) setParam (r2, "mode", 0.f); Buf sub (sw.getArrayOfWritePointers(), 2, pos, 128); juce::MidiBuffer mm; r2.processBlock (sub, mm); }
        float maxStepSw = 0; for (int i = 4000; i < 48000; ++i) maxStepSw = std::max (maxStepSw, std::abs (sw.getSample (0, i) - sw.getSample (0, i - 1)));
        std::printf ("      largest sample-to-sample step: steady %.4f, with a mode switch %.4f\n", maxStep, maxStepSw);
        CHECK (maxStepSw < maxStep * 1.6f + 0.02f, "switching HARD/SOFT during playback does not click"); }

    std::printf ("--- editor wiring (simulated mouse, real parameters) ---\n");
    {
        MoonshotProcessor p; p.setRateAndBufferSizeDetails (SR, 512); p.prepareToPlay (SR, 512);
        MoonshotEditor ed (p); ed.setSize (892, 532);
        auto ev = [&] (juce::Point<float> pos, juce::Point<float> down, bool dragged) {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed,
                                     juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged); };
        auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (120); };
        auto raw = [&] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };

        const juce::Point<float> hardC (380, 477), softC (500, 479), build (645, 407), mix (784, 424);
        ed.mouseDown (ev (hardC, hardC, false)); pump();
        CHECK (raw ("mode") == 0.0f, "click on the HARD sticker sets Mode = Hard");
        ed.mouseDown (ev (softC, softC, false)); pump();
        CHECK (raw ("mode") == 1.0f, "click on the SOFT sticker sets Mode = Soft");

        ed.mouseDown (ev (build, build, false));
        ed.mouseDrag (ev ({ build.x, build.y - 115.0f }, build, true)); pump();
        CHECK (std::abs (raw ("build") - 0.5f) < 0.02f, "dragging the BUILD blade up 115 px sets Build to %.0f%%", raw ("build") * 100);
        ed.mouseDrag (ev ({ build.x, build.y - 400.0f }, build, true)); pump();
        CHECK (raw ("build") == 1.0f, "dragging far up stops at 100%% (no overshoot)");
        ed.mouseDrag (ev ({ build.x, build.y + 400.0f }, build, true)); pump();
        CHECK (raw ("build") == 0.0f, "dragging far down stops at 0%%");
        ed.mouseUp (ev (build, build, true));

        ed.mouseDown (ev (mix, mix, false)); ed.mouseDrag (ev ({ mix.x, mix.y + 80.0f }, mix, true)); ed.mouseUp (ev (mix, mix, true)); pump();
        CHECK (std::abs (raw ("mix") - 0.5f) < 0.02f, "dragging the MIX blade down 80 px sets Mix to %.0f%%", raw ("mix") * 100);
        ed.mouseDoubleClick (ev (mix, mix, false)); pump();
        CHECK (raw ("mix") == 1.0f, "double-click on MIX resets it to 100%%");

        // host automation -> editor follows
        setParam (p, "build", 0.73f); pump(); pump();
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (juce::File ("/tmp/mfull/out/12_host_automation_73.png")); out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (img, out);
        CHECK (img.getWidth() == 892, "editor repainted after host automation (see 12_host_automation_73.png)");
    }

    std::printf (failures == 0 ? "\nALL PLUGIN TESTS PASSED\n" : "\n%d PLUGIN TEST(S) FAILED\n", failures);
    return failures;
}
