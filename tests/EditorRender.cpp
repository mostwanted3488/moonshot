// Renders the editor offscreen at several states and writes PNGs (software renderer, no host needed).
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? argv[1] : "editor_out");
    outDir.createDirectory();

    MoonshotProcessor proc;
    struct Shot { const char* name; float build, mix; int mode; double seconds; };
    const Shot shots[] = {
        { "01_build000_soft", 0.00f, 1.0f, 1, 0.0 }, { "02_build030_soft", 0.30f, 1.0f, 1, 0.0 },
        { "03_build060_hard", 0.60f, 0.7f, 0, 0.0 }, { "04_build085_soft", 0.85f, 1.0f, 1, 0.0 },
        { "05_build100_boom_0p15s", 1.00f, 1.0f, 1, 0.15 }, { "06_build100_boom_0p6s", 1.00f, 1.0f, 1, 0.6 },
        { "07_build100_boom_1p5s", 1.00f, 1.0f, 1, 1.5 }, { "08_build100_aftermath", 1.00f, 1.0f, 0, 4.5 },
        { "09_build010_hard", 0.10f, 1.0f, 0, 0.0 }, { "10_build016_release", 0.16f, 1.0f, 1, 0.0 } };
    int failed = 0;
    for (auto& s : shots)
    {
        MoonshotEditor ed (proc);
        ed.setSize (892, 532);
        ed.setPreviewState (s.build, s.mix, s.mode, s.seconds);
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile (juce::String (s.name) + ".png");
        juce::FileOutputStream out (f); out.setPosition (0); out.truncate();
        juce::PNGImageFormat png;
        if (! png.writeImageToStream (img, out)) { std::printf ("could not write %s\n", s.name); ++failed; }
        else std::printf ("wrote %s (%dx%d)\n", f.getFileName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
    // hi-dpi / resized check
    { MoonshotEditor ed (proc); ed.setSize (1338, 798); ed.setPreviewState (0.6f, 1.0f, 1, 0.0);
      auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
      juce::FileOutputStream out (outDir.getChildFile ("11_resized_1338.png")); out.setPosition (0); out.truncate(); juce::PNGImageFormat().writeImageToStream (img, out); }
    return failed;
}
