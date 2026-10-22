// Dsp.h — small building blocks for the Fractone voice: band-limited
// oscillator shapes, a resonant 4-pole ladder low-pass, attack/decay
// envelopes, a halfband decimator and a few utilities. No JUCE dependency.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fractone::dsp
{
constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double clamp (double v, double lo, double hi) { return std::min (hi, std::max (lo, v)); }

// ---------------------------------------------------------------------------
// PolyBLEP residual for a unit step at phase 0 (phase t in [0,1), increment dt)
// ---------------------------------------------------------------------------
inline double polyBlep (double t, double dt) noexcept
{
    if (dt <= 0.0)
        return 0.0;
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

inline double sawWave (double t, double dt) noexcept
{
    return 2.0 * t - 1.0 - polyBlep (t, dt);
}

// Pulse with duty cycle `duty` (fraction of the cycle spent high).
inline double pulseWave (double t, double dt, double duty) noexcept
{
    duty = clamp (duty, 0.01, 0.99);
    double v = t < duty ? 1.0 : -1.0;
    v += polyBlep (t, dt);
    double t2 = t - duty;
    if (t2 < 0.0)
        t2 += 1.0;
    v -= polyBlep (t2, dt);
    return v;
}

// ---------------------------------------------------------------------------
// Phase-locked oscillator bank: one master phase plus integer subdivisions.
// A sub at divisor n runs at exactly f/n and stays locked to the master,
// the way a hardware frequency divider does.
// ---------------------------------------------------------------------------
class DividerBank
{
public:
    static constexpr uint32_t kCycleWrap = 720720; // lcm(1..16)

    void reset() noexcept { phase = 0.0; cycles = 0; }

    void advance (double inc) noexcept
    {
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            cycles = (cycles + 1) % kCycleWrap;
        }
    }

    double masterPhase() const noexcept { return phase; }

    double subPhase (int n) const noexcept
    {
        if (n <= 1)
            return phase;
        return ((double) (cycles % (uint32_t) n) + phase) / (double) n;
    }

private:
    double   phase  = 0.0;
    uint32_t cycles = 0;
};

// ---------------------------------------------------------------------------
// Ladder low-pass: 4 cascaded one-pole TPT sections with global feedback,
// solved implicitly for the linear part and saturated at the input.
// Self-oscillates as k approaches 4.
// ---------------------------------------------------------------------------
class Ladder
{
public:
    void prepare (double sampleRate) noexcept { fs = sampleRate; reset(); }
    void reset() noexcept { s.fill (0.0); }

    double process (double x, double cutoffHz, double k) noexcept
    {
        cutoffHz = clamp (cutoffHz, 5.0, 0.45 * fs);
        const double g  = std::tan (kPi * cutoffHz / fs);
        const double G  = g / (1.0 + g);
        const double G2 = G * G;
        const double G3 = G2 * G;
        const double G4 = G2 * G2;

        const double S = (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]) / (1.0 + g);

        // Partial passband-loss compensation keeps high resonance usable.
        const double in = x * (1.0 + 0.45 * k);
        double u = (in - k * S) / (1.0 + k * G4);
        u = std::tanh (u);

        double y = u;
        for (auto& st : s)
        {
            const double v = (y - st) * G;
            y  = v + st;
            st = y + v;
        }
        return y;
    }

private:
    double fs = 48000.0;
    std::array<double, 4> s {};
};

// ---------------------------------------------------------------------------
// Attack / decay envelope generator.
// - A trigger runs the attack to the top, then decays.
// - A gate holds at the top until it ends, then decays.
// - Neither a trigger nor a gate restarts the envelope during its attack.
// - Attack restarts from the current level (analog style).
// ---------------------------------------------------------------------------
class AdEnvelope
{
public:
    enum class Stage { idle, attack, hold, decay };

    void prepare (double sampleRate) noexcept { fs = sampleRate; setTimes (0.001, 0.2); }

    void setTimes (double attackSec, double decaySec) noexcept
    {
        attackSec = std::max (0.0005, attackSec);
        decaySec  = std::max (0.002, decaySec);
        if (std::abs (attackSec - lastA) > 1.0e-9)
        {
            // RC charge toward an overshoot target, reaching 1.0 after attackSec.
            constexpr double target = 1.2;
            const double tau = attackSec / std::log (target / (target - 1.0));
            attackCoef = std::exp (-1.0 / (tau * fs));
            lastA = attackSec;
        }
        if (std::abs (decaySec - lastD) > 1.0e-9)
        {
            // Exponential fall to about -40 dB over decaySec.
            const double tau = decaySec / std::log (100.0);
            decayCoef = std::exp (-1.0 / (tau * fs));
            lastD = decaySec;
        }
    }

    // Returns true if the envelope actually (re)started.
    bool trigger() noexcept
    {
        if (stage == Stage::attack)
            return false;
        stage = Stage::attack;
        gate  = false;
        return true;
    }

    bool gateOn() noexcept
    {
        gate = true;
        if (stage == Stage::attack || stage == Stage::hold)
            return false;
        stage = Stage::attack;
        return true;
    }

    void gateOff() noexcept
    {
        gate = false;
        if (stage == Stage::hold)
            stage = Stage::decay;
    }

    double tick() noexcept
    {
        switch (stage)
        {
            case Stage::attack:
            {
                constexpr double target = 1.2;
                value = target + (value - target) * attackCoef;
                if (value >= 1.0)
                {
                    value = 1.0;
                    stage = gate ? Stage::hold : Stage::decay;
                }
                break;
            }
            case Stage::hold:
                value = 1.0;
                if (! gate)
                    stage = Stage::decay;
                break;
            case Stage::decay:
                value *= decayCoef;
                if (value < 1.0e-5)
                {
                    value = 0.0;
                    stage = Stage::idle;
                }
                break;
            case Stage::idle:
                break;
        }
        return value;
    }

    double level() const noexcept { return value; }
    Stage  getStage() const noexcept { return stage; }
    void   reset() noexcept { value = 0.0; stage = Stage::idle; gate = false; }

private:
    double fs = 48000.0;
    double value = 0.0;
    double attackCoef = 0.0, decayCoef = 0.0;
    double lastA = -1.0, lastD = -1.0;
    Stage  stage = Stage::idle;
    bool   gate  = false;
};

// ---------------------------------------------------------------------------
// One-pole smoother
// ---------------------------------------------------------------------------
class Smoother
{
public:
    void prepare (double sampleRate, double timeSec) noexcept
    {
        coef = std::exp (-1.0 / (std::max (1.0e-4, timeSec) * sampleRate));
    }
    void   snap (double v) noexcept { y = v; }
    double next (double target) noexcept { y = target + (y - target) * coef; return y; }
    double value() const noexcept { return y; }

private:
    double coef = 0.0, y = 0.0;
};

// ---------------------------------------------------------------------------
// 2:1 halfband FIR decimator (windowed sinc, Blackman)
// ---------------------------------------------------------------------------
class Decimator2
{
public:
    static constexpr int kTaps = 63;

    Decimator2()
    {
        const int mid = kTaps / 2;
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const int    n = i - mid;
            const double x = n == 0 ? 0.5 : std::sin (kPi * 0.5 * n) / (kPi * n);
            const double w = 0.42 - 0.5 * std::cos (kTwoPi * i / (kTaps - 1)) + 0.08 * std::cos (2.0 * kTwoPi * i / (kTaps - 1));
            h[(size_t) i] = x * w;
            sum += h[(size_t) i];
        }
        for (auto& c : h)
            c /= sum;
        reset();
    }

    void reset() noexcept { buf.fill (0.0); pos = 0; }

    // Push two oversampled inputs, get one output.
    double process (double a, double b) noexcept
    {
        push (a);
        push (b);
        double acc = 0.0;
        for (int i = 0; i < kTaps; ++i)
            acc += h[(size_t) i] * buf[(size_t) (pos + i)];
        return acc;
    }

private:
    void push (double v) noexcept
    {
        pos = (pos == 0 ? kTaps : pos) - 1;
        buf[(size_t) pos] = v;
        buf[(size_t) (pos + kTaps)] = v;
    }

    std::array<double, kTaps> h {};
    std::array<double, kTaps * 2> buf {};
    int pos = 0;
};

// ---------------------------------------------------------------------------
// DC blocker
// ---------------------------------------------------------------------------
class DcBlocker
{
public:
    void   prepare (double sampleRate) noexcept { r = std::exp (-kTwoPi * 8.0 / sampleRate); x1 = y1 = 0.0; }
    double process (double x) noexcept { const double y = x - x1 + r * y1; x1 = x; y1 = y; return y; }

private:
    double r = 0.999, x1 = 0.0, y1 = 0.0;
};

// ---------------------------------------------------------------------------
// Tiny RNG (xorshift)
// ---------------------------------------------------------------------------
class Rng
{
public:
    explicit Rng (uint64_t seed = 0x9E3779B97F4A7C15ull) : s (seed ? seed : 1) {}
    uint64_t next() noexcept { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double   bipolar() noexcept { return (double) (next() >> 11) * (2.0 / 9007199254740992.0) - 1.0; }

private:
    uint64_t s;
};

// Schmitt-trigger edge detector for gate/trigger/clock inputs (volts).
class EdgeDetector
{
public:
    // Returns +1 on rising edge, -1 on falling edge, 0 otherwise.
    int process (double v) noexcept
    {
        if (! high && v > 1.0)  { high = true;  return 1; }
        if (high && v < 0.5)    { high = false; return -1; }
        return 0;
    }
    bool isHigh() const noexcept { return high; }
    void reset() noexcept { high = false; }

private:
    bool high = false;
};
} // namespace fractone::dsp
