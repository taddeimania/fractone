#include "PluginEditor.h"

using namespace fractone;
using namespace fractone::ui;
using juce::Point;
using juce::Rectangle;
using juce::String;

namespace
{
// The layout is measured on a reference drawing of the panel and scaled
// into the design space, so controls sit where a player expects them.
constexpr float kS = 1.19f;
Point<float> R (float x, float y) { return { 30.0f + (x - 118.0f) * kS, 18.0f + (y - 185.0f) * kS }; }
Rectangle<float> RB (float x0, float y0, float x1, float y1) { return { R (x0, y0), R (x1, y1) }; }

constexpr float kRailTop = 630.0f;

const char* kInputTips[] = {
    "1 V/oct pitch CV for VCO 1, added to its FREQ knob and Sequencer 1. Normalled to VCO 2's input too.",
    "Selects VCO 1's subharmonic divisors (1-16). Centre the SUB FREQ knobs for +/- control.",
    "Pulse width for VCO 1 and its subs (square waves). Replaces the SUB 1 saw normal in the middle WAVE position.",
    "Adds to the VCA envelope: raises or lowers the volume (0 to +8 V).",
    "1 V/oct pitch CV for VCO 2. Patching here breaks the normal from VCO 1's input.",
    "Selects VCO 2's subharmonic divisors (1-16).",
    "Pulse width for VCO 2 and its subs (square waves).",
    "Cutoff CV, 1 V/oct: +/-5 V sweeps +/-5 octaves.",
    "Gate: rising edge starts PLAY, falling edge stops it.",
    "Trigger resets the sequencers and rhythm generators; a held gate holds step 1.",
    "Rising edge fires both envelopes (ignored while they are in their attack).",
    "Sets Rhythm 1's divisor (centre the RHYTHM 1 knob for the best range).",
    "Sets Rhythm 2's divisor.",
    "Sets Rhythm 3's divisor.",
    "Sets Rhythm 4's divisor.",
    "External clock: each rising edge is a clock pulse. Overrides TEMPO and host sync."
};

const char* kOutputTips[] = {
    "Final output after the VCA.",
    "VCO 1 audio.", "VCO 1 subharmonic 1 audio.", "VCO 1 subharmonic 2 audio.",
    "VCA envelope, 0 to +8 V.",
    "VCO 2 audio.", "VCO 2 subharmonic 1 audio.", "VCO 2 subharmonic 2 audio.",
    "VCF envelope, 0 to +8 V.",
    "Sequencer 1 step CV (follows QUANTIZE and SEQ OCT).", "A pulse each time Sequencer 1 advances.",
    "Sequencer 2 step CV.", "A pulse each time Sequencer 2 advances.",
    "Master clock pulses (only while playing).",
    "A 1 ms pulse every time the envelopes are triggered."
};

std::vector<JackInfo> makeJacks()
{
    const float xs[] { 1226.0f, 1272.0f, 1318.0f, 1364.0f };
    const float ys[] { 270.0f, 322.0f, 375.0f, 428.0f, 481.0f, 533.0f, 586.0f, 639.0f };
    struct Row { const char* label; bool out; int idx; };
    const Row rows[8][4] {
        { { "VCO 1", false, inVco1 }, { "VCO 1 SUB", false, inVco1Sub }, { "VCO 1 PWM", false, inVco1Pwm }, { "VCA", true, outVca } },
        { { "VCO 1", true, outVco1 }, { "VCO 1 SUB 1", true, outVco1Sub1 }, { "VCO 1 SUB 2", true, outVco1Sub2 }, { "VCA", false, inVca } },
        { { "VCO 2", false, inVco2 }, { "VCO 2 SUB", false, inVco2Sub }, { "VCO 2 PWM", false, inVco2Pwm }, { "VCA EG", true, outVcaEg } },
        { { "VCO 2", true, outVco2 }, { "VCO 2 SUB 1", true, outVco2Sub1 }, { "VCO 2 SUB 2", true, outVco2Sub2 }, { "CUTOFF", false, inCutoff } },
        { { "PLAY", false, inPlay }, { "RESET", false, inReset }, { "TRIGGER", false, inTrigger }, { "VCF EG", true, outVcfEg } },
        { { "RHYTHM 1", false, inRhythm1 }, { "RHYTHM 2", false, inRhythm2 }, { "RHYTHM 3", false, inRhythm3 }, { "RHYTHM 4", false, inRhythm4 } },
        { { "SEQ 1", true, outSeq1 }, { "SEQ 1 CLK", true, outSeq1Clk }, { "SEQ 2", true, outSeq2 }, { "SEQ 2 CLK", true, outSeq2Clk } },
        { { "MIDI IN", false, -1 }, { "CLOCK", false, inClock }, { "CLOCK", true, outClock }, { "TRIGGER", true, outTrigger } },
    };

    std::vector<JackInfo> jacks;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 4; ++c)
        {
            const auto& row = rows[r][c];
            const char* tip = row.idx < 0 ? "Notes and CCs from the track arrive here: notes transpose both VCOs from middle C and gate the envelopes."
                                          : (row.out ? kOutputTips[row.idx] : kInputTips[row.idx]);
            jacks.push_back ({ row.label, row.out, row.idx, R (xs[c], ys[r]), tip });
        }
    return jacks;
}
} // namespace

// ---------------------------------------------------------------------------
FractonePanel::FractonePanel (FractoneProcessor& p) : proc (p), apvts (p.apvts)
{
    setLookAndFeel (&lnf);
    setSize (designW, designH);
    setOpaque (true);

    // --- Sequencers ----------------------------------------------------------------
    const float stepX[] { 185.0f, 256.0f, 327.0f, 397.0f };
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < 4; ++i)
        {
            auto& k = steps[s][i];
            k.kind = Knob::Kind::step;
            k.bipolar = true;
            const float y = s == 0 ? 251.0f : 345.0f;
            addKnob (k, ids::step (s, i), R (stepX[i], y), 48.0f,
                     "Sequencer " + String (s + 1) + ", step " + String (i + 1) + ". Centre = no change. "
                     "Range follows SEQ OCT; snaps to the scale when QUANTIZE is on.");
            k.textFromValueFunction = [this] (double v)
            {
                const int range = seqOctRange (juce::roundToInt (getParam (ids::seqOct)));
                const auto q = (Quant) juce::roundToInt (getParam (ids::quantize));
                const double oct = quantizeOctaves (v * range, q);
                if (q == Quant::off)
                    return (oct >= 0 ? "+" : "") + String (oct, 2) + " oct";
                const int semis = juce::roundToInt (oct * 12.0);
                const double cents = (oct * 12.0 - semis) * 100.0;
                String t = (semis >= 0 ? "+" : "") + String (semis) + " st";
                if (std::abs (cents) >= 1.0)
                    t << (cents > 0 ? " +" : " ") << juce::roundToInt (cents) << "c";
                return t;
            };
            addLed (stepLeds[s][i], R (stepX[i], s == 0 ? 289.0f : 383.0f), 22.0f);
        }

    for (int s = 0; s < 2; ++s)
    {
        diceBtn[s] = std::make_unique<PanelButton> ("DICE");
        addButton (*diceBtn[s], R (397.0f, s == 0 ? 211.0f : 305.0f), 42.0f, 16.0f,
                   "DICE: keep step 1 and roll steps 2-4 to consonant intervals from it "
                   "(3rds, 4ths, 5ths, 6ths, octaves), within SEQ OCT and the QUANTIZE scale. "
                   "Click again to re-roll; your host's undo brings back the previous steps. "
                   "Best with OSC assigned: with only SUB 1/SUB 2 assigned the steps move divisors instead of pitch. "
                   "MIDI-mappable as the \"Dice Seq " + juce::String (s + 1) + "\" parameter.");
        diceBtn[s]->setFontHeight (9.0f);
        diceBtn[s]->onClick = [this, s] { proc.triggerDice (s); };
    }

    // --- Rhythm generators -----------------------------------------------------------
    for (int r = 0; r < 4; ++r)
    {
        auto& k = rhythm[r];
        k.kind = Knob::Kind::rhythm;
        addDivisorKnob (k, ids::rhythm (r), R (stepX[r], 458.0f), 46.0f,
                        "Rhythm " + String (r + 1) + ": divides the clock by 1 (clockwise) to 16 (counter-clockwise).",
                        [this, r] { return proc.uiRhythmDiv[r].load(); });
        for (int s = 0; s < 2; ++s)
        {
            rhythmSeq[r][s] = std::make_unique<PanelButton> ("SEQ " + String (s + 1));
            addToggle (*rhythmSeq[r][s], ids::rhythmToSeq (r, s), R (stepX[r], s == 0 ? 501.0f : 535.0f), 42.0f, 24.0f,
                       "Rhythm " + String (r + 1) + " advances Sequencer " + String (s + 1) + ". Combine rhythms for polyrhythms.");
        }
    }

    // --- Tempo & transport -------------------------------------------------------------
    addKnob (tempo, ids::tempo, R (209.0f, 627.0f), 98.0f,
             "TEMPO: free-running clock, 20-3000 BPM (one pulse per beat). Inactive while SYNC follows the host.");
    addButton (resetBtn, R (298.0f, 597.0f), 38.0f, 30.0f, "RESET: back to step 1 and re-align the rhythms. Hold to hold step 1 (EGs keep firing).");
    addButton (egBtn, R (347.0f, 597.0f), 38.0f, 30.0f, "EG: click toggles whether steps fire the envelopes. Hold until it blinks to hold them open.");
    addButton (nextBtn, R (395.0f, 597.0f), 38.0f, 30.0f, "NEXT: advance one step (sequencers with a rhythm assigned).");
    addButton (playBtn, R (310.0f, 635.0f), 64.0f, 30.0f, "PLAY: run the sequencers. With SYNC on they run while the host plays.");
    addButton (triggerBtn, R (380.0f, 635.0f), 64.0f, 30.0f, "TRIGGER: fire the envelopes now. With EG off it acts as a gate while held.");

    resetBtn.onPress   = [this] { proc.uiResetPress(); proc.uiResetHeld (true); };
    resetBtn.onRelease = [this] { proc.uiResetHeld (false); };
    nextBtn.onPress    = [this] { proc.uiNext(); };
    playBtn.onClick    = [this] { proc.uiTogglePlay(); };
    triggerBtn.onPress   = [this] { proc.uiTriggerHeld (true); };
    triggerBtn.onRelease = [this] { proc.uiTriggerHeld (false); };
    egBtn.onClick = [this]
    {
        const int m = juce::roundToInt (getParam (ids::egMode));
        setParam (ids::egMode, m == 1 ? 0.0f : 1.0f);   // Held -> On, On <-> Off
    };
    egBtn.onLongPress = [this] { setParam (ids::egMode, 2.0f); };

    // --- Oscillators ---------------------------------------------------------------------
    const float vcoX[] { 581.0f, 843.0f };
    const float subX[2][2] { { 529.0f, 633.0f }, { 791.0f, 895.0f } };
    const float waveX[] { 511.0f, 914.0f };
    for (int v = 0; v < 2; ++v)
    {
        addKnob (vcoFreq[v], ids::vcoFreq (v), R (vcoX[v], 298.0f), 92.0f,
                 "VCO " + String (v + 1) + " FREQ: four octaves, middle C to the top C. Steps through the scale when QUANTIZE is on.");
        vcoFreq[v].textFromValueFunction = [this] (double x)
        {
            const auto q = (Quant) juce::roundToInt (getParam (ids::quantize));
            return ids::noteNameForHz (kC4Hz * std::pow (2.0, quantizeOctaves (x * 4.0, q)));
        };
        for (int j = 0; j < 2; ++j)
            addDivisorKnob (subFreq[v][j], ids::subDiv (v, j), R (subX[v][j], 394.0f), 74.0f,
                            "SUB " + String (j + 1) + " FREQ: VCO " + String (v + 1) + " divided by 1 (clockwise, unison) to 16.",
                            [this, v, j] { return proc.uiSubDiv[v][j].load(); });

        if (auto* wp = apvts.getParameter (ids::vcoWave (v)))
        {
            waveSwitch[v] = std::make_unique<WaveSwitch> (*wp);
            waveSwitch[v]->setTooltip ("VCO " + String (v + 1) + " WAVE. Up: square on all three. Middle: pulse VCO (width swept by SUB 1's saw) with saw subs. Down: saw on all three.");
            waveSwitch[v]->setBounds (Rectangle<float> (28.0f, 54.0f).withCentre (R (waveX[v], 297.0f)).toNearestInt());
            addAndMakeVisible (*waveSwitch[v]);
        }
    }
    addButton (seqOctBtn, R (705.0f, 334.0f), 52.0f, 26.0f,
               "SEQ OCT: step knob range +/-1, +/-2 or +/-5 octaves. Hold until the LED blinks: SEQ outputs stay at +/-5.");
    seqOctBtn.onClick = [this]
    {
        const int i = juce::roundToInt (getParam (ids::seqOct));
        setParam (ids::seqOct, (float) ((i + 1) % 3));
    };
    seqOctBtn.onLongPress = [this] { toggleParam (ids::seqOutFixed); };
    addLed (seqOctLeds[0], R (697.0f, 263.0f), 20.0f);
    addLed (seqOctLeds[1], R (697.0f, 281.0f), 20.0f);
    addLed (seqOctLeds[2], R (697.0f, 299.0f), 20.0f);

    // --- Mixer ---------------------------------------------------------------------------
    const float assignX[2][3] { { 533.0f, 581.0f, 629.0f }, { 800.0f, 848.0f, 896.0f } };
    for (int s = 0; s < 2; ++s)
        for (int t = 0; t < 3; ++t)
        {
            static const char* names[] { "OSC", "SUB 1", "SUB 2" };
            const String label = t == 0 ? "OSC " + String (s + 1) : String (names[t]);
            assignBtn[s][t] = std::make_unique<PanelButton> (label);
            const String what = t == 0 ? "the pitch of VCO " + String (s + 1) + " (its subs follow)"
                                       : "the divisor of SUB " + String (t) + " of VCO " + String (s + 1);
            addToggle (*assignBtn[s][t], ids::assign (s, t), R (assignX[s][t], 489.0f), 40.0f, 26.0f,
                       "SEQ " + String (s + 1) + " ASSIGN: Sequencer " + String (s + 1) + " changes " + what + ".");
        }

    for (int v = 0; v < 2; ++v)
    {
        addKnob (vcoLevel[v], ids::vcoLevel (v), R (vcoX[v], 551.0f), 64.0f,
                 "VCO " + String (v + 1) + " LEVEL. Push several sources high for warm mixer saturation.");
        for (int j = 0; j < 2; ++j)
        {
            const float x = v == 0 ? (j == 0 ? 529.0f : 633.0f) : (j == 0 ? 796.0f : 900.0f);
            addKnob (subLevel[v][j], ids::subLevel (v, j), R (x, 636.0f), 64.0f,
                     "SUB " + String (j + 1) + " LEVEL (VCO " + String (v + 1) + ").");
        }
    }
    addButton (quantizeBtn, R (705.0f, 549.0f), 58.0f, 26.0f,
               "QUANTIZE: off, 12-ET, 8-ET, 12-JI, 8-JI. Applies to the VCO FREQ knobs and the steps. Hold until the LED blinks: SEQ outputs unquantized.");
    quantizeBtn.onClick = [this]
    {
        const int q = juce::roundToInt (getParam (ids::quantize));
        setParam (ids::quantize, (float) ((q + 1) % 5));
    };
    quantizeBtn.onLongPress = [this] { toggleParam (ids::seqOutUnq); };
    const float qy[] { 461.0f, 479.0f, 497.0f, 516.0f };
    for (int i = 0; i < 4; ++i)
        addLed (quantLeds[i], R (697.0f, qy[i]), 20.0f);

    // --- Filter / VCA / envelopes ---------------------------------------------------------
    addKnob (cutoff, ids::cutoff, R (1025.0f, 266.0f), 90.0f, "CUTOFF: 4-pole ladder low-pass, 20 Hz to 20 kHz.");
    addKnob (volume, ids::volume, R (1137.0f, 266.0f), 90.0f, "VOLUME: the level the VCA envelope opens to.");
    addKnob (resonance, ids::resonance, R (1025.0f, 379.0f), 90.0f, "RESONANCE: emphasis at the cutoff. At maximum the filter self-oscillates.");
    addKnob (egAmt, ids::egAmt, R (1137.0f, 379.0f), 90.0f, "VCF EG AMT: bipolar. Clockwise opens the filter with the envelope, counter-clockwise closes it; centre = none.");
    addKnob (vcfAttack, ids::vcfAttack, R (1025.0f, 516.0f), 90.0f, "VCF ATTACK: 1 ms to 10 s. The envelope won't restart during its attack.");
    addKnob (vcfDecay, ids::vcfDecay, R (1137.0f, 516.0f), 90.0f, "VCF DECAY: 5 ms to 10 s.");
    addKnob (vcaAttack, ids::vcaAttack, R (1025.0f, 630.0f), 90.0f, "VCA ATTACK: 1 ms to 10 s.");
    addKnob (vcaDecay, ids::vcaDecay, R (1137.0f, 630.0f), 90.0f, "VCA DECAY: 5 ms to 10 s.");

    // --- Rail ------------------------------------------------------------------------------
    buildRail();

    // --- Patchbay (on top: its cables hang over the panel) ------------------------------------
    patchbay = std::make_unique<Patchbay> (proc, makeJacks());
    patchbay->setBounds (getLocalBounds().withBottom ((int) kRailTop));
    addAndMakeVisible (*patchbay);

    startTimerHz (30);
    timerCallback();
}

FractonePanel::~FractonePanel()
{
    stopTimer();
    divAttachments.clear();
    attachments.clear();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
float FractonePanel::getParam (const String& id) const
{
    if (auto* p = apvts.getParameter (id))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

void FractonePanel::setParam (const String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void FractonePanel::toggleParam (const String& id)
{
    setParam (id, getParam (id) > 0.5f ? 0.0f : 1.0f);
}

void FractonePanel::addKnob (Knob& k, const String& id, Point<float> centre, float size, const String& tip)
{
    k.setTooltip (tip);
    k.setBounds (Rectangle<float> (size, size).withCentre (centre).toNearestInt());
    addAndMakeVisible (k);
    attachments.push_back (std::make_unique<SliderAttachment> (apvts, id, k));
    if (auto* p = apvts.getParameter (id))
        k.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void FractonePanel::addDivisorKnob (Knob& k, const String& id, Point<float> centre, float size, const String& tip,
                                    std::function<int()> effective)
{
    k.setTooltip (tip);
    k.setBounds (Rectangle<float> (size, size).withCentre (centre).toNearestInt());
    addAndMakeVisible (k);
    if (auto* p = apvts.getParameter (id))
        divAttachments.push_back (std::make_unique<InvertedDivisorAttachment> (*p, k, std::move (effective)));
}

void FractonePanel::addButton (PanelButton& b, Point<float> centre, float w, float h, const String& tip)
{
    b.setTooltip (tip);
    b.setBounds (Rectangle<float> (w, h).withCentre (centre).toNearestInt());
    addAndMakeVisible (b);
}

void FractonePanel::addToggle (PanelButton& b, const String& id, Point<float> centre, float w, float h, const String& tip)
{
    addButton (b, centre, w, h, tip);
    b.setFontHeight (10.5f);
    b.onClick = [this, id] { toggleParam (id); };
}

void FractonePanel::addLed (Led& l, Point<float> centre, float size)
{
    l.setBounds (Rectangle<float> (size, size).withCentre (centre).toNearestInt());
    addAndMakeVisible (l);
}

// ---------------------------------------------------------------------------
// Utility rail: presets, host sync, extras
// ---------------------------------------------------------------------------
void FractonePanel::buildRail()
{
    const float cy = kRailTop + 30.0f;
    auto place = [] (juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds (Rectangle<float> (x, y - h * 0.5f, w, h).toNearestInt());
    };

    // Presets
    place (prevBtn, 44.0f, cy, 34.0f, 30.0f);
    place (presetDisplay, 84.0f, cy, 290.0f, 30.0f);
    place (nextPresetBtn, 380.0f, cy, 34.0f, 30.0f);
    place (saveBtn, 422.0f, cy, 60.0f, 30.0f);
    for (auto* c : { (juce::Component*) &prevBtn, (juce::Component*) &presetDisplay,
                     (juce::Component*) &nextPresetBtn, (juce::Component*) &saveBtn })
        addAndMakeVisible (c);
    prevBtn.setTooltip ("Previous preset");
    nextPresetBtn.setTooltip ("Next preset");
    saveBtn.setTooltip ("Save: overwrite your current user preset, or name a new one.");
    presetDisplay.setTooltip ("Click to browse presets, save, rename or delete.");
    prevBtn.onClick = [this] { proc.presets.step (-1); };
    nextPresetBtn.onClick = [this] { proc.presets.step (1); };
    saveBtn.onClick = [this]
    {
        if (proc.presets.currentIsUser())
            proc.presets.saveAs (proc.presets.currentName());
        else
            savePresetAs();
    };
    presetDisplay.getText = [this]
    {
        auto n = proc.presets.currentName();
        if (proc.presets.isEdited()) n << " *";
        return n;
    };
    presetDisplay.onOpen = [this] { showPresetMenu(); };

    // Sync
    place (syncBtn, 520.0f, cy, 60.0f, 30.0f);
    addAndMakeVisible (syncBtn);
    syncBtn.setTooltip ("SYNC: follow the host's tempo and transport. The clock pulses at the RATE below; RESET happens on host start or loop.");
    syncBtn.onClick = [this] { toggleParam (ids::sync); };

    place (rateDisplay, 588.0f, cy, 92.0f, 30.0f);
    addAndMakeVisible (rateDisplay);
    rateDisplay.setTooltip ("Clock pulse length when synced (the hardware runs MIDI clock at 1/16).");
    rateDisplay.getText = [this] { return ids::syncRateNames()[juce::roundToInt (getParam (ids::syncRate))]; };
    rateDisplay.buildMenu = [this] (juce::PopupMenu& m)
    {
        const int cur = juce::roundToInt (getParam (ids::syncRate));
        for (int i = 0; i < ids::syncRateNames().size(); ++i)
            m.addItem (i + 1, ids::syncRateNames()[i], true, i == cur);
    };
    rateDisplay.onChoose = [this] (int r) { setParam (ids::syncRate, (float) (r - 1)); };

    // Logic
    place (logicBtn, 712.0f, cy, 60.0f, 30.0f);
    addAndMakeVisible (logicBtn);
    logicBtn.setTooltip ("Rhythm logic. OR: a sequencer advances on any assigned rhythm. XOR: coinciding pulses cancel.");
    logicBtn.onClick = [this] { toggleParam (ids::logic); };

    // Fine tune & drift
    fineTune.setTooltip ("FINE TUNE: +/-50 cents for the whole instrument.");
    fineTune.setBounds (Rectangle<float> (44.0f, 44.0f).withCentre ({ 816.0f, cy - 2.0f }).toNearestInt());
    addAndMakeVisible (fineTune);
    attachments.push_back (std::make_unique<SliderAttachment> (apvts, ids::fineTune, fineTune));
    fineTune.setDoubleClickReturnValue (true, 0.0);

    drift.setTooltip ("DRIFT: slow analog-style pitch wander of the two oscillators.");
    drift.setBounds (Rectangle<float> (44.0f, 44.0f).withCentre ({ 896.0f, cy - 2.0f }).toNearestInt());
    addAndMakeVisible (drift);
    attachments.push_back (std::make_unique<SliderAttachment> (apvts, ids::drift, drift));
    drift.setDoubleClickReturnValue (true, 0.25);

    // MIDI channel
    place (channelDisplay, 950.0f, cy, 84.0f, 30.0f);
    addAndMakeVisible (channelDisplay);
    channelDisplay.setTooltip ("MIDI channel for notes and CCs.");
    channelDisplay.getText = [this]
    {
        const int c = juce::roundToInt (getParam (ids::midiChannel));
        return c == 0 ? String ("ALL") : String (c);
    };
    channelDisplay.buildMenu = [this] (juce::PopupMenu& m)
    {
        const int cur = juce::roundToInt (getParam (ids::midiChannel));
        m.addItem (1, "All channels", true, cur == 0);
        for (int c = 1; c <= 16; ++c)
            m.addItem (c + 1, "Channel " + String (c), true, cur == c);
    };
    channelDisplay.onChoose = [this] (int r) { setParam (ids::midiChannel, (float) (r - 1)); };

    // Cables
    place (clearBtn, 1452.0f, cy, 116.0f, 30.0f);
    addAndMakeVisible (clearBtn);
    clearBtn.setTooltip ("Unplug every patch cable.");
    clearBtn.onClick = [this] { proc.clearPatch(); proc.presets.markEdited(); };
}

void FractonePanel::showPresetMenu()
{
    juce::PopupMenu m;
    const auto entries = proc.presets.list();
    const int cur = proc.presets.currentIndex();

    m.addSectionHeader ("FACTORY");
    bool userHeader = false;
    for (int i = 0; i < entries.size(); ++i)
    {
        if (! entries[i].factory && ! userHeader)
        {
            m.addSeparator();
            m.addSectionHeader ("YOUR PRESETS");
            userHeader = true;
        }
        m.addItem (i + 1, entries[i].name, true, i == cur);
    }
    if (! userHeader)
    {
        m.addSeparator();
        m.addItem (-1, "(no saved presets yet)", false);
    }
    m.addSeparator();
    m.addItem (10001, proc.presets.currentIsUser() ? "Save" : "Save...", true);
    m.addItem (10002, "Save As...", true);
    m.addItem (10003, "Delete \"" + proc.presets.currentName() + "\"", proc.presets.currentIsUser());
    m.addItem (10004, "Show Presets Folder", true);

    auto safe = juce::Component::SafePointer<FractonePanel> (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetDisplay).withMinimumWidth (presetDisplay.getWidth()),
                     [safe, entries] (int result)
                     {
                         if (safe == nullptr || result == 0) return;
                         auto& self = *safe;
                         if (result > 0 && result <= entries.size())
                             self.proc.presets.load (entries[result - 1]);
                         else if (result == 10001)
                             self.saveBtn.onClick();
                         else if (result == 10002)
                             self.savePresetAs();
                         else if (result == 10003)
                             self.confirmDelete();
                         else if (result == 10004)
                         {
                             auto dir = PresetManager::presetFolder();
                             dir.createDirectory();
                             dir.revealToUser();
                         }
                         self.presetDisplay.refresh();
                     });
}

void FractonePanel::savePresetAs()
{
    auto* aw = new juce::AlertWindow ("Save Preset",
                                      "Name this patch. Every knob, switch and patch cable is saved.\n"
                                      "A preset with the same name is replaced.",
                                      juce::MessageBoxIconType::NoIcon);
    aw->setLookAndFeel (&lnf);
    String suggestion = proc.presets.currentName();
    if (! proc.presets.currentIsUser())
        suggestion = suggestion == "Init" ? String ("My Patch") : suggestion + " (mine)";
    aw->addTextEditor ("name", suggestion, "Preset name:");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->setAlwaysOnTop (true);

    auto safe = juce::Component::SafePointer<FractonePanel> (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (safe != nullptr && result == 1)
        {
            const auto name = aw->getTextEditorContents ("name");
            if (! safe->proc.presets.saveAs (name))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Preset",
                                                        "Couldn't save \"" + name + "\" to\n" + PresetManager::presetFolder().getFullPathName());
            safe->presetDisplay.refresh();
        }
    }), true);
}

void FractonePanel::confirmDelete()
{
    const auto entries = proc.presets.list();
    const int idx = proc.presets.currentIndex();
    if (! juce::isPositiveAndBelow (idx, entries.size()) || entries[idx].factory)
        return;
    const auto entry = entries[idx];

    auto options = juce::MessageBoxOptions()
                       .withTitle ("Delete Preset")
                       .withMessage ("Move \"" + entry.name + "\" to the Trash? The current sound stays loaded.")
                       .withButton ("Delete")
                       .withButton ("Cancel")
                       .withIconType (juce::MessageBoxIconType::QuestionIcon);
    auto safe = juce::Component::SafePointer<FractonePanel> (this);
    juce::AlertWindow::showAsync (options, [safe, entry] (int result)
    {
        if (safe != nullptr && result == 1)
        {
            safe->proc.presets.remove (entry);
            safe->presetDisplay.refresh();
        }
    });
}

// ---------------------------------------------------------------------------
// Live state
// ---------------------------------------------------------------------------
void FractonePanel::timerCallback()
{
    ++blinkCounter;
    const bool blinkOn = (blinkCounter / 8) % 2 == 0;

    // Steps
    const bool running = proc.uiRunning.load();
    for (int s = 0; s < 2; ++s)
    {
        const int cur = proc.uiStep[s].load();
        for (int i = 0; i < 4; ++i)
            stepLeds[s][i].setLevel (i == cur ? (running ? 1.0f : 0.55f) : 0.0f);
    }

    // Rhythm buttons: lit = assigned, flash on each pulse
    for (int r = 0; r < 4; ++r)
    {
        const auto fires = proc.uiRhythmFires[r].load();
        if (fires != lastFires[r]) { rhythmFlash[r] = 1.0f; lastFires[r] = fires; }
        else rhythmFlash[r] *= 0.75f;
        for (int s = 0; s < 2; ++s)
        {
            rhythmSeq[r][s]->setLit (getParam (ids::rhythmToSeq (r, s)) > 0.5f);
            rhythmSeq[r][s]->setFlash (rhythmFlash[r]);
        }
    }

    for (int s = 0; s < 2; ++s)
        for (int t = 0; t < 3; ++t)
            assignBtn[s][t]->setLit (getParam (ids::assign (s, t)) > 0.5f);

    // Transport
    playBtn.setLit (proc.uiPlayOn.load());
    const int eg = juce::roundToInt (getParam (ids::egMode));
    egBtn.setLit (eg != 0);
    egBtn.setBlink (eg == 2);
    egBtn.setBlinkPhase (blinkOn);

    // SEQ OCT & QUANTIZE LEDs (blink in their hidden modes)
    const int so = juce::roundToInt (getParam (ids::seqOct));      // 0 ±1, 1 ±2, 2 ±5
    const bool soFixed = getParam (ids::seqOutFixed) > 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        const bool on = (2 - i) == so;   // LEDs top to bottom: ±5, ±2, ±1
        seqOctLeds[i].setLevel (on && (! soFixed || blinkOn) ? 1.0f : 0.0f);
    }
    const int q = juce::roundToInt (getParam (ids::quantize));
    const bool qUnq = getParam (ids::seqOutUnq) > 0.5f;
    for (int i = 0; i < 4; ++i)
        quantLeds[i].setLevel ((q == i + 1) && (! qUnq || blinkOn) ? 1.0f : 0.0f);

    // Rail
    syncBtn.setLit (getParam (ids::sync) > 0.5f);
    const bool xorLogic = juce::roundToInt (getParam (ids::logic)) == 1;
    logicBtn.setLabel (xorLogic ? "XOR" : "OR");
    logicBtn.setLit (xorLogic);
    presetDisplay.refresh();
    rateDisplay.refresh();
    channelDisplay.refresh();

    // MIDI activity
    const auto midi = proc.uiMidi.load();
    if (midi != lastMidi) { midiFlash = 1.0f; lastMidi = midi; }
    else midiFlash *= 0.8f;
    patchbay->setMidiLevel (midiFlash);

    if (const int pv = proc.patchVersion.load(); pv != lastPatchVersion)
    {
        lastPatchVersion = pv;
        patchbay->refresh();
    }

    // Status line
    String st;
    if (getParam (ids::sync) > 0.5f)
    {
        st << "SYNC  " << String (proc.uiHostBpm.load(), 1) << " BPM  " << ids::syncRateNames()[juce::roundToInt (getParam (ids::syncRate))];
        st << (proc.uiHostPlaying.load() ? (proc.uiPlayOn.load() ? "  RUNNING" : "  PLAY OFF") : "  WAITING FOR HOST");
    }
    else
    {
        st << "FREE  " << juce::roundToInt (getParam (ids::tempo)) << " BPM" << (running ? "  RUNNING" : "  STOPPED");
    }
    if (st != statusText)
    {
        statusText = st;
        repaint (Rectangle<int> (1060, (int) kRailTop, 380, designH - (int) kRailTop));
    }

    tempo.setAlpha (getParam (ids::sync) > 0.5f ? 0.45f : 1.0f);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void FractonePanel::paintWood (juce::Graphics& g, Rectangle<float> r) const
{
    juce::ColourGradient grad (colours::wood.brighter (0.12f), r.getX(), r.getY(),
                               colours::woodDark, r.getRight(), r.getY(), false);
    g.setGradientFill (grad);
    g.fillRect (r);
    juce::Random rnd (1234 + (int) r.getX());
    for (int i = 0; i < 26; ++i)
    {
        const float x = r.getX() + rnd.nextFloat() * r.getWidth();
        juce::Path grain;
        grain.startNewSubPath (x, r.getY());
        for (float y = r.getY(); y < r.getBottom(); y += 40.0f)
            grain.lineTo (x + std::sin (y * 0.013f + (float) i) * 2.5f, y + 40.0f);
        g.setColour (colours::woodDark.withAlpha (0.25f + rnd.nextFloat() * 0.3f));
        g.strokePath (grain, juce::PathStrokeType (0.6f + rnd.nextFloat()));
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRect (r, 1.0f);
}

void FractonePanel::paintSections (juce::Graphics& g) const
{
    const auto ink = colours::ink;
    auto box = [&] (Rectangle<float> r)
    {
        g.setColour (ink.withAlpha (0.9f));
        g.drawRoundedRectangle (r, 3.0f, 1.6f);
    };

    box (RB (138, 200, 447, 395));   // sequencers
    box (RB (138, 402, 447, 556));   // polyrhythm
    box (RB (138, 563, 447, 672));   // tempo
    box (RB (454, 200, 965, 440));   // oscillators
    box (RB (454, 443, 965, 672));   // mixer
    box (RB (969, 200, 1190, 437));  // filter
    box (RB (1081, 204, 1186, 313)); // volume
    box (RB (969, 440, 1190, 672));  // envelopes
    box (RB (1196, 200, 1390, 672)); // patchbay

    // Sequencers
    const float stepX[] { 185.0f, 256.0f, 327.0f, 397.0f };
    drawText (g, "SEQUENCER 1", R (292, 212), 12.5f);
    drawText (g, "SEQUENCER 2", R (292, 306), 12.5f);
    for (int i = 0; i < 4; ++i)
    {
        drawText (g, "STEP " + String (i + 1), R (stepX[i], 224), 9.5f);
        drawText (g, "STEP " + String (i + 1), R (stepX[i], 318), 9.5f);
    }

    // Polyrhythm
    drawText (g, "POLYRHYTHM", R (292, 413), 12.5f);
    for (int r = 0; r < 4; ++r)
    {
        drawText (g, "RHYTHM " + String (r + 1), R (stepX[r], 427), 9.5f);
        g.setColour (ink.withAlpha (0.85f));
        g.drawLine (juce::Line<float> (R (stepX[r], 477), R (stepX[r], 491)), 1.4f);
        g.drawLine (juce::Line<float> (R (stepX[r], 511), R (stepX[r], 525)), 1.4f);
    }

    // Tempo
    drawText (g, "TEMPO", R (209, 573), 13.0f);

    // Oscillators header
    {
        const auto l = R (464, 213), rr = R (955, 213);
        g.setColour (ink);
        g.drawLine (l.x + 26.0f, l.y, R (650, 213).x, l.y, 1.4f);
        g.drawLine (R (768, 213).x, l.y, rr.x - 26.0f, rr.y, 1.4f);
        drawText (g, "OSCILLATORS", R (709, 213), 13.0f);
        auto num = [&] (const String& t, Point<float> c)
        {
            const auto rect = Rectangle<float> (20.0f, 20.0f).withCentre (c);
            g.setColour (ink);
            g.drawRect (rect, 1.4f);
            drawText (g, t, c, 12.0f);
        };
        num ("1", R (476, 213));
        num ("2", R (943, 213));
    }
    drawText (g, "VCO 1 FREQ", R (581, 243), 12.0f);
    drawText (g, "VCO 2 FREQ", R (843, 243), 12.0f);
    drawText (g, "VCO 1 WAVE", R (511, 264), 9.5f);
    drawText (g, "VCO 2 WAVE", R (914, 264), 9.5f);
    drawText (g, "SUB 1 FREQ", R (529, 353), 10.5f);
    drawText (g, "SUB 2 FREQ", R (633, 353), 10.5f);
    drawText (g, "SUB 1 FREQ", R (791, 353), 10.5f);
    drawText (g, "SUB 2 FREQ", R (895, 353), 10.5f);

    // Wave symbols beside the switches
    auto squareSym = [&] (Point<float> c)
    {
        juce::Path p;
        p.startNewSubPath (c.x - 6, c.y + 4); p.lineTo (c.x - 6, c.y - 4); p.lineTo (c.x + 1, c.y - 4);
        p.lineTo (c.x + 1, c.y + 4); p.lineTo (c.x + 6, c.y + 4);
        g.setColour (ink); g.strokePath (p, juce::PathStrokeType (1.5f));
    };
    auto sawSym = [&] (Point<float> c)
    {
        juce::Path p;
        p.startNewSubPath (c.x - 6, c.y + 4); p.lineTo (c.x + 5, c.y - 4); p.lineTo (c.x + 5, c.y + 4);
        g.setColour (ink); g.strokePath (p, juce::PathStrokeType (1.5f));
    };
    auto mixedSym = [&] (Point<float> c)
    {
        juce::Path p;
        p.startNewSubPath (c.x - 13, c.y + 4); p.lineTo (c.x - 13, c.y - 4); p.lineTo (c.x - 8, c.y - 4); p.lineTo (c.x - 8, c.y + 4);
        p.startNewSubPath (c.x - 4, c.y); p.lineTo (c.x - 1, c.y);
        p.startNewSubPath (c.x + 3, c.y + 4); p.lineTo (c.x + 12, c.y - 4); p.lineTo (c.x + 12, c.y + 4);
        g.setColour (ink); g.strokePath (p, juce::PathStrokeType (1.4f));
    };
    squareSym (R (511, 277)); sawSym (R (511, 319)); mixedSym (R (481, 297));
    squareSym (R (914, 277)); sawSym (R (914, 319)); mixedSym (R (946, 297));

    // SEQ OCT / QUANTIZE labels and centre divider
    const char* octLabels[] { "\xc2\xb1" "5", "\xc2\xb1" "2", "\xc2\xb1" "1" };
    const float octY[] { 263, 281, 299 };
    for (int i = 0; i < 3; ++i)
        drawText (g, String::fromUTF8 (octLabels[i]), R (714, octY[i]), 10.0f);
    const char* qLabels[] { "12-ET", "8-ET", "12-JI", "8-JI" };
    const float qy[] { 461, 479, 497, 516 };
    for (int i = 0; i < 4; ++i)
        drawText (g, qLabels[i], R (719, qy[i]), 9.5f);
    g.setColour (ink.withAlpha (0.8f));
    g.drawLine (juce::Line<float> (R (705, 348), R (705, 440)), 1.2f);
    g.drawLine (juce::Line<float> (R (705, 443), R (705, 452)), 1.2f);
    g.drawLine (juce::Line<float> (R (705, 562), R (705, 672)), 1.2f);

    // Assign brackets
    auto bracket = [&] (float x0, float x1, const String& label)
    {
        const auto a = R (x0, 471), b = R (x1, 471);
        const auto top = R (x0, 465).y;
        g.setColour (ink);
        g.drawLine (a.x, top, a.x, a.y + 12.0f, 1.3f);
        g.drawLine (b.x, top, b.x, b.y + 12.0f, 1.3f);
        const float mid = (a.x + b.x) * 0.5f;
        g.drawLine (a.x, top, mid - 40.0f, top, 1.3f);
        g.drawLine (mid + 40.0f, top, b.x, top, 1.3f);
        drawText (g, label, { mid, top }, 9.0f);
    };
    bracket (511, 651, "SEQ 1 ASSIGN");
    bracket (778, 918, "SEQ 2 ASSIGN");
    drawText (g, "VCO 1 LEVEL", R (581, 513), 10.5f);
    drawText (g, "VCO 2 LEVEL", R (848, 513), 10.5f);
    drawText (g, "SUB 1 LEVEL", R (529, 599), 10.5f);
    drawText (g, "SUB 2 LEVEL", R (633, 599), 10.5f);
    drawText (g, "SUB 1 LEVEL", R (796, 599), 10.5f);
    drawText (g, "SUB 2 LEVEL", R (900, 599), 10.5f);

    // Filter & envelopes
    drawText (g, "CUTOFF", R (1025, 213), 12.0f);
    drawText (g, "VOLUME", R (1137, 214), 12.0f);
    drawText (g, "RESONANCE", R (1025, 327), 12.0f);
    drawText (g, "VCF EG AMT", R (1137, 327), 12.0f);
    drawText (g, juce::String::fromUTF8 ("(\xe2\x80\x93)"), R (1108, 424), 9.0f);
    drawText (g, "(+)", R (1166, 424), 9.0f);
    drawText (g, "VCF ATTACK", R (1025, 464), 12.0f);
    drawText (g, "VCF DECAY", R (1137, 464), 12.0f);
    drawText (g, "VCA ATTACK", R (1025, 578), 12.0f);
    drawText (g, "VCA DECAY", R (1137, 578), 12.0f);

    // Patchbay header
    {
        const auto c = R (1293, 218);
        drawText (g, "IN /", c.translated (-16.0f, 0.0f), 11.0f);
        drawInverseText (g, "OUT", c.translated (14.0f, 0.0f), 11.0f);
    }

    // Brand line
    const float by = 614.0f;
    g.setColour (ink);
    g.setFont (panelFont (30.0f, true));
    g.drawText ("FRACTONE", Rectangle<float> (R (146, 0).x, by - 18.0f, 200.0f, 36.0f), juce::Justification::centredLeft, false);
    g.drawLine (R (146, 0).x + 168.0f, by - 13.0f, R (146, 0).x + 168.0f, by + 13.0f, 1.6f);
    g.setFont (panelFont (11.5f, true));
    g.drawText ("POLYRHYTHMIC", Rectangle<float> (R (146, 0).x + 176.0f, by - 13.0f, 200.0f, 13.0f), juce::Justification::centredLeft, false);
    g.drawText ("UNDERTONE SYNTHESIZER", Rectangle<float> (R (146, 0).x + 176.0f, by, 200.0f, 13.0f), juce::Justification::centredLeft, false);

    // Fraction mark f/n
    {
        const auto c = Point<float> (R (1293, 0).x, by);
        g.setColour (ink);
        g.drawEllipse (Rectangle<float> (30.0f, 30.0f).withCentre (c), 1.6f);
        g.setFont (panelFont (12.0f, true));
        g.drawText ("f", Rectangle<float> (12.0f, 12.0f).withCentre (c.translated (-5.0f, -5.0f)), juce::Justification::centred, false);
        g.drawLine (c.x + 6.0f, c.y - 9.0f, c.x - 6.0f, c.y + 9.0f, 1.3f);
        g.drawText ("n", Rectangle<float> (12.0f, 12.0f).withCentre (c.translated (5.0f, 5.0f)), juce::Justification::centred, false);
    }
}

void FractonePanel::paintRail (juce::Graphics& g) const
{
    const auto r = Rectangle<float> (0.0f, kRailTop, (float) designW, (float) designH - kRailTop);
    g.setColour (colours::rail);
    g.fillRect (r);
    g.setColour (juce::Colours::black);
    g.drawLine (0.0f, kRailTop, (float) designW, kRailTop, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (0.0f, kRailTop + 2.0f, (float) designW, kRailTop + 2.0f, 1.0f);

    const float capY = kRailTop + 58.0f;
    auto cap = [&] (const String& t, float cx) { drawText (g, t, { cx, capY }, 8.5f, colours::inkDim); };
    cap ("PRESET", 229.0f);
    cap ("HOST", 550.0f);
    cap ("RATE", 634.0f);
    cap ("LOGIC", 742.0f);
    cap ("FINE TUNE", 816.0f);
    cap ("DRIFT", 896.0f);
    cap ("MIDI CH", 992.0f);
    cap ("PATCHBAY", 1510.0f);

    // Status display
    const auto st = Rectangle<float> (1070.0f, kRailTop + 15.0f, 360.0f, 30.0f);
    g.setColour (juce::Colour (0xff09090a));
    g.fillRoundedRectangle (st, 3.0f);
    g.setColour (colours::inkDim.withAlpha (0.4f));
    g.drawRoundedRectangle (st, 3.0f, 1.0f);
    g.setColour (colours::ledAmber);
    g.setFont (panelFont (14.0f, true));
    g.drawText (statusText, st.reduced (10.0f, 0.0f), juce::Justification::centredLeft, true);
    cap ("CLOCK", 1250.0f);
}

void FractonePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    paintWood (g, { 0.0f, 0.0f, 30.0f, kRailTop });
    paintWood (g, { R (1410, 0).x, 0.0f, (float) designW - R (1410, 0).x, kRailTop });
    paintSections (g);
    paintRail (g);
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------
FractoneEditor::FractoneEditor (FractoneProcessor& p)
    : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    setResizable (true, true);
    const double ratio = (double) FractonePanel::designW / FractonePanel::designH;
    setResizeLimits (960, (int) (960 / ratio), 2400, (int) (2400 / ratio));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);
    setSize (1280, (int) (1280 / ratio));
}

void FractoneEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void FractoneEditor::resized()
{
    const float s = (float) getWidth() / (float) FractonePanel::designW;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, FractonePanel::designW, FractonePanel::designH);
}
