// Snapshot.cpp — dev tool.
//   FractoneSnapshot panel.png [presetIndex] [scale]     render the panel
//   FractoneSnapshot sound.wav [presetIndex] [seconds]   render audio at 120 BPM (host playing)
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct FakeHost final : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        return info;
    }
};
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc > 1 && juce::String (argv[1]) == "--preset-test")
    {
        int fails = 0;
        auto expect = [&] (bool ok, const char* what) { std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++fails; };
        using namespace fractone;

        auto p = std::make_unique<FractoneProcessor>();
        p->setCurrentProgram (3); // Stumble Down: has cables
        const float cutoff = p->get (ids::cutoff);
        expect (p->sourceFor (inVco1) == outSeq2, "factory preset patches its cables");
        expect (p->presets.saveAs ("Test Patch"), "save user preset");
        expect (PresetManager::presetFolder().getChildFile ("Test Patch.fractone").existsAsFile(), "file written to presets folder");

        p->setCurrentProgram (0);
        expect (p->sourceFor (inVco1) == -1, "Init clears the cables");

        bool loaded = false;
        for (auto& e : p->presets.list())
            if (! e.factory && e.name == "Test Patch")
                loaded = p->presets.load (e);
        expect (loaded, "user preset listed and loaded");
        expect (p->sourceFor (inVco1) == outSeq2 && p->sourceFor (inTrigger) == outSeq1Clk, "user preset restores cables");
        expect (std::abs (p->get (ids::cutoff) - cutoff) < 0.01f, "user preset restores knobs");
        expect (p->presets.currentIsUser() && p->presets.currentName() == "Test Patch", "current preset is the user preset");

        // Instance settings are kept when loading presets.
        if (auto* ch = p->apvts.getParameter (ids::midiChannel)) ch->setValueNotifyingHost (ch->convertTo0to1 (5.0f));
        p->setCurrentProgram (1);
        expect (juce::roundToInt (p->get (ids::midiChannel)) == 5, "preset load keeps MIDI channel");

        // Host session round trip.
        p->presets.loadIndex (p->presets.currentIndex());
        juce::MemoryBlock mb;
        p->setCurrentProgram (5);
        p->getStateInformation (mb);
        auto q = std::make_unique<FractoneProcessor>();
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        expect (q->sourceFor (inVco2) == outSeq1 && q->sourceFor (inCutoff) == outSeq2, "session restore keeps cables");
        expect (q->presets.currentName() == p->presets.currentName(), "session restore keeps preset name");
        expect (std::abs (q->get (ids::resonance) - p->get (ids::resonance)) < 1e-4f, "session restore keeps knobs");

        // A host latches the program count when it loads the plugin. Saving a
        // user preset used to grow that list, so the host clamped to its stale
        // maximum and wrote the index back, loading the last factory preset
        // over whatever had just been saved or chosen.
        {
            auto h = std::make_unique<FractoneProcessor>();
            const int programsAtLoad = h->getNumPrograms();
            expect (programsAtLoad == (int) factoryPresets().size(),
                    "only the factory presets are exposed as host programs");

            h->presets.saveAs ("zzz Host Clamp");
            expect (h->getNumPrograms() == programsAtLoad, "saving a user preset does not change the program count");

            auto hostEcho = [&h, programsAtLoad]
            {
                h->setCurrentProgram (juce::jlimit (0, programsAtLoad - 1, h->getCurrentProgram()));
            };

            hostEcho();
            expect (h->presets.currentName() == "zzz Host Clamp", "the host echo leaves a just-saved user preset loaded");

            h->presets.loadIndex (0);
            int userIdx = -1;
            const auto all = h->presets.list();
            for (int i = 0; i < all.size(); ++i)
                if (! all[i].factory && all[i].name == "zzz Host Clamp")
                    userIdx = i;
            h->presets.loadIndex (userIdx);
            expect (h->presets.currentName() == "zzz Host Clamp", "the panel menu loads a user preset");
            hostEcho();
            expect (h->presets.currentName() == "zzz Host Clamp", "the host echo leaves a chosen user preset loaded");

            h->setCurrentProgram (4);
            expect (h->presets.currentName() == factoryPresets()[4].name, "the host can still select factory programs");
            expect (h->getCurrentProgram() == 4, "the host reads back the factory program it selected");
            expect (h->getProgramName (4) == factoryPresets()[4].name, "program names come from the factory list");

            for (auto& e : h->presets.list())
                if (! e.factory && e.name == "zzz Host Clamp")
                    h->presets.remove (e);
        }

        for (auto& e : p->presets.list())
            if (! e.factory && e.name == "Test Patch")
                expect (p->presets.remove (e), "delete user preset");
        std::printf ("%d failures\n", fails);
        return fails == 0 ? 0 : 1;
    }

    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "panel.png"));
    const int preset = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
    const double extra = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 0.0;

    auto proc = std::make_unique<FractoneProcessor>();
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    proc->setCurrentProgram (preset);

    FakeHost host;
    proc->setPlayHead (&host);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;

    if (out.hasFileExtension ("wav"))
    {
        const double seconds = extra > 0.0 ? extra : 8.0;
        const int blocks = (int) (seconds * 48000.0 / 512.0);
        juce::AudioBuffer<float> all (2, blocks * 512);
        double peak = 0.0, sumSq = 0.0;
        bool finite = true;
        for (int b = 0; b < blocks; ++b)
        {
            proc->processBlock (buf, midi);
            host.ppq += 512.0 * host.bpm / 60.0 / 48000.0;
            for (int c = 0; c < 2; ++c)
                all.copyFrom (c, b * 512, buf, c, 0, 512);
            for (int i = 0; i < 512; ++i)
            {
                const float s = buf.getSample (0, i);
                if (! std::isfinite (s)) finite = false;
                peak = std::max (peak, (double) std::abs (s));
                sumSq += (double) s * s;
            }
        }
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), 48000.0, 2, 24, {}, 0));
        const bool ok = w != nullptr && w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
        w.reset();
        std::printf ("%-18s peak %.3f  rms %.4f  %s  steps %d/%d  %s\n",
                     proc->getProgramName (preset).toRawUTF8(), peak, std::sqrt (sumSq / (blocks * 512.0)),
                     finite ? "finite" : "NAN!", proc->uiStep[0].load(), proc->uiStep[1].load(), ok ? "" : "WRITE FAILED");
        proc.reset();
        return ok && finite ? 0 : 1;
    }

    for (int i = 0; i < 40; ++i)
        proc->processBlock (buf, midi);

    const float scale = extra > 0.0 ? (float) extra : 1.0f;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setSize (juce::roundToInt (1600 * scale), juce::roundToInt (700 * scale));

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    const bool ok = png.writeImageToStream (img, os);
    std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());

    ed.reset();
    proc.reset();
    return ok ? 0 : 1;
}
