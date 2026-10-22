// Presets.h — factory patches. The first is the default panel; the rest are
// starting points modelled on classic polyrhythmic patch-sheet ideas.
// Values are written as knob positions (0..1, or -1..+1 for bipolar knobs)
// and converted to parameter units here.
#pragma once

#include "Params.h"
#include <vector>

namespace fractone
{
struct Preset
{
    juce::String name;
    juce::String notes;
    std::vector<std::pair<juce::String, float>> values;   // plain parameter values
    std::vector<std::pair<int, int>> cables;              // { output, input }
    bool startFromDefaults = true;
};

namespace presetdetail
{
    // Knob position helpers
    inline float divFromKnob (float n)   { return (float) juce::jlimit (1, 16, 16 - juce::roundToInt (n * 15.0f)); }
    inline float cutoffFromKnob (float n) { return 20.0f * std::pow (1000.0f, n); }
    inline float attackFromKnob (float n) { return (float) attackSecondsFromNorm (n); }
    inline float decayFromKnob (float n)  { return (float) decaySecondsFromNorm (n); }
    inline float tempoFromKnob (float n)  { return juce::jlimit (20.0f, 3000.0f, (0.333f + 49.67f * n) * 60.0f); }

    class Builder
    {
    public:
        Builder (juce::String n, juce::String nt) { p.name = std::move (n); p.notes = std::move (nt); clearPanel(); }

        Builder& seq (int s, float a, float b, float c, float d)
        {
            const float v[] { a, b, c, d };
            for (int i = 0; i < 4; ++i) set (ids::step (s, i), v[i]);
            return *this;
        }
        Builder& rhythm (int r, int div, bool toSeq1, bool toSeq2)
        {
            set (ids::rhythm (r), (float) div);
            set (ids::rhythmToSeq (r, 0), toSeq1 ? 1.0f : 0.0f);
            set (ids::rhythmToSeq (r, 1), toSeq2 ? 1.0f : 0.0f);
            return *this;
        }
        Builder& vco (int v, float freqKnob, Wave w, int sub1, int sub2)
        {
            set (ids::vcoFreq (v), freqKnob);
            set (ids::vcoWave (v), (float) (int) w);
            set (ids::subDiv (v, 0), (float) sub1);
            set (ids::subDiv (v, 1), (float) sub2);
            return *this;
        }
        Builder& assign (int s, bool osc, bool sub1, bool sub2)
        {
            set (ids::assign (s, 0), osc ? 1.0f : 0.0f);
            set (ids::assign (s, 1), sub1 ? 1.0f : 0.0f);
            set (ids::assign (s, 2), sub2 ? 1.0f : 0.0f);
            return *this;
        }
        Builder& levels (int v, float lvco, float lsub1, float lsub2)
        {
            set (ids::vcoLevel (v), lvco);
            set (ids::subLevel (v, 0), lsub1);
            set (ids::subLevel (v, 1), lsub2);
            return *this;
        }
        Builder& filter (float cutoffKnob, float res, float egAmt, float volume)
        {
            set (ids::cutoff, cutoffFromKnob (cutoffKnob));
            set (ids::resonance, res);
            set (ids::egAmt, egAmt);
            set (ids::volume, volume);
            return *this;
        }
        Builder& envs (float vcfA, float vcfD, float vcaA, float vcaD)
        {
            set (ids::vcfAttack, attackFromKnob (vcfA));
            set (ids::vcfDecay, decayFromKnob (vcfD));
            set (ids::vcaAttack, attackFromKnob (vcaA));
            set (ids::vcaDecay, decayFromKnob (vcaD));
            return *this;
        }
        Builder& modes (int seqOctIndex, Quant q, EgMode eg)
        {
            set (ids::seqOct, (float) seqOctIndex);
            set (ids::quantize, (float) (int) q);
            set (ids::egMode, (float) (int) eg);
            return *this;
        }
        Builder& clock (float tempoKnob, int syncRateIndex)
        {
            set (ids::tempo, tempoFromKnob (tempoKnob));
            set (ids::syncRate, (float) syncRateIndex);
            return *this;
        }
        Builder& cable (int out, int in) { p.cables.push_back ({ out, in }); return *this; }

        Preset build() { return p; }

    private:
        void set (const juce::String& id, float v)
        {
            for (auto& kv : p.values)
                if (kv.first == id) { kv.second = v; return; }
            p.values.push_back ({ id, v });
        }

        // Every preset starts from a cleared panel, not from the default patch.
        void clearPanel()
        {
            for (int s = 0; s < 2; ++s) { seq (s, 0, 0, 0, 0); assign (s, false, false, false); }
            for (int r = 0; r < 4; ++r) rhythm (r, 1, false, false);
            for (int v = 0; v < 2; ++v) levels (v, 0, 0, 0);
            set (ids::logic, 0.0f);
            set (ids::seqOutUnq, 0.0f);
            set (ids::seqOutFixed, 0.0f);
        }

        Preset p;
    };
} // namespace presetdetail

inline const std::vector<Preset>& factoryPresets()
{
    using presetdetail::Builder;
    using W = Wave;
    using Q = Quant;
    using E = EgMode;

    static const std::vector<Preset> presets = []
    {
        std::vector<Preset> v;

        Preset init;
        init.name = "Init";
        init.notes = "A first sequence: Sequencer 1 plays VCO 1, clocked by Rhythm 1.";
        v.push_back (init);

        v.push_back (Builder ("Tidepool Chords",
                              "Clock retriggers the envelopes; Sequencer 1 also sweeps the cutoff.")
            .seq (0, -0.44f, 0.19f, -0.56f, 0.0f)
            .seq (1, -0.44f, -0.95f, -0.81f, -0.44f)
            .rhythm (0, 4, true, false).rhythm (1, 5, false, true)
            .vco (0, 0.63f, W::saw, 5, 4).vco (1, 0.5f, W::square, 10, 3)
            .assign (0, true, false, false).assign (1, true, false, false)
            .levels (0, 0.44f, 0.54f, 0.54f).levels (1, 0.59f, 0.45f, 0.5f)
            .filter (0.28f, 0.2f, 0.52f, 0.85f)
            .envs (0.33f, 0.17f, 0.0f, 0.33f)
            .modes (0, Q::et8, E::off)
            .clock (0.09f, 3)
            .cable (outSeq1, inCutoff).cable (outClock, inTrigger)
            .build());

        v.push_back (Builder ("Soft Ratios",
                              "Sequencer 1's clock fires the envelopes; its pitch also moves the filter.")
            .seq (0, -0.63f, 0.07f, 0.37f, -0.44f)
            .seq (1, -0.67f, -0.33f, -0.85f, -0.33f)
            .rhythm (0, 1, true, false).rhythm (1, 8, false, true)
            .vco (0, 0.63f, W::saw, 5, 4).vco (1, 0.5f, W::square, 2, 6)
            .assign (0, true, false, false).assign (1, true, false, false)
            .levels (0, 0.48f, 0.5f, 0.5f).levels (1, 0.5f, 0.5f, 0.5f)
            .filter (0.17f, 0.05f, 0.56f, 0.9f)
            .envs (0.15f, 0.09f, 0.0f, 0.28f)
            .modes (0, Q::et8, E::off)
            .clock (0.09f, 3)
            .cable (outSeq1, inCutoff).cable (outSeq1Clk, inTrigger)
            .build());

        v.push_back (Builder ("Stumble Down",
                              "Cross-patched: each sequencer also plays the other oscillator, and the envelopes bend the rhythms.")
            .seq (0, 0.30f, -0.44f, -0.11f, -0.22f)
            .seq (1, -0.59f, -0.22f, -0.67f, 0.30f)
            .rhythm (0, 8, true, false).rhythm (1, 10, false, true)
            .vco (0, 0.95f, W::square, 6, 7).vco (1, 0.5f, W::square, 2, 3)
            .assign (0, true, false, false).assign (1, true, false, false)
            .levels (0, 0.72f, 0.03f, 0.57f).levels (1, 0.67f, 0.05f, 0.94f)
            .filter (0.70f, 0.19f, 0.56f, 0.9f)
            .envs (0.72f, 0.0f, 0.46f, 0.0f)
            .modes (0, Q::ji8, E::off)
            .clock (0.39f, 5)
            .cable (outSeq2, inVco1).cable (outSeq1, inVco2)
            .cable (outVcaEg, inRhythm2).cable (outVcfEg, inRhythm1)
            .cable (outSeq1Clk, inTrigger)
            .build());

        v.push_back (Builder ("Ground State",
                              "Full mixer into a slow filter sweep. Tune the sequencers and subs to taste; VCF ATTACK sets the sweep length.")
            .seq (0, -0.44f, -0.44f, -0.85f, -0.85f)
            .seq (1, -0.44f, -0.22f, -0.9f, -0.85f)
            .rhythm (0, 3, true, false).rhythm (1, 8, false, true)
            .vco (0, 0.67f, W::square, 6, 3).vco (1, 0.67f, W::saw, 1, 5)
            .assign (0, true, false, false).assign (1, true, false, false)
            .levels (0, 0.95f, 0.95f, 0.95f).levels (1, 0.95f, 0.95f, 0.95f)
            .filter (0.17f, 0.39f, 0.67f, 0.9f)
            .envs (0.67f, 0.22f, 0.0f, 0.98f)
            .modes (1, Q::et8, E::on)
            .clock (0.22f, 4)
            .cable (outSeq1, inVco1Pwm).cable (outSeq2, inCutoff)
            .build());

        v.push_back (Builder ("Many Hands",
                              "Both sequencers steer VCO 2 (Sequencer 1 through the patchbay); Sequencer 2 also plays the filter.")
            .seq (0, -0.63f, 0.07f, 0.37f, -0.19f)
            .seq (1, -0.56f, 0.33f, -0.33f, -0.78f)
            .rhythm (0, 13, true, false).rhythm (1, 6, false, true)
            .vco (0, 0.5f, W::square, 2, 2).vco (1, 0.5f, W::saw, 2, 4)
            .assign (0, false, false, false).assign (1, true, false, false)
            .levels (0, 0.0f, 0.0f, 0.0f).levels (1, 0.94f, 0.72f, 0.5f)
            .filter (0.19f, 0.44f, 0.44f, 0.9f)
            .envs (0.0f, 0.33f, 0.0f, 0.2f)
            .modes (1, Q::et8, E::on)
            .clock (0.65f, 6)
            .cable (outSeq1, inVco2).cable (outSeq2, inCutoff)
            .build());

        v.push_back (Builder ("Ladder Kick",
                              "No oscillators: the self-oscillating filter is the drum. Tune with CUTOFF; shape with VCF DECAY and EG AMT.")
            .seq (0, -0.95f, -0.19f, 0.95f, 0.11f)
            .seq (1, 0.95f, 0.30f, -0.56f, 0.19f)
            .rhythm (0, 8, true, false).rhythm (1, 4, false, true).rhythm (3, 16, true, false)
            .vco (0, 0.69f, W::mixed, 2, 2).vco (1, 0.5f, W::mixed, 2, 2)
            .filter (0.17f, 1.0f, 0.22f, 0.9f)
            .envs (0.0f, 0.15f, 0.0f, 0.44f)
            .modes (0, Q::et12, E::on)
            .clock (0.17f, 4)
            .cable (outSeq1, inRhythm1).cable (outSeq2, inRhythm2)
            .cable (outSeq1Clk, inCutoff).cable (outSeq2Clk, inVca)
            .build());

        v.push_back (Builder ("Spiral Stair",
                              "SUB 1 of VCO 1 a fifth down, SUB 2 of VCO 2 a major third down. The filter envelope bends Rhythm 1; VCF ATTACK sets the swell.")
            .seq (0, 0.22f, 0.22f, -0.22f, -0.22f)
            .seq (1, -0.95f, -0.44f, -0.9f, -0.44f)
            .rhythm (0, 4, true, false).rhythm (1, 12, false, true)
            .vco (0, 0.95f, W::mixed, 3, 7).vco (1, 0.5f, W::saw, 1, 5)
            .assign (0, true, false, false).assign (1, true, false, false)
            .levels (0, 0.61f, 0.61f, 0.61f).levels (1, 0.65f, 0.9f, 0.5f)
            .filter (0.22f, 0.31f, 0.78f, 0.9f)
            .envs (0.5f, 0.06f, 0.0f, 0.12f)
            .modes (0, Q::et8, E::on)
            .clock (0.5f, 5)
            .cable (outVcfEg, inRhythm1)
            .build());

        v.push_back (Builder ("Night Clusters",
                              "Three rhythms percolate Sequencer 2. Ride the mixer to bring pitches in and out; VCA DECAY sets the cluster length.")
            .seq (0, 0.19f, -0.52f, -0.59f, -0.11f)
            .seq (1, 0.07f, 0.07f, -0.22f, -0.59f)
            .rhythm (0, 3, true, false).rhythm (1, 8, false, true).rhythm (2, 7, false, true).rhythm (3, 10, false, true)
            .vco (0, 0.95f, W::square, 4, 3).vco (1, 0.95f, W::square, 7, 16)
            .assign (0, true, true, true).assign (1, true, false, false)
            .levels (0, 0.0f, 0.4f, 0.4f).levels (1, 0.4f, 0.35f, 0.35f)
            .filter (0.28f, 0.0f, 0.48f, 0.9f)
            .envs (0.0f, 0.0f, 0.0f, 0.55f)
            .modes (0, Q::ji8, E::on)
            .clock (0.74f, 6)
            .cable (outSeq2, inVco1Pwm).cable (outSeq1, inCutoff)
            .build());

        v.push_back (Builder ("Odd Rock",
                              "Slow chord changes over a clock pulse 'drum' (CLOCK into VCA). Tune the four subs into chord shapes.")
            .seq (0, -0.95f, -0.85f, -0.85f, -0.95f)
            .seq (1, 0.19f, -1.0f, -0.11f, -0.95f)
            .rhythm (0, 11, true, false).rhythm (1, 12, false, true).rhythm (2, 4, true, false)
            .vco (0, 0.67f, W::square, 8, 2).vco (1, 0.91f, W::mixed, 6, 6)
            .assign (0, true, false, false).assign (1, true, false, false)
            .levels (0, 0.61f, 0.57f, 0.95f).levels (1, 0.05f, 0.1f, 0.57f)
            .filter (0.11f, 0.5f, -0.44f, 0.9f)
            .envs (0.44f, 0.09f, 0.0f, 0.33f)
            .modes (0, Q::et8, E::on)
            .clock (0.06f, 3)
            .cable (outVcaEg, inCutoff).cable (outClock, inVca)
            .build());

        v.push_back (Builder ("Cloud Walk",
                              "Start with VCO 1 alone, then fade in VCO 2 and the subs. Assign Rhythm 3 to Sequencer 1 for double time.")
            .seq (0, -0.95f, -0.67f, -0.67f, -0.85f)
            .seq (1, -0.44f, -0.89f, -0.74f, -0.44f)
            .rhythm (0, 12, true, true).rhythm (1, 1, false, false).rhythm (2, 9, false, false)
            .vco (0, 0.78f, W::square, 4, 3).vco (1, 0.67f, W::mixed, 2, 6)
            .assign (0, true, false, false).assign (1, true, false, false)
            .levels (0, 0.80f, 0.35f, 0.35f).levels (1, 0.3f, 0.25f, 0.25f)
            .filter (0.05f, 0.39f, -0.22f, 0.9f)
            .envs (0.85f, 0.11f, 0.76f, 0.59f)
            .modes (0, Q::et12, E::on)
            .clock (0.5f, 5)
            .cable (outVcaEg, inCutoff).cable (outVcfEg, inRhythm3)
            .build());

        return v;
    }();
    return presets;
}
} // namespace fractone
