// Tuning.h — quantize tables. Pitches are handled in volts, 1 V per octave.
#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace fractone
{
enum class Quant { off = 0, et12, et8, ji12, ji8 };

inline const std::vector<double>& scaleDegrees (Quant q)
{
    auto fromRatios = [] (std::initializer_list<double> ratios)
    {
        std::vector<double> v;
        for (double r : ratios)
            v.push_back (std::log2 (r));
        v.push_back (1.0); // octave, so the nearest search can round up
        return v;
    };
    auto fromSemis = [] (std::initializer_list<int> semis)
    {
        std::vector<double> v;
        for (int s : semis)
            v.push_back (s / 12.0);
        v.push_back (1.0);
        return v;
    };

    static const std::vector<double> et12 = fromSemis ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
    static const std::vector<double> et8  = fromSemis ({ 0, 2, 4, 5, 7, 9, 11 });
    // 5-limit just chromatic scale
    static const std::vector<double> ji12 = fromRatios ({ 1.0, 16.0 / 15.0, 9.0 / 8.0, 6.0 / 5.0, 5.0 / 4.0, 4.0 / 3.0,
                                                          45.0 / 32.0, 3.0 / 2.0, 8.0 / 5.0, 5.0 / 3.0, 9.0 / 5.0, 15.0 / 8.0 });
    // Just major (diatonic) scale
    static const std::vector<double> ji8  = fromRatios ({ 1.0, 9.0 / 8.0, 5.0 / 4.0, 4.0 / 3.0, 3.0 / 2.0, 5.0 / 3.0, 15.0 / 8.0 });
    static const std::vector<double> none;

    switch (q)
    {
        case Quant::et12: return et12;
        case Quant::et8:  return et8;
        case Quant::ji12: return ji12;
        case Quant::ji8:  return ji8;
        case Quant::off:  break;
    }
    return none;
}

// Snap a pitch (in octaves above the scale root) to the nearest scale degree.
inline double quantizeOctaves (double v, Quant q)
{
    if (q == Quant::off)
        return v;

    const auto& deg = scaleDegrees (q);
    const double oct  = std::floor (v);
    const double frac = v - oct;

    double best = deg.front();
    double bestDist = 1.0e9;
    for (double d : deg)
    {
        const double dist = std::abs (frac - d);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = d;
        }
    }
    return oct + best;
}

inline const char* quantName (Quant q)
{
    switch (q)
    {
        case Quant::et12: return "12-ET";
        case Quant::et8:  return "8-ET";
        case Quant::ji12: return "12-JI";
        case Quant::ji8:  return "8-JI";
        case Quant::off:  break;
    }
    return "Off";
}
} // namespace fractone
