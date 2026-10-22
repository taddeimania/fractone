// Dice.h — rolls steps 2-4 of a sequencer from step 1.
//
// Step 1 is the anchor and is left alone. Each new step lands a consonant
// interval above or below it (unison/octave, minor or major 3rd, perfect 4th
// or 5th, minor or major 6th; never 2nds, 7ths or the tritone), inside the
// SEQ OCT range and, for the 8-step scales, inside the scale. Smaller leaps
// are favoured and a note never sounds three times in a row, so the result
// reads as a phrase. In the chromatic modes each roll picks a major or minor
// flavour and sticks to it, so thirds and sixths don't clash with each other.
// No JUCE dependency.
#pragma once

#include "Dsp.h"
#include "Tuning.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fractone::dice
{
inline int mod12 (int s) { return ((s % 12) + 12) % 12; }

// Intervals counted as consonant, in semitones (any octave).
inline bool isConsonant (int semis)
{
    switch (mod12 (semis))
    {
        case 0: case 3: case 4: case 5: case 7: case 8: case 9: return true;
        default: return false;
    }
}

inline bool isDiatonic (Quant q) { return q == Quant::et8 || q == Quant::ji8; }

inline bool inScale (int semis, Quant q)
{
    if (! isDiatonic (q))
        return true;
    static constexpr bool major[12] { true, false, true, false, true, true, false, true, false, true, false, true };
    return major[mod12 (semis)];
}

// Pitch (octaves above the scale root) of a chromatic position in this tuning.
inline double octavesForSemis (int semis, Quant q)
{
    const int oct = (semis - mod12 (semis)) / 12;
    if (q == Quant::ji12 || q == Quant::ji8)
    {
        static const double ratio[12] { 1.0, 16.0 / 15.0, 9.0 / 8.0, 6.0 / 5.0, 5.0 / 4.0, 4.0 / 3.0,
                                        45.0 / 32.0, 3.0 / 2.0, 8.0 / 5.0, 5.0 / 3.0, 9.0 / 5.0, 15.0 / 8.0 };
        return oct + std::log2 (ratio[mod12 (semis)]);
    }
    return semis / 12.0;
}

// Returns knob values (-1..+1) for steps 2, 3 and 4.
//   anchorKnob  step 1's knob value
//   range       SEQ OCT range in octaves (1, 2 or 5)
inline std::array<float, 3> rollSteps (float anchorKnob, int range, Quant q, uint64_t seed)
{
    dsp::Rng rng (seed * 0x9E3779B97F4A7C15ull + 1);
    auto uniform = [&rng] { return (rng.bipolar() + 1.0) * 0.5; };

    // Same arithmetic as Engine::stepVolts (float knob x int range), so a step 1
    // sitting exactly between two scale notes resolves to the note it plays.
    const float  clamped   = anchorKnob < -1.0f ? -1.0f : (anchorKnob > 1.0f ? 1.0f : anchorKnob);
    const double anchorOct = (double) (clamped * range);
    const bool quantized = q != Quant::off;

    // The anchor as a chromatic position. When quantized this is exactly the
    // note step 1 plays; unquantized, steps keep step 1's offset (fractional part).
    const double anchorQ   = quantized ? quantizeOctaves (anchorOct, q) : anchorOct;
    const int    anchorSem = (int) std::lround (anchorQ * 12.0);
    const double fraction  = quantized ? 0.0 : anchorOct - anchorSem / 12.0;

    // Chromatic modes: one flavour per roll (diatonic scales choose for themselves).
    const bool minorFlavour = ! isDiatonic (q) && uniform() < 0.5;
    auto allowed = [&] (int interval)
    {
        if (! isConsonant (interval))
            return false;
        if (isDiatonic (q))
            return true;
        const int m = mod12 (interval);
        return minorFlavour ? (m != 4 && m != 9) : (m != 3 && m != 8);
    };

    struct Candidate { int semis; double octaves; };
    std::vector<Candidate> pool;
    for (int s = -12 * range - 1; s <= 12 * range + 1; ++s)
    {
        if (! allowed (s - anchorSem) || ! inScale (s, q))
            continue;
        const double oct = (quantized ? octavesForSemis (s, q) : s / 12.0) + fraction;
        if (std::abs (oct) > range + 1.0e-9)
            continue;
        pool.push_back ({ s, oct });
    }

    std::array<float, 3> out { anchorKnob, anchorKnob, anchorKnob };
    if (pool.empty())
        return out;

    for (int attempt = 0; attempt < 32; ++attempt)
    {
        std::array<int, 3> pick {};
        int prev = anchorSem, prevPrev = INT32_MIN;
        for (int i = 0; i < 3; ++i)
        {
            std::vector<double> w (pool.size());
            double total = 0.0;
            for (size_t c = 0; c < pool.size(); ++c)
            {
                const int s = pool[c].semis;
                double weight = std::exp (-std::abs (s - prev) / 5.0)          // prefer small leaps
                              * std::exp (-std::abs (s - anchorSem) / 12.0);   // stay near home
                if (s == prev)
                    weight *= (prev == prevPrev) ? 0.0 : 0.3;                    // no triple repeats
                w[c] = weight;
                total += weight;
            }
            double r = uniform() * total;
            size_t chosen = pool.size() - 1;
            for (size_t c = 0; c < pool.size(); ++c)
            {
                r -= w[c];
                if (r <= 0.0) { chosen = c; break; }
            }
            pick[(size_t) i] = (int) chosen;
            prevPrev = prev;
            prev = pool[chosen].semis;
        }

        // A roll that only repeats step 1 isn't much of a roll.
        bool allAnchor = true;
        for (int i = 0; i < 3; ++i)
            allAnchor = allAnchor && pool[(size_t) pick[(size_t) i]].semis == anchorSem;
        if (allAnchor && pool.size() > 1)
            continue;

        for (int i = 0; i < 3; ++i)
            out[(size_t) i] = (float) dsp::clamp (pool[(size_t) pick[(size_t) i]].octaves / range, -1.0, 1.0);
        return out;
    }
    return out;
}
} // namespace fractone::dice
