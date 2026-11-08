#pragma once
#include <juce_dsp/juce_dsp.h>

// MOONSHOT clipper DSP : hard / soft clipping, driven by one BUILD macro.
// Header-only on purpose so it can be unit-tested without any GUI code.
namespace moonshot
{
enum class Mode { hard = 0, soft = 1 };

// BUILD (0..1) -> drive in dB.  0% = unity, 100% = +20 dB.
inline float buildToDriveDb (float build) noexcept
{
    build = juce::jlimit (0.0f, 1.0f, build);
    return 20.0f * build;
}

// Pure transfer curves. u is the signal AFTER the drive gain.
//
// HARD : transparent up to the ceiling, then a brick wall (flat top).
// SOFT : "Fruity Soft Clipper" style. Linear up to a knee T, then a smooth tanh
//        shoulder that approaches the ceiling asymptotically. Slope is continuous
//        at T (no corner), the output can never exceed the ceiling.
//        The knee drops as BUILD rises (T = ceiling at 0 %, ceiling/2 at 100 %), so
//        BUILD 0 % is transparent and the saturation grows as you push it.
inline float hardClip (float u, float ceiling) noexcept
{
    return juce::jlimit (-ceiling, ceiling, u);
}

inline float kneeFromBuild (float build, float ceiling) noexcept
{
    return ceiling * (1.0f - 0.5f * juce::jlimit (0.0f, 1.0f, build));
}

inline float softClip (float u, float knee, float ceiling) noexcept
{
    const float a = std::abs (u);
    if (a <= knee) return u;
    const float width = ceiling - knee;
    if (width < 1.0e-6f) return std::copysign (ceiling, u);
    return std::copysign (knee + width * std::tanh ((a - knee) / width), u);
}

class ClipperDSP
{
public:
    static constexpr float ceilingDb = -0.3f;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        numChannels = (int) spec.numChannels;
        maxBlock = (int) spec.maximumBlockSize;

        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numChannels, 2, // 2 stages = 4x
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampler->initProcessing ((size_t) spec.maximumBlockSize);

        mixer.prepare (spec);
        mixer.setMixingRule (juce::dsp::DryWetMixingRule::linear);
        mixer.setWetLatency (oversampler->getLatencyInSamples());

        buildSmoothed.reset (sampleRate * 4.0, 0.03);
        buildSmoothed.setCurrentAndTargetValue (0.0f);
        modeSmoothed.reset (sampleRate * 4.0, 0.015);
        modeSmoothed.setCurrentAndTargetValue (mode == Mode::soft ? 1.0f : 0.0f);
        reset();
    }

    void reset()
    {
        if (oversampler != nullptr) oversampler->reset();
        mixer.reset();
    }

    float getLatencySamples() const noexcept
    {
        return oversampler != nullptr ? oversampler->getLatencyInSamples() : 0.0f;
    }

    void setParameters (float build, float mix, Mode m)
    {
        modeSmoothed.setTargetValue (m == Mode::soft ? 1.0f : 0.0f);
        mixer.setWetMixProportion (juce::jlimit (0.0f, 1.0f, mix));
        buildSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, build));
    }

    // Safe for any host block size (the oversampler is sized for maxBlock, so long buffers are chunked)
    // and for any channel count (extra channels are left untouched).
    void process (juce::AudioBuffer<float>& buffer)
    {
        if (oversampler == nullptr) return;
        juce::dsp::AudioBlock<float> all (buffer);
        const int chans = juce::jmin ((int) all.getNumChannels(), numChannels);
        if (chans <= 0) return;
        for (int pos = 0; pos < (int) all.getNumSamples(); pos += maxBlock)
        {
            const int n = juce::jmin (maxBlock, (int) all.getNumSamples() - pos);
            auto sub = all.getSubBlock ((size_t) pos, (size_t) n).getSubsetChannelBlock (0, (size_t) chans);
            processChunk (sub);
        }
    }

private:
    void processChunk (juce::dsp::AudioBlock<float>& block)
    {
        mixer.pushDrySamples (block);

        auto up = oversampler->processSamplesUp (block);
        const auto ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        const auto n = up.getNumSamples();

        for (size_t i = 0; i < n; ++i)
        {
            const float b = buildSmoothed.getNextValue();
            const float m = modeSmoothed.getNextValue();
            const float g = juce::Decibels::decibelsToGain (buildToDriveDb (b));
            const float knee = kneeFromBuild (b, ceiling);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                const float u = d[i] * g;
                const float hc = hardClip (u, ceiling), sc = softClip (u, knee, ceiling);
                d[i] = hc + (sc - hc) * m; // short crossfade: no click when the mode is switched
            }
        }

        oversampler->processSamplesDown (block);

        // Guarantee the ceiling: the down-sampling filter can overshoot a clipped edge slightly.
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* d = block.getChannelPointer (ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
                d[i] = juce::jlimit (-ceiling, ceiling, d[i]);
        }

        mixer.mixWetSamples (block);
    }

    double sampleRate = 44100.0;
    int numChannels = 2;
    int maxBlock = 512;
    Mode mode = Mode::soft;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::dsp::DryWetMixer<float> mixer { 64 }; // room for the oversampler latency
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> buildSmoothed { 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> modeSmoothed { 1.0f };
};
} // namespace moonshot
