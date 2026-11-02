#include "PluginEditor.h"
#include "BinaryData.h"

using juce::Colour;
using juce::Graphics;
using juce::Path;
using juce::Point;
using juce::Rectangle;

namespace
{
constexpr float W = 892.0f, H = 532.0f;
constexpr float EX = 690.0f, EY = 172.0f, PR = 88.0f;      // planet
constexpr float OX = 230.0f;                               // player
constexpr float REL = 0.16f;                               // build value where the ball leaves the hands
constexpr float R0 = 30.0f;                                // ball radius in the hands
constexpr float kStageToPlayerX = 218.0f / 1.5f, kStageToPlayerY = 77.0f / 1.5f, kPlayerW = 256.0f / 1.5f, kPlayerH = 673.0f / 1.5f;

const Rectangle<float> kBuildKnob { 588.0f, 350.0f, 114.0f, 114.0f };
const Rectangle<float> kMixKnob   { 744.0f, 384.0f,  80.0f,  80.0f };
const Rectangle<float> kWave      { 324.0f, 388.0f, 232.0f,  58.0f };
const Rectangle<float> kHard      { 322.0f, 456.0f, 116.0f,  42.0f };
const Rectangle<float> kSoft      { 442.0f, 458.0f, 116.0f,  42.0f };
const float kHardRot = -0.05f, kSoftRot = 0.045f;

float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }

Point<float> bez (float t)
{
    const Point<float> p0 { OX + 38.0f, 86.0f }, p1 { 450.0f, -10.0f }, p2 { EX, EY };
    const float u = 1.0f - t;
    return { u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y };
}

juce::Image loadImage (const char* name)
{
    int size = 0;
    if (auto* d = BinaryData::getNamedResource (name, size))
        return juce::ImageFileFormat::loadFrom (d, (size_t) size);
    return {};
}

juce::Typeface::Ptr loadFont (const void* d, int n) { return juce::Typeface::createSystemTypefaceFor (d, (size_t) n); }

Colour lerpC (Colour a, Colour b, float t) { return a.interpolatedWith (b, t); }

// shared curve used by the scope: same maths as the audio engine
float scopeCurve (float u, float build, float mode01)
{
    const float c = 0.94f;
    const float hard = moonshot::hardClip (u, c);
    const float soft = moonshot::softClip (u, moonshot::kneeFromBuild (build, c), c);
    return hard + (soft - hard) * mode01;
}

float inSig (float t, float ph2) { return (std::sin (t) + 0.28f * std::sin (2.31f * t + ph2) + 0.12f * std::sin (5.1f * t - ph2)) / 1.4f; }
} // namespace

//==============================================================================
MoonshotEditor::MoonshotEditor (MoonshotProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      buildAtt (*p.apvts.getParameter ("build"), [this] (float v) { vBuild = v; }),
      mixAtt   (*p.apvts.getParameter ("mix"),   [this] (float v) { vMix = v; }),
      modeAtt  (*p.apvts.getParameter ("mode"),  [this] (float v) { modeIndex = juce::roundToInt (v); })
{
    bg       = loadImage ("bg_jpg");
    earthImg = loadImage ("earth_png");
    sawImg   = loadImage ("saw_png");
    ballImg  = loadImage ("ball_png");
    playerFrames.resize (201);

    if (earthImg.isValid())
    {
        earthTint = earthImg.createCopy();
        juce::Image::BitmapData bd (earthTint, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < bd.height; ++y)
            for (int x = 0; x < bd.width; ++x)
            {
                auto* px = (juce::PixelARGB*) bd.getPixelPointer (x, y);
                const auto a = px->getAlpha();
                px->setARGB (a, 224, 24, 24);
                px->premultiply();
            }
    }

    tfAnton   = loadFont (BinaryData::AntonRegular_ttf, BinaryData::AntonRegular_ttfSize);
    tfGrunge  = loadFont (BinaryData::MoonGrunge_ttf, BinaryData::MoonGrunge_ttfSize);

    buildAtt.sendInitialUpdate();
    mixAtt.sendInitialUpdate();
    modeAtt.sendInitialUpdate();
    dv = vBuild;
    modeDisp = (float) modeIndex;

    setOpaque (true);
    setResizeLimits (446, 266, 1784, 1064);
    getConstrainer()->setFixedAspectRatio ((double) W / (double) H);
    setSize ((int) W, (int) H);

    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

MoonshotEditor::~MoonshotEditor() { stopTimer(); }

void MoonshotEditor::resized() { scale = (float) getWidth() / W; }

//==============================================================================
// state machine (mirrors the browser preview)
void MoonshotEditor::triggerBoom()
{
    state = SceneState::boom; boomAge = 0; flash = 1.0f; shake = 18.0f;
    parts.clear(); fire.clear(); sparks.clear(); rings.clear();
    const Colour cols[] { Colour (0xff2b2420), Colour (0xff4a3a30), Colour (0xff6b5848), Colour (0xff1a1512), Colour (0xff8a4a20), Colour (0xffc9712a) };
    auto rf = [this] (float a, float b) { return a + rnd.nextFloat() * (b - a); };
    for (int i = 0; i < 34; ++i) { const float a = rf (0, 6.28f), sp = rf (80, 460);
        parts.push_back ({ EX, EY, std::cos (a) * sp, std::sin (a) * sp, 0, rf (1.4f, 2.6f), rf (5, 15), rf (0, 6), rf (-4.5f, 4.5f), cols[i % 6] }); }
    for (int i = 0; i < 110; ++i) { const float a = rf (0, 6.28f), sp = std::pow (rnd.nextFloat(), 0.6f) * 300.0f;
        fire.push_back ({ EX, EY, std::cos (a) * sp, std::sin (a) * sp - 30, -rf (0, 0.15f), rf (1.0f, 2.2f), rf (30, 80), 0, 0, Colour(), false }); }
    for (int i = 0; i < 40; ++i) { const float a = rf (0, 6.28f), sp = rf (60, 250);
        fire.push_back ({ EX, EY, std::cos (a) * sp, std::sin (a) * sp - 60, -0.1f - rf (0, 0.3f), rf (1.8f, 2.8f), rf (40, 90), 0, 0, Colour(), true }); }
    for (int i = 0; i < 90; ++i) { const float a = rf (0, 6.28f), sp = rf (250, 950);
        sparks.push_back ({ EX, EY, std::cos (a) * sp, std::sin (a) * sp, 0, rf (0.5f, 1.4f), 0, 0, 0, Colour(), false }); }
    rings.push_back ({ 0, 1, 820 });
}

void MoonshotEditor::makeRubble()
{
    const Colour cols[] { Colour (0xff2b2420), Colour (0xff4a3a30), Colour (0xff6b5848), Colour (0xff1a1512), Colour (0xff8a4a20), Colour (0xffc9712a) };
    rubble.clear();
    for (int i = 0; i < 22; ++i)
        rubble.push_back ({ rnd.nextFloat() * 6.28f, 42.0f + rnd.nextFloat() * 78.0f, (0.25f + rnd.nextFloat() * 0.5f) * (rnd.nextBool() ? 1.0f : -1.0f),
                            5.0f + rnd.nextFloat() * 9.0f, rnd.nextFloat() * 6.0f, cols[i % 6] });
    doneT = 0;
}

void MoonshotEditor::advance (float dt)
{
    clock += dt;
    spin += dt * 0.5f;
    phase += dt * 1.6f;
    dv += (vBuild - dv) * juce::jmin (1.0f, dt * 9.0f);
    if (std::abs (vBuild - dv) < 0.0005f) dv = vBuild;
    modeDisp += ((float) modeIndex - modeDisp) * juce::jmin (1.0f, dt * 14.0f);

    auto startReform = [this] { state = SceneState::reform; earthA = 0; parts.clear(); rings.clear(); fire.clear(); sparks.clear(); rubble.clear(); flash = 0; shake = 0; };

    if (state == SceneState::boom)
    {
        boomAge += dt;
        if (boomAge > 0.06) earthA = 0;
        for (auto& p : parts) { p.age += dt; p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= 0.99f; p.vy *= 0.99f; p.rot += p.vr * dt; }
        for (auto& f : fire) { f.age += dt; if (f.age > 0) { f.x += f.vx * dt; f.y += f.vy * dt; f.vx *= 0.97f; f.vy = f.vy * 0.97f - (f.smoke ? 26.0f : 50.0f) * dt; } }
        for (auto& s : sparks) { s.age += dt; s.x += s.vx * dt; s.y += s.vy * dt + 60.0f * dt * s.age; s.vx *= 0.985f; s.vy *= 0.985f; }
        for (auto& r : rings) { r.r += r.v * dt; r.a = juce::jmax (0.0f, r.a - dt * 1.1f); }
        flash = juce::jmax (0.0f, flash - dt * 2.6f);
        shake = juce::jmax (0.0f, shake - dt * 14.0f);
        if (vBuild < 0.97f) startReform();
        else if (boomAge > 2.9) { state = SceneState::done; parts.clear(); rings.clear(); fire.clear(); sparks.clear(); makeRubble(); }
    }
    else if (state == SceneState::done)
    {
        doneT += dt;
        for (auto& r : rubble) r.a += r.sp * dt;
        if (vBuild < 0.97f) startReform();
    }
    else if (state == SceneState::reform)
    {
        earthA = juce::jmin (1.0f, earthA + dt / 0.6f);
        if (earthA >= 1.0f) state = SceneState::idle;
    }
    if ((state == SceneState::idle || state == SceneState::reform) && vBuild >= 0.999f && dv >= 0.985f) triggerBoom();
}

void MoonshotEditor::timerCallback()
{
    if (previewMode) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = juce::jlimit (0.0f, 0.1f, (float) ((now - lastTime) / 1000.0));
    lastTime = now;
    advance (dt);
    repaint();
}

void MoonshotEditor::setPreviewState (float build, float mix, int mode, double seconds)
{
    previewMode = true; stopTimer();
    vBuild = build; vMix = mix; modeIndex = mode; modeDisp = (float) mode; dv = build;
    state = SceneState::idle; earthA = 1.0f;
    for (double t = 0; t < seconds; t += 1.0 / 30.0) advance (1.0f / 30.0f);
}

//==============================================================================
// text helpers
Path MoonshotEditor::makeTextPath (const juce::Typeface::Ptr& tf, const juce::String& s, float em, float ls) const
{
    juce::Font f { juce::FontOptions (tf).withPointHeight (em) };
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, s, 0.0f, 0.0f);
    for (int i = 1; i < ga.getNumGlyphs(); ++i) ga.moveRangeOfGlyphs (i, 1, ls * (float) i, 0.0f);
    Path p; ga.createPath (p);
    return p;
}

void MoonshotEditor::drawTextPath (Graphics& g, Path p, Point<float> anchor, int align, Colour fill, float outline, float rotateDeg) const
{
    const auto b = p.getBounds();
    float dx = anchor.x - b.getX();
    if (align == 0) dx = anchor.x - b.getCentreX();
    else if (align > 0) dx = anchor.x - b.getRight();
    auto t = juce::AffineTransform::translation (dx, anchor.y);
    if (std::abs (rotateDeg) > 1.0e-6f) t = t.rotated (juce::degreesToRadians (rotateDeg), anchor.x, anchor.y);
    p.applyTransform (t);
    if (outline > 0.0f) { g.setColour (juce::Colours::black); g.strokePath (p, juce::PathStrokeType (outline * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    g.setColour (fill);
    g.fillPath (p);
}

void MoonshotEditor::paintTexts (Graphics& g)
{
    // title (rotated -3 deg around its left-middle), copyright, percentage, knob labels
    drawTextPath (g, makeTextPath (tfGrunge, "MOONSHOT", 62.0f, 2.0f), { 30.0f, 24.0f + 0.83f * 62.0f }, -1, Colour (0xffe8e4d8), 3.0f, -3.0f);
    drawTextPath (g, makeTextPath (tfGrunge, juce::String (juce::CharPointer_UTF8 ("\xc2\xa9 BUYORBEALOSER")), 17.0f, 1.0f), { 16.0f, 3.0f + 0.86f * 17.0f }, -1, juce::Colours::white, 2.0f);
    drawTextPath (g, makeTextPath (tfGrunge, juce::String (juce::roundToInt (vBuild * 100.0f)).paddedLeft ('0', 3) + "%", 40.0f, 1.0f), { W - 30.0f, 12.0f + 0.83f * 40.0f }, 1, Colour (0xffc9d6a3), 3.0f);
    drawTextPath (g, makeTextPath (tfGrunge, "BUILD", 25.0f, 2.0f), { kBuildKnob.getCentreX(), 468.0f + 0.86f * 25.0f }, 0, Colour (0xffe8e4d8), 2.0f);
    drawTextPath (g, makeTextPath (tfGrunge, "MIX", 25.0f, 2.0f), { kMixKnob.getCentreX(), 468.0f + 0.86f * 25.0f }, 0, Colour (0xffe8e4d8), 2.0f);
    drawTextPath (g, makeTextPath (tfGrunge, juce::String (juce::roundToInt (vMix * 100.0f)) + "%", 22.0f, 1.0f), { 834.0f, 412.0f + 0.86f * 22.0f }, -1, Colour (0xffc9d6a3), 2.0f);
}

//==============================================================================
juce::Image MoonshotEditor::getPlayerFrame (int index)
{
    index = juce::jlimit (0, 200, index);
    if (! playerFrames[(size_t) index].isValid())
        playerFrames[(size_t) index] = loadImage (("player_" + juce::String (index).paddedLeft ('0', 3) + "_png").toRawUTF8());
    return playerFrames[(size_t) index];
}

void MoonshotEditor::paintEarth (Graphics& g, float alpha, float warnIn)
{
    if (alpha <= 0.0f || ! earthImg.isValid()) return;
    const float warn = std::pow (clamp01 (warnIn), 1.3f);
    float jx = 0, jy = 0;
    if (state == SceneState::idle && warnIn > 0.5f) { jx = (rnd.nextFloat() - 0.5f) * warnIn * 5.0f; jy = (rnd.nextFloat() - 0.5f) * warnIn * 5.0f; }
    const float pulse = 1.0f + warn * 0.07f * (0.5f + 0.5f * std::sin (spin * (8.0f + warn * 14.0f)));
    const float rot = spin * 0.15f + warn * 0.06f * std::sin (spin * 11.0f);
    const float k = (2.0f * PR / (float) earthImg.getWidth()) * pulse;
    const auto t = juce::AffineTransform::translation (-(float) earthImg.getWidth() * 0.5f, -(float) earthImg.getHeight() * 0.5f).scaled (k).rotated (rot).translated (EX + jx, EY + jy);

    g.saveState();
    g.setOpacity (alpha);
    g.drawImageTransformed (earthImg, t, false);
    if (warn > 0.0f && earthTint.isValid()) { g.setOpacity (alpha * warn * 0.6f); g.drawImageTransformed (earthTint, t, false); }
    if (warn > 0.0f)
    {
        const float R = PR * 1.5f;
        juce::ColourGradient grad (Colour::fromFloatRGBA (1.0f, 0.2f, 0.12f, 0.0f), EX, EY, Colour::fromFloatRGBA (1.0f, 0.2f, 0.12f, 0.0f), EX + R, EY, true);
        grad.addColour (0.34, Colour::fromFloatRGBA (1.0f, 0.2f, 0.12f, 0.0f));
        grad.addColour (0.79, Colour::fromFloatRGBA (1.0f, 0.2f, 0.12f, 0.4f * warn * alpha));
        g.setGradientFill (grad);
        g.fillEllipse (EX - R, EY - R, 2 * R, 2 * R);
    }
    g.restoreState();
}

void MoonshotEditor::paintWave (Graphics& g)
{
    const float x = kWave.getX(), y = kWave.getY(), w = kWave.getWidth(), h = kWave.getHeight();
    const float mid = y + h * 0.5f, amp = h * 0.5f, gain = juce::Decibels::decibelsToGain (moonshot::buildToDriveDb (dv));
    const float hot = clamp01 ((dv - 0.3f) / 0.55f);
    const Colour blue = lerpC (Colour (60, 125, 255), Colour (255, 80, 50), hot * 0.85f);
    // dark halo
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (1.0f, 0.36f, x + w * 0.5f, mid));
    juce::ColourGradient halo (Colour::fromFloatRGBA (0, 0, 0, 0.85f), x + w * 0.5f, mid, Colour::fromFloatRGBA (0, 0, 0, 0.0f), x + w * 0.5f + w * 0.62f, mid, true);
    halo.addColour (0.7, Colour::fromFloatRGBA (0, 0, 0, 0.55f));
    g.setGradientFill (halo);
    g.fillEllipse (x + w * 0.5f - w * 0.62f, mid - w * 0.62f, w * 1.24f, w * 1.24f);
    g.restoreState();

    const float N = 2.4f, IN = 0.34f;
    auto makePath = [&] (float ph, float gn, float ph2)
    {
        Path p;
        for (int i = 0; i <= (int) w; i += 2)
        {
            const float u = (float) i / w, t = u * N * 6.283f + ph;
            const float env = std::pow (juce::jmax (0.0f, std::sin (3.14159265f * u)), 0.55f);   // never feed pow() a tiny negative
            const float v = scopeCurve (IN * inSig (t, ph2) * env * gn, dv, modeDisp);
            const float px = x + (float) i, py = mid - v * amp * 0.95f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        return p;
    };
    auto stroke = [&] (const Path& p, Colour c, float lw) { g.setColour (c); g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); };

    // faint ceiling guides
    g.setColour (blue.withAlpha (0.35f));
    for (int sg = -1; sg <= 1; sg += 2) for (float xx = x; xx < x + w; xx += 9.0f) g.fillRect (xx, mid + (float) sg * amp * 0.9f - 0.6f, 2.0f, 1.2f);

    // ghost trails
    for (int k = 6; k >= 1; --k)
    {
        auto p = makePath (phase - (float) k * 0.26f, gain * (1.0f - 0.07f * (float) k) * (1.0f + 0.05f * std::sin ((float) clock * 2.0f + (float) k)), (float) k * 0.7f);
        const float a = 0.34f / std::pow ((float) k, 0.75f);
        stroke (p, blue.withAlpha (a * 0.35f), 9.0f + (float) k * 2.0f);
        stroke (p, blue.withAlpha (a), 3.0f + (float) k * 1.2f);
    }
    auto main = makePath (phase, gain, 0.0f);
    stroke (main, blue.withAlpha (0.16f), 18.0f);
    stroke (main, blue.withAlpha (0.30f), 12.0f);
    stroke (main, blue.withAlpha (0.75f), 6.5f);
    auto second = makePath (phase + 0.42f, gain * 0.92f, 1.3f);
    stroke (second, lerpC (juce::Colours::white, Colour (255, 235, 225), hot).withAlpha (0.7f), 1.2f);
    stroke (main, juce::Colours::white.withAlpha (0.35f), 5.5f);
    stroke (main, lerpC (juce::Colours::white, Colour (255, 235, 225), hot), 2.6f);
}

void MoonshotEditor::paintSticker (Graphics& g, Rectangle<float> r, float rot, bool on, const juce::String& label, const juce::String& tiny)
{
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (rot, r.getCentreX(), r.getCentreY()));
    const float rx = r.getWidth() * 0.5f, ry = r.getHeight() * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
    auto ell = [&] (float inset, float dx = 0, float dy = 0) { return Rectangle<float> (cx - rx + inset + dx, cy - ry + inset + dy, (rx - inset) * 2, (ry - inset) * 2); };

    g.setColour (Colour::fromFloatRGBA (0, 0, 0, 0.55f)); g.fillEllipse (ell (0, 2.5f, 3.5f));
    juce::ColourGradient body (on ? Colour (0xffffe321) : Colour (0xff9c8d16), cx, cy - ry, on ? Colour (0xfff0c800) : Colour (0xff6e640f), cx, cy + ry, false);
    g.setGradientFill (body); g.fillEllipse (ell (0));
    g.setColour (on ? Colour (0xff0b0a05) : juce::Colours::black); g.drawEllipse (ell (0), 1.6f);
    const Colour ink = on ? Colour (0xff0b0a05) : Colour::fromFloatRGBA (0, 0, 0, 0.8f);
    g.setColour (ink); g.drawEllipse (ell (4.5f), 3.4f); g.drawEllipse (ell (9.0f), 1.1f);

    // peeled-sticker sheen
    g.saveState();
    Path clip; clip.addEllipse (ell (1.0f)); g.reduceClipRegion (clip);
    juce::ColourGradient sheen (Colour::fromFloatRGBA (1, 1, 1, on ? 0.28f : 0.08f), cx - rx, cy - ry, Colour::fromFloatRGBA (0, 0, 0, 0.18f), cx + rx, cy + ry, false);
    sheen.addColour (0.45, Colour::fromFloatRGBA (1, 1, 1, 0.0f));
    g.setGradientFill (sheen); g.fillRect (r.expanded (4));
    g.restoreState();

    // big, tight, stretched label (Anton)
    Path tp = makeTextPath (tfAnton, label, 34.0f, -1.5f);
    const auto tb = tp.getBounds();
    const float target = r.getWidth() * 0.8f, sx = target / juce::jmax (1.0f, tb.getWidth()), sy = 0.7f;
    tp.applyTransform (juce::AffineTransform::translation (-tb.getCentreX(), -tb.getCentreY()).scaled (sx, sy).translated (cx, cy - 1.5f));
    g.setColour (on ? Colour (0xff0b0a05) : Colour::fromFloatRGBA (0, 0, 0, 0.85f)); g.fillPath (tp);

    // optional tiny line (unused now)
    if (tiny.isNotEmpty())
    {
    Path tiny_ = makeTextPath (tfAnton, tiny, 5.0f, 0.6f);
    const auto b2 = tiny_.getBounds();
    tiny_.applyTransform (juce::AffineTransform::translation (-b2.getRight(), 0.0f).translated (cx + rx - 22.0f, cy + ry - 12.5f + 2.0f));
    g.setColour (on ? Colour (0xff0b0a05) : Colour::fromFloatRGBA (0, 0, 0, 0.7f)); g.fillPath (tiny_);
    }
    g.restoreState();
}

void MoonshotEditor::paintKnob (Graphics& g, Rectangle<float> box, float value)
{
    const float k = box.getWidth() / 100.0f, ang = juce::degreesToRadians (-135.0f + 270.0f * value);
    g.setColour (Colour::fromFloatRGBA (0, 0, 0, 0.55f));
    g.fillEllipse (box.getX() + 10.0f * k, box.getY() + 89.0f * k, 84.0f * k, 12.0f * k);
    if (sawImg.isValid())
    {
        g.setOpacity (1.0f);
        const auto t = juce::AffineTransform::translation (-(float) sawImg.getWidth() * 0.5f, -(float) sawImg.getHeight() * 0.5f)
                           .scaled (98.0f * k / (float) sawImg.getWidth()).rotated (ang).translated (box.getCentreX(), box.getCentreY());
        g.drawImageTransformed (sawImg, t, false);
    }
    Path m; m.startNewSubPath (50, 14); m.lineTo (46, 25); m.lineTo (54, 25); m.closeSubPath();
    m.applyTransform (juce::AffineTransform::rotation (ang, 50, 50).scaled (k).translated (box.getX(), box.getY()));
    g.setColour (Colour (0xffc9d6a3)); g.fillPath (m);
    g.setColour (juce::Colours::black); g.strokePath (m, juce::PathStrokeType (1.4f * k));
}

void MoonshotEditor::paintBoom (Graphics& g, float age)
{
    const float fade = age > 2.0f ? juce::jmax (0.0f, 1.0f - (age - 2.0f) / 0.8f) : 1.0f;
    for (auto& r : rings) { g.setColour (Colour (0xfffff3d0).withAlpha (r.a * fade)); g.drawEllipse (EX - r.r, EY - r.r, 2 * r.r, 2 * r.r, 5.0f); }
    auto radial = [&] (float x, float y, float r, Colour c0, Colour c1, float midPos = -1.0f, Colour cm = Colour())
    {
        juce::ColourGradient grad (c0, x, y, c1, x + r, y, true);
        if (midPos > 0) grad.addColour (midPos, cm);
        g.setGradientFill (grad); g.fillEllipse (x - r, y - r, 2 * r, 2 * r);
    };
    for (auto& f : fire) if (f.smoke)
    {
        const float t = f.age / f.life; if (t >= 1.0f || t < 0.0f) continue;
        const float r = f.r * (1.0f + t * 1.8f), a = std::sin (juce::jmin (1.0f, t * 1.2f) * 3.1416f * 0.9f) * 0.5f * fade;
        radial (f.x, f.y, r, Colour::fromFloatRGBA (40 / 255.0f, 28 / 255.0f, 24 / 255.0f, a), Colour::fromFloatRGBA (40 / 255.0f, 28 / 255.0f, 24 / 255.0f, 0.0f));
    }
    for (auto& f : fire) if (! f.smoke)
    {
        const float t = f.age / f.life; if (t >= 1.0f || t < 0.0f) continue;
        const float r = f.r * (0.5f + t * 1.1f);
        const float col[3] = { t < .2f ? 255.f : t < .45f ? 255.f : t < .75f ? 240.f : 120.f, t < .2f ? 244.f : t < .45f ? 190.f : t < .75f ? 90.f : 25.f, t < .2f ? 200.f : t < .45f ? 60.f : t < .75f ? 20.f : 8.f };
        const float a = (1 - t) * (1 - t) * 0.95f * fade;
        radial (f.x, f.y, r, Colour::fromFloatRGBA (col[0] / 255, col[1] / 255, col[2] / 255, a), Colour::fromFloatRGBA (200 / 255.f, 40 / 255.f, 0, 0), 0.55f,
                Colour::fromFloatRGBA (col[0] / 255, col[1] * 0.6f / 255, col[2] * 0.4f / 255, a * 0.55f));
    }
    const float ca = juce::jmax (0.0f, 1.0f - age / 0.7f);
    if (ca > 0.0f) radial (EX, EY, 40.0f + age * 260.0f, Colour::fromFloatRGBA (1, 1, 240 / 255.f, ca), Colour::fromFloatRGBA (1, 90 / 255.f, 0, 0), 0.35f, Colour::fromFloatRGBA (1, 210 / 255.f, 100 / 255.f, ca * 0.8f));
    for (auto& s : sparks)
    {
        const float t = s.age / s.life; if (t >= 1.0f) continue;
        g.setColour (Colour::fromFloatRGBA (1.0f, (220.0f - 120.0f * t) / 255.0f, (120.0f - 100.0f * t) / 255.0f, (1 - t) * fade));
        g.drawLine (s.x, s.y, s.x - s.vx * 0.045f, s.y - s.vy * 0.045f, 2.2f * (1 - t) + 0.6f);
    }
    for (auto& p : parts)
    {
        const float a = juce::jmax (0.0f, 1.0f - p.age / p.life) * fade; if (a <= 0) continue;
        Path q; q.startNewSubPath (-p.r, -p.r * .6f); q.lineTo (p.r, -p.r * .3f); q.lineTo (p.r * .5f, p.r * .7f); q.lineTo (-p.r * .7f, p.r * .5f); q.closeSubPath();
        q.applyTransform (juce::AffineTransform::rotation (p.rot).translated (p.x, p.y));
        g.setColour (p.c.withAlpha (a)); g.fillPath (q);
        g.setColour (juce::Colours::black.withAlpha (a)); g.strokePath (q, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved));
    }
}

void MoonshotEditor::paintRubble (Graphics& g)
{
    const float al = juce::jmin (1.0f, doneT / 0.8f);
    for (auto& p : rubble)
    {
        Path q; q.startNewSubPath (-p.r, -p.r * .6f); q.lineTo (p.r, -p.r * .3f); q.lineTo (p.r * .5f, p.r * .7f); q.lineTo (-p.r * .7f, p.r * .5f); q.closeSubPath();
        q.applyTransform (juce::AffineTransform::rotation (p.rot + p.a * 2.0f).translated (EX + std::cos (p.a) * p.rad, EY + std::sin (p.a) * p.rad * 0.6f));
        g.setColour (p.c.withAlpha (al)); g.fillPath (q);
        g.setColour (juce::Colours::black.withAlpha (al)); g.strokePath (q, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved));
    }
}

//==============================================================================
void MoonshotEditor::paintScene (Graphics& g)
{
    g.setOpacity (1.0f);
    if (bg.isValid()) g.drawImage (bg, 0, 0, (int) W, (int) H, 0, 0, bg.getWidth(), bg.getHeight()); else g.fillAll (juce::Colours::black);

    paintEarth (g, earthA, (state == SceneState::idle || state == SceneState::reform) ? dv : 0.0f);
    if (state == SceneState::done) paintRubble (g);

    paintWave (g);
    paintSticker (g, kHard, kHardRot, modeIndex == 0, "HARD", {});
    paintSticker (g, kSoft, kSoftRot, modeIndex == 1, "SOFT", {});

    auto frame = getPlayerFrame (juce::roundToInt (dv * 200.0f));
    g.setOpacity (1.0f);
    if (frame.isValid()) g.drawImage (frame, Rectangle<float> (kStageToPlayerX, kStageToPlayerY, kPlayerW, kPlayerH));

    if ((state == SceneState::idle || state == SceneState::reform) && dv >= REL && ballImg.isValid())
    {
        const float t = (dv - REL) / (1.0f - REL), r = R0 * (1.0f - 0.5f * t);
        const auto p = bez (t);
        const float k = r / R0 * 0.5f;     // sprite is 160 px for an 80 css-px ball
        g.setOpacity (1.0f);
        const auto tr = juce::AffineTransform::translation (-(float) ballImg.getWidth() * 0.5f, -(float) ballImg.getHeight() * 0.5f).scaled (k)
                            .rotated ((float) clock * 4.17f + t * 10.0f).translated (p.x, p.y);
        g.drawImageTransformed (ballImg, tr, false);
    }
    if (state == SceneState::boom) paintBoom (g, (float) boomAge);
}

void MoonshotEditor::paint (Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.addTransform (juce::AffineTransform::scale (scale));

    g.saveState();
    if (shake > 0.0f) g.addTransform (juce::AffineTransform::translation ((rnd.nextFloat() - 0.5f) * shake, (rnd.nextFloat() - 0.5f) * shake));
    g.reduceClipRegion (-40, -40, (int) W + 80, (int) H + 80);
    paintScene (g);
    if (flash > 0.0f) { g.setColour (Colour::fromFloatRGBA (1.0f, 0.957f, 0.84f, juce::jmin (1.0f, flash) * 0.95f)); g.fillRect (-40.0f, -40.0f, W + 80.0f, H + 80.0f); }
    g.restoreState();

    paintTexts (g);
    paintKnob (g, kBuildKnob, vBuild);
    paintKnob (g, kMixKnob, vMix);
}

//==============================================================================
void MoonshotEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = toStage (e.position);
    auto inSticker = [&] (Rectangle<float> r, float rot) { return Rectangle<float> (r).expanded (2.0f).contains (p.transformedBy (juce::AffineTransform::rotation (-rot, r.getCentreX(), r.getCentreY()))); };
    if (inSticker (kHard, kHardRot)) { modeAtt.setValueAsCompleteGesture (0.0f); return; }
    if (inSticker (kSoft, kSoftRot)) { modeAtt.setValueAsCompleteGesture (1.0f); return; }
    if (kBuildKnob.getCentre().getDistanceFrom (p) <= kBuildKnob.getWidth() * 0.5f) { dragging = Drag::build; dragStartValue = vBuild; buildAtt.beginGesture(); }
    else if (kMixKnob.getCentre().getDistanceFrom (p) <= kMixKnob.getWidth() * 0.5f) { dragging = Drag::mix; dragStartValue = vMix; mixAtt.beginGesture(); }
    else return;
    dragStartY = p.y;
}

void MoonshotEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Drag::none) return;
    const float y = toStage (e.position).y;
    if (dragging == Drag::build) { vBuild = clamp01 (dragStartValue + (dragStartY - y) / 230.0f); buildAtt.setValueAsPartOfGesture (vBuild); }
    else                         { vMix   = clamp01 (dragStartValue + (dragStartY - y) / 160.0f); mixAtt.setValueAsPartOfGesture (vMix); }
}

void MoonshotEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Drag::build) buildAtt.endGesture();
    else if (dragging == Drag::mix) mixAtt.endGesture();
    dragging = Drag::none;
}

void MoonshotEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = toStage (e.position);
    if (kBuildKnob.getCentre().getDistanceFrom (p) <= kBuildKnob.getWidth() * 0.5f) { vBuild = 0.0f; buildAtt.setValueAsCompleteGesture (0.0f); }
    else if (kMixKnob.getCentre().getDistanceFrom (p) <= kMixKnob.getWidth() * 0.5f) { vMix = 1.0f; mixAtt.setValueAsCompleteGesture (1.0f); }
}
