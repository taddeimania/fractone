#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/Widgets.h"
#include "UI/Patchbay.h"

// The panel is drawn in a fixed 1600 x 700 design space and scaled to fit.
class FractonePanel final : public juce::Component, private juce::Timer
{
public:
    static constexpr int designW = 1600;
    static constexpr int designH = 700;

    explicit FractonePanel (FractoneProcessor&);
    ~FractonePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    using Knob = fractone::ui::Knob;
    using PanelButton = fractone::ui::PanelButton;
    using Led = fractone::ui::Led;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;

    float getParam (const juce::String& id) const;
    void  setParam (const juce::String& id, float plainValue);
    void  toggleParam (const juce::String& id);

    void addKnob (Knob&, const juce::String& id, juce::Point<float> centre, float size, const juce::String& tip);
    void addDivisorKnob (Knob&, const juce::String& id, juce::Point<float> centre, float size, const juce::String& tip,
                         std::function<int()> effective);
    void addButton (PanelButton&, juce::Point<float> centre, float w, float h, const juce::String& tip);
    void addToggle (PanelButton&, const juce::String& id, juce::Point<float> centre, float w, float h, const juce::String& tip);
    void addLed (Led&, juce::Point<float> centre, float size = 22.0f);

    void buildRail();
    void showPresetMenu();
    void savePresetAs();
    void confirmDelete();

    void paintSections (juce::Graphics&) const;
    void paintWood (juce::Graphics&, juce::Rectangle<float>) const;
    void paintRail (juce::Graphics&) const;

    FractoneProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    fractone::ui::PanelLookAndFeel lnf;

    // Sequencers
    Knob steps[2][4];
    Led  stepLeds[2][4];
    std::unique_ptr<PanelButton> diceBtn[2];

    // Rhythm generators
    Knob rhythm[4];
    std::unique_ptr<PanelButton> rhythmSeq[4][2];

    // Tempo & transport
    Knob tempo { Knob::Kind::large };
    PanelButton resetBtn { "RESET" }, egBtn { "EG" }, nextBtn { "NEXT" }, playBtn { "PLAY" }, triggerBtn { "TRIGGER" };

    // Oscillators
    Knob vcoFreq[2] { Knob { Knob::Kind::large }, Knob { Knob::Kind::large } };
    Knob subFreq[2][2] { { Knob { Knob::Kind::divider }, Knob { Knob::Kind::divider } },
                         { Knob { Knob::Kind::divider }, Knob { Knob::Kind::divider } } };
    std::unique_ptr<fractone::ui::WaveSwitch> waveSwitch[2];
    PanelButton seqOctBtn { "SEQ OCT" };
    Led seqOctLeds[3];

    // Mixer
    std::unique_ptr<PanelButton> assignBtn[2][3];
    Knob vcoLevel[2] { Knob { Knob::Kind::level }, Knob { Knob::Kind::level } };
    Knob subLevel[2][2] { { Knob { Knob::Kind::level }, Knob { Knob::Kind::level } },
                          { Knob { Knob::Kind::level }, Knob { Knob::Kind::level } } };
    PanelButton quantizeBtn { "QUANTIZE" };
    Led quantLeds[4];

    // Filter, VCA, envelopes
    Knob cutoff { Knob::Kind::large }, resonance { Knob::Kind::large }, egAmt { Knob::Kind::large, true }, volume { Knob::Kind::large };
    Knob vcfAttack { Knob::Kind::large }, vcfDecay { Knob::Kind::large }, vcaAttack { Knob::Kind::large }, vcaDecay { Knob::Kind::large };

    // Patchbay
    std::unique_ptr<fractone::ui::Patchbay> patchbay;

    // Utility rail
    PanelButton prevBtn { "<" }, nextPresetBtn { ">" }, saveBtn { "SAVE" };
    fractone::ui::ChoiceDisplay presetDisplay, rateDisplay, channelDisplay;
    PanelButton syncBtn { "SYNC" }, logicBtn { "OR" }, clearBtn { "CLEAR CABLES" };
    Knob fineTune { Knob::Kind::trim, true }, drift { Knob::Kind::trim };
    juce::String statusText;

    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::vector<std::unique_ptr<fractone::ui::InvertedDivisorAttachment>> divAttachments;

    int blinkCounter = 0;
    uint32_t lastFires[4] {};
    float rhythmFlash[4] {};
    uint32_t lastMidi = 0;
    float midiFlash = 0.0f;
    int lastPatchVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractonePanel)
};

class FractoneEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FractoneEditor (FractoneProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FractonePanel panel;
    juce::TooltipWindow tooltips { this, 800 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractoneEditor)
};
