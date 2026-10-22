// Params.h — parameter IDs and layout for Fractone.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/Engine.h"

namespace fractone::ids
{
inline juce::String step (int seq, int i)       { return "seq" + juce::String (seq + 1) + "_step" + juce::String (i + 1); }
inline juce::String rhythm (int r)              { return "rhythm" + juce::String (r + 1); }
inline juce::String rhythmToSeq (int r, int s)  { return "rhythm" + juce::String (r + 1) + "_seq" + juce::String (s + 1); }
inline juce::String vcoFreq (int v)             { return "vco" + juce::String (v + 1) + "_freq"; }
inline juce::String vcoWave (int v)             { return "vco" + juce::String (v + 1) + "_wave"; }
inline juce::String subDiv (int v, int j)       { return "vco" + juce::String (v + 1) + "_sub" + juce::String (j + 1) + "_div"; }
inline juce::String assign (int s, int t)       { static const char* n[] { "osc", "sub1", "sub2" }; return "seq" + juce::String (s + 1) + "_assign_" + n[t]; }
inline juce::String vcoLevel (int v)            { return "vco" + juce::String (v + 1) + "_level"; }
inline juce::String subLevel (int v, int j)     { return "vco" + juce::String (v + 1) + "_sub" + juce::String (j + 1) + "_level"; }

inline constexpr const char* tempo        = "tempo";
inline constexpr const char* seqOct       = "seq_oct";
inline constexpr const char* quantize     = "quantize";
inline constexpr const char* cutoff       = "cutoff";
inline constexpr const char* resonance    = "resonance";
inline constexpr const char* egAmt        = "vcf_eg_amt";
inline constexpr const char* volume       = "volume";
inline constexpr const char* vcfAttack    = "vcf_attack";
inline constexpr const char* vcfDecay     = "vcf_decay";
inline constexpr const char* vcaAttack    = "vca_attack";
inline constexpr const char* vcaDecay     = "vca_decay";
inline constexpr const char* egMode       = "eg_mode";
inline constexpr const char* seqOutUnq    = "seq_out_unquantized";
inline constexpr const char* seqOutFixed  = "seq_out_fixed_range";
inline constexpr const char* sync         = "sync";
inline constexpr const char* syncRate     = "sync_rate";
inline constexpr const char* fineTune     = "fine_tune";
inline constexpr const char* logic        = "rhythm_logic";
inline constexpr const char* midiChannel  = "midi_channel";
inline constexpr const char* drift        = "drift";

inline const juce::StringArray& waveNames()     { static const juce::StringArray n { "Square", "Pulse + Saw Subs", "Saw" }; return n; }
inline const juce::StringArray& seqOctNames()   { static const juce::StringArray n { "+/-1 oct", "+/-2 oct", "+/-5 oct" }; return n; }
inline const juce::StringArray& quantNames()    { static const juce::StringArray n { "Off", "12-ET", "8-ET", "12-JI", "8-JI" }; return n; }
inline const juce::StringArray& egModeNames()   { static const juce::StringArray n { "Off", "On", "Held" }; return n; }
inline const juce::StringArray& logicNames()    { static const juce::StringArray n { "OR", "XOR" }; return n; }
inline const juce::StringArray& syncRateNames() { static const juce::StringArray n { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T" }; return n; }

inline double syncRateBeats (int index)
{
    static constexpr double beats[] { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 1.0 / 3.0, 1.0 / 6.0 };
    return beats[juce::jlimit (0, 8, index)];
}

inline juce::String noteNameForHz (double hz)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int n = juce::roundToInt (midi);
    const int cents = juce::roundToInt ((midi - n) * 100.0);
    juce::String s = juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    if (cents != 0)
        s << (cents > 0 ? " +" : " ") << cents << "c";
    return s;
}
} // namespace fractone::ids

namespace fractone
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });

    auto seconds = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s";
    });

    auto divisor = [] (const String& id, const String& name, int def)
    {
        return std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, 1, 16, def,
            AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return "1/" + String (v); }));
    };

    // --- Sequencers ----------------------------------------------------------
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < 4; ++i)
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::step (s, i), 1 },
                "Seq " + String (s + 1) + " Step " + String (i + 1), NormalisableRange<float> (-1.0f, 1.0f),
                s == 0 ? std::array<float, 4> { 0.0f, 7.0f / 24.0f, 12.0f / 24.0f, 3.0f / 24.0f }[(size_t) i] : 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                {
                    return (v >= 0 ? "+" : "") + String (v * 100.0f, 0) + "%";
                })));

    for (int s = 0; s < 2; ++s)
        for (int t = 0; t < 3; ++t)
        {
            static const char* names[] { "Osc", "Sub 1", "Sub 2" };
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::assign (s, t), 1 },
                "Seq " + String (s + 1) + " Assign " + names[t], t == 0));
        }

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::seqOct, 1 }, "Seq Oct", ids::seqOctNames(), 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::quantize, 1 }, "Quantize", ids::quantNames(), 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::seqOutUnq, 1 }, "Seq Out Unquantized", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::seqOutFixed, 1 }, "Seq Out Fixed +/-5", false));

    // --- Rhythm generators ---------------------------------------------------------
    for (int r = 0; r < 4; ++r)
        layout.add (divisor (ids::rhythm (r), "Rhythm " + String (r + 1), std::array<int, 4> { 2, 3, 4, 6 }[(size_t) r]));
    for (int r = 0; r < 4; ++r)
        for (int s = 0; s < 2; ++s)
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rhythmToSeq (r, s), 1 },
                "Rhythm " + String (r + 1) + " > Seq " + String (s + 1), r == 0 && s == 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::logic, 1 }, "Rhythm Logic", ids::logicNames(), 0));

    // --- Clock -------------------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::tempo, 1 }, "Tempo",
        NormalisableRange<float> (20.0f, 3000.0f, 0.0f, 0.5f), 240.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " BPM"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::sync, 1 }, "Sync To Host", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::syncRate, 1 }, "Sync Rate", ids::syncRateNames(), 4));

    // --- Oscillators ---------------------------------------------------------------------
    for (int v = 0; v < 2; ++v)
    {
        const String n = "VCO " + String (v + 1);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::vcoFreq (v), 1 }, n + " Freq",
            NormalisableRange<float> (0.0f, 1.0f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int)
            {
                return ids::noteNameForHz (kC4Hz * std::pow (2.0, x * 4.0));
            })));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::vcoWave (v), 1 }, n + " Wave", ids::waveNames(), 2));
        for (int j = 0; j < 2; ++j)
            layout.add (divisor (ids::subDiv (v, j), n + " Sub " + String (j + 1) + " Freq", j == 0 ? 2 : 3));
    }

    // --- Mixer -------------------------------------------------------------------------------
    for (int v = 0; v < 2; ++v)
    {
        const String n = "VCO " + String (v + 1);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::vcoLevel (v), 1 }, n + " Level",
            NormalisableRange<float> (0.0f, 1.0f), v == 0 ? 0.5f : 0.0f, pct));
        for (int j = 0; j < 2; ++j)
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::subLevel (v, j), 1 },
                n + " Sub " + String (j + 1) + " Level", NormalisableRange<float> (0.0f, 1.0f), (v == 0 && j == 0) ? 0.3f : 0.0f, pct));
    }

    // --- Filter / VCA -----------------------------------------------------------------------
    // Exponential knob: value = lo * (hi/lo)^n
    auto expRange = [] (float lo, float hi)
    {
        return NormalisableRange<float> (lo, hi,
            [] (float start, float end, float n) { return start * std::pow (end / start, n); },
            [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); });
    };
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::cutoff, 1 }, "Cutoff",
        expRange (20.0f, 20000.0f), 1200.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 2) + " kHz";
        })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::resonance, 1 }, "Resonance",
        NormalisableRange<float> (0.0f, 1.0f), 0.2f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::egAmt, 1 }, "VCF EG Amount",
        NormalisableRange<float> (-1.0f, 1.0f), 0.35f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return (v >= 0 ? "+" : "") + String (roundToInt (v * 100.0f)) + "%";
        })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::volume, 1 }, "Volume",
        NormalisableRange<float> (0.0f, 1.0f), 0.75f, pct));

    // --- Envelopes ----------------------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::vcfAttack, 1 }, "VCF Attack", expRange (0.001f, 10.0f), 0.001f, seconds));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::vcfDecay, 1 },  "VCF Decay",  expRange (0.005f, 10.0f), 0.25f, seconds));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::vcaAttack, 1 }, "VCA Attack", expRange (0.001f, 10.0f), 0.001f, seconds));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::vcaDecay, 1 },  "VCA Decay",  expRange (0.005f, 10.0f), 0.35f, seconds));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::egMode, 1 }, "EG Mode", ids::egModeNames(), 1));

    // --- Globals ------------------------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::fineTune, 1 }, "Fine Tune",
        NormalisableRange<float> (-50.0f, 50.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return (v >= 0 ? "+" : "") + String (v, 1) + " cents";
        })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::drift, 1 }, "Analog Drift",
        NormalisableRange<float> (0.0f, 1.0f), 0.25f, pct));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::midiChannel, 1 }, "MIDI Channel", 0, 16, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? String ("All") : String (v); })));

    return layout;
}
} // namespace fractone
