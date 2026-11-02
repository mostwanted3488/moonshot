#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// MOONSHOT editor : 892 x 532 artwork scene, drawn natively.
//  - BUILD / MIX : the two saw-blade knobs (drag up/down, double-click = reset)
//  - HARD / SOFT : the two stickers (click)
//  - the player, the ball and the planet follow BUILD ; at 100 % the planet explodes
class MoonshotEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit MoonshotEditor (MoonshotProcessor&);
    ~MoonshotEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // exposed for the offscreen render test
    void setPreviewState (float build, float mix, int modeIndex, double simulatedSeconds);

private:
    struct Particle { float x, y, vx, vy, age, life, r, rot, vr; juce::Colour c; bool smoke = false; };
    enum class SceneState { idle, boom, done, reform };

    void timerCallback() override;
    void advance (float dt);
    void triggerBoom();
    void makeRubble();

    // drawing
    void paintScene (juce::Graphics&);
    void paintEarth (juce::Graphics&, float alpha, float warn);
    void paintWave (juce::Graphics&);
    void paintSticker (juce::Graphics&, juce::Rectangle<float> r, float rot, bool on, const juce::String& label, const juce::String& tiny);
    void paintKnob (juce::Graphics&, juce::Rectangle<float> box, float value);
    void paintBoom (juce::Graphics&, float age);
    void paintRubble (juce::Graphics&);
    void paintTexts (juce::Graphics&);

    juce::Image getPlayerFrame (int index);
    juce::Path makeTextPath (const juce::Typeface::Ptr&, const juce::String&, float em, float letterSpacing) const;
    void drawTextPath (juce::Graphics&, juce::Path p, juce::Point<float> anchor, int align /*-1 left,0 centre,1 right*/,
                       juce::Colour fill, float outline, float rotateDeg = 0.0f) const;

    juce::Point<float> toStage (juce::Point<float> p) const { return p / scale; }

    MoonshotProcessor& proc;
    juce::ParameterAttachment buildAtt, mixAtt, modeAtt;
    float vBuild = 0.0f, vMix = 1.0f;
    int modeIndex = 1;

    // assets
    juce::Image bg, earthImg, earthTint, sawImg, ballImg;
    std::vector<juce::Image> playerFrames;
    juce::Typeface::Ptr tfAnton, tfGrunge;

    // scene state
    float scale = 1.0f;
    float dv = 0.0f, spin = 0.0f, phase = 0.0f, earthA = 1.0f, flash = 0.0f, shake = 0.0f, doneT = 0.0f, modeDisp = 1.0f;
    double boomAge = 0.0, lastTime = 0.0, clock = 0.0;
    SceneState state = SceneState::idle;
    std::vector<Particle> parts, fire, sparks;
    struct Ring { float r, a, v; };
    std::vector<Ring> rings;
    struct Rock { float a, rad, sp, r, rot; juce::Colour c; };
    std::vector<Rock> rubble;
    juce::Random rnd { 1234 };

    // interaction
    enum class Drag { none, build, mix } dragging = Drag::none;
    float dragStartY = 0.0f, dragStartValue = 0.0f;
    bool previewMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoonshotEditor)
};
