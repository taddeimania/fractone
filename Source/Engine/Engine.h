// Engine.h — the complete Fractone instrument as plain C++ (no JUCE):
// two oscillators with two subharmonic dividers each, a saturating mixer,
// a resonant ladder low-pass, two AD envelopes, four rhythm generators, two
// four-step sequencers, the transport, and an internal patchbay.
//
// Everything runs at twice the host rate and is decimated on output.
#pragma once

#include "Dsp.h"
#include "Tuning.h"

#include <array>
#include <cstdint>

namespace fractone
{
// ---------------------------------------------------------------------------
// Patch points
// ---------------------------------------------------------------------------
enum In : int
{
    inVco1, inVco1Sub, inVco1Pwm, inVca,
    inVco2, inVco2Sub, inVco2Pwm, inCutoff,
    inPlay, inReset, inTrigger,
    inRhythm1, inRhythm2, inRhythm3, inRhythm4,
    inClock,
    kNumInputs
};

enum Out : int
{
    outVca,
    outVco1, outVco1Sub1, outVco1Sub2,
    outVcaEg,
    outVco2, outVco2Sub1, outVco2Sub2,
    outVcfEg,
    outSeq1, outSeq1Clk, outSeq2, outSeq2Clk,
    outClock, outTrigger,
    kNumOutputs
};

using Patch = std::array<int, kNumInputs>; // source output per input, -1 = unpatched

inline Patch emptyPatch() { Patch p; p.fill (-1); return p; }

enum class Wave   { square = 0, mixed = 1, saw = 2 }; // switch up / middle / down
enum class EgMode { off = 0, on = 1, held = 2 };
enum class Logic  { orLogic = 0, xorLogic = 1 };

// ---------------------------------------------------------------------------
// Panel state
// ---------------------------------------------------------------------------
struct EngineParams
{
    // Sequencers: step knobs in -1..+1 (centre = no change)
    std::array<std::array<float, 4>, 2> step {};

    // Rhythm generators
    std::array<int, 4> rhythmDiv { 1, 2, 3, 4 };            // 1..16
    std::array<std::array<bool, 2>, 4> rhythmToSeq {};       // [rhythm][seq]
    Logic logic = Logic::orLogic;

    // Clock
    float  tempoBpm = 120.0f;        // free-running TEMPO (1 pulse per beat)
    bool   sync = true;              // follow host tempo / transport
    double syncPulseBeats = 0.25;    // clock pulse length in quarter notes when synced

    // Oscillators
    std::array<float, 2> vcoFreq { 0.0f, 0.0f };             // 0..1 over four octaves from C4
    std::array<Wave, 2>  wave { Wave::saw, Wave::saw };
    std::array<std::array<int, 2>, 2> subDiv { { { 1, 1 }, { 1, 1 } } }; // [vco][sub] 1..16

    int   seqOct = 1;                 // 0: ±1, 1: ±2, 2: ±5 octaves
    Quant quant  = Quant::off;
    std::array<std::array<bool, 3>, 2> assign {};            // [seq][osc, sub1, sub2]

    // Mixer 0..1
    std::array<float, 2> vcoLevel { 0.0f, 0.0f };
    std::array<std::array<float, 2>, 2> subLevel {};          // [vco][sub]

    // Filter / VCA
    float cutoffHz  = 2000.0f;
    float resonance = 0.0f;           // 0..1
    float egAmt     = 0.0f;           // -1..1
    float volume    = 0.7f;           // 0..1

    // Envelopes (seconds)
    float vcfAttack = 0.001f, vcfDecay = 0.3f;
    float vcaAttack = 0.001f, vcaDecay = 0.3f;
    EgMode egMode = EgMode::on;

    // Hidden modes
    bool seqOutUnquantized = false;   // long-press QUANTIZE
    bool seqOutFixedRange  = false;   // long-press SEQ OCT

    // Globals
    float fineCents = 0.0f;           // ±50
    float drift     = 0.25f;          // analog pitch drift amount 0..1
};

struct HostInfo
{
    bool   valid   = false;
    bool   playing = false;
    double ppq     = 0.0;
    double bpm     = 120.0;
};

// Mapping helpers shared with the plugin layer and the MIDI CC map.
inline double attackSecondsFromNorm (double n) { return 0.001 * std::pow (10000.0, dsp::clamp (n, 0.0, 1.0)); }
inline double decaySecondsFromNorm (double n)  { return 0.005 * std::pow (2000.0, dsp::clamp (n, 0.0, 1.0)); }
inline double normFromAttackSeconds (double s) { return std::log (s / 0.001) / std::log (10000.0); }
inline double normFromDecaySeconds (double s)  { return std::log (s / 0.005) / std::log (2000.0); }
inline int    seqOctRange (int index)          { return index <= 0 ? 1 : (index == 1 ? 2 : 5); }

constexpr double kC4Hz = 261.6255653005986;

// ---------------------------------------------------------------------------
class Engine
{
public:
    Engine();

    void prepare (double hostSampleRate);
    void reset();

    void setParams (const EngineParams& p) noexcept { params = p; }
    const EngineParams& getParams() const noexcept { return params; }
    void setPatch (const Patch& p) noexcept { patch = p; }

    // Call once per host block before render(); render() may then be called
    // several times to split the block at MIDI events.
    void beginBlock (const HostInfo& host, int numSamples) noexcept;
    void render (float* out, int numSamples) noexcept;

    // --- Performance & MIDI ------------------------------------------------
    void noteOn (int note) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff() noexcept;
    void controlChange (int cc, int value) noexcept;

    void clearSubOverride (int vco, int sub) noexcept { subOverride[(size_t) vco][(size_t) sub] = 0; }
    void clearEgOverride (int index) noexcept         { egOverride[(size_t) index] = -1.0; }
    void clearLogicOverride() noexcept                { logicOverride = -1; }

    void pressNext() noexcept;
    void pressReset() noexcept;
    void setResetHeld (bool down) noexcept   { resetButton = down; }
    void setTriggerHeld (bool down) noexcept;
    void setPlayOn (bool on) noexcept;
    void togglePlay() noexcept               { setPlayOn (! playOn); }

    // --- State for the panel --------------------------------------------------
    bool  isPlayOn() const noexcept          { return playOn; }
    bool  isRunning() const noexcept         { return running; }
    int   currentStep (int seq) const noexcept { return stepIndex[(size_t) seq]; }
    uint32_t rhythmFires (int r) const noexcept { return fireCount[(size_t) r]; }
    uint32_t clockPulses() const noexcept    { return masterCount; }
    double vcfEnvLevel() const noexcept      { return vcfEnv.level(); }
    double vcaEnvLevel() const noexcept      { return vcaEnv.level(); }
    int   effectiveSubDiv (int vco, int sub) const noexcept { return lastSubDiv[(size_t) vco][(size_t) sub]; }
    int   effectiveRhythmDiv (int r) const noexcept { return lastRhythmDiv[(size_t) r]; }
    double currentVcoHz (int vco) const noexcept { return lastVcoHz[(size_t) vco]; }
    double outputVoltage (int o) const noexcept { return outV[(size_t) o]; }

    // Volts produced by a step knob, after SEQ OCT and QUANTIZE (what the oscillator hears).
    double stepVolts (int seq, int step) const noexcept;

private:
    double tick() noexcept;
    double inV (int input) const noexcept;
    bool   isPatched (int input) const noexcept { return patch[(size_t) input] >= 0; }

    void masterPulse() noexcept;
    void advanceSeq (int seq, bool fromNext) noexcept;
    void doReset() noexcept;
    void triggerEgs() noexcept;
    void updateGate() noexcept;
    Logic currentLogic() const noexcept;

    EngineParams params;
    Patch patch = emptyPatch();

    double hostRate = 48000.0, fs = 96000.0;

    // Voice
    std::array<dsp::DividerBank, 2> bank;
    dsp::Ladder ladder;
    dsp::AdEnvelope vcfEnv, vcaEnv;
    dsp::Decimator2 decimator;
    dsp::DcBlocker dcBlock;
    std::array<dsp::Smoother, 2> knobPitchSmooth, ccPitchSmooth;
    std::array<dsp::Smoother, 2> vcoLevelSmooth;
    std::array<std::array<dsp::Smoother, 2>, 2> subLevelSmooth;
    dsp::Smoother cutoffSmooth, resSmooth, egAmtSmooth, volSmooth;
    dsp::Rng rng;
    std::array<double, 2> driftTarget {}, driftValue {};
    int driftCounter = 0;

    // Patch voltages (previous tick)
    std::array<double, kNumOutputs> outV {};

    // Clocking
    double internalPhase = 0.0;
    HostInfo host;
    double hostPpq = 0.0, hostPpqPerTick = 0.0;
    int64_t lastGrid = 0;
    double lastGridLen = 0.25;
    bool hostWasPlaying = false;
    bool hostPlayingNow = false;
    double expectedPpq = 0.0;
    bool   havePpq = false;
    int    blockSamples = 0;

    bool playOn  = true;
    bool running = false;
    uint32_t k = 0;               // master pulses since reset
    uint32_t masterCount = 0;     // for the panel
    double ticksSincePulse = 0.0, pulsePeriod = 4800.0;

    // Sequencers
    std::array<int, 2>  stepIndex { 0, 0 };
    std::array<bool, 2> armed { true, true };

    // Rhythm
    std::array<uint32_t, 4> fireCount {};
    std::array<int, 4> lastRhythmDiv { 1, 1, 1, 1 };

    // Gates / triggers
    bool resetButton = false, resetHeld = false;
    bool triggerButton = false;
    bool gateActive = false;
    dsp::EdgeDetector playIn, resetIn, triggerIn, clockIn;

    // Output pulse timers (ticks remaining)
    int clockOutTicks = 0, triggerOutTicks = 0;
    std::array<int, 2> seqClkTicks { 0, 0 };

    // MIDI
    std::array<int, 16> heldNotes {};
    int numHeld = 0;
    double midiPitchV = 0.0;
    std::array<int, 2> ccPitchMsb { -1, -1 };
    std::array<int, 2> ccPitchLsb { 0, 0 };
    std::array<double, 2> ccPitchV { 0.0, 0.0 };
    std::array<std::array<int, 2>, 2> subOverride {};       // 0 = none, else 1..16
    std::array<double, 4> egOverride { -1.0, -1.0, -1.0, -1.0 }; // seconds; vcfA, vcfD, vcaA, vcaD
    std::array<int, 4> egMsb { 0, 0, 0, 0 };
    int logicOverride = -1;

    // Panel readouts
    std::array<std::array<int, 2>, 2> lastSubDiv { { { 1, 1 }, { 1, 1 } } };
    std::array<double, 2> lastVcoHz { kC4Hz, kC4Hz };
};
} // namespace fractone
