// EngineTests.cpp — behaviour tests for the Fractone engine (no JUCE needed).
#include "Engine/Engine.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace fractone;

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.c_str());
    }
}

void near (double a, double b, double tol, const std::string& what)
{
    check (std::abs (a - b) <= tol, what + " (got " + std::to_string (a) + ", want " + std::to_string (b) + ")");
}

constexpr double kRate = 48000.0;

EngineParams quietParams()
{
    EngineParams p;
    p.sync = false;
    p.tempoBpm = 120.0f;
    p.egMode = EgMode::on;
    p.drift = 0.0f;
    return p;
}

// Render n host samples one at a time, calling `each` after every sample.
void run (Engine& e, int n, const std::function<void (int)>& each = {})
{
    float s = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        e.render (&s, 1);
        if (each) each (i);
    }
}

// Frequency of a patch output from rising zero crossings over `seconds`.
double measureHz (Engine& e, int out, double seconds)
{
    int crossings = 0;
    double prev = e.outputVoltage (out);
    int first = -1, last = -1;
    const int n = (int) (seconds * kRate);
    run (e, n, [&] (int i)
    {
        const double v = e.outputVoltage (out);
        if (prev < 0.0 && v >= 0.0)
        {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
        prev = v;
    });
    if (crossings < 2) return 0.0;
    return (crossings - 1) * kRate / (double) (last - first);
}

void startEngine (Engine& e, const EngineParams& p, bool play = true)
{
    e.setParams (p);
    e.prepare (kRate);
    e.setPlayOn (play);
    e.beginBlock ({}, 1);
}

// ---------------------------------------------------------------------------
void testQuantize()
{
    std::puts ("quantize");
    near (quantizeOctaves (0.04, Quant::et12), 0.0, 1e-9, "12-ET snaps down");
    near (quantizeOctaves (0.05, Quant::et12), 1.0 / 12.0, 1e-9, "12-ET snaps up");
    near (quantizeOctaves (1.0 / 12.0, Quant::et8), 0.0, 1e-9, "8-ET has no minor second");
    near (quantizeOctaves (std::log2 (1.5) + 0.004, Quant::ji12), std::log2 (1.5), 1e-9, "12-JI pure fifth");
    near (quantizeOctaves (std::log2 (5.0 / 4.0) - 0.01, Quant::ji8), std::log2 (1.25), 1e-9, "8-JI pure third");
    near (quantizeOctaves (-0.02, Quant::et12), 0.0, 1e-9, "negative rounds to root");
    near (quantizeOctaves (-0.5, Quant::et12), -0.5, 1e-9, "tritone below");
    near (quantizeOctaves (0.37, Quant::off), 0.37, 1e-12, "off passes through");
}

void testSubharmonics()
{
    std::puts ("subharmonics");
    for (int n : { 1, 2, 3, 5, 7, 16 })
    {
        auto p = quietParams();
        p.subDiv[0][0] = n;
        Engine e;
        startEngine (e, p, false);
        const double hz = measureHz (e, outVco1Sub1, 1.0);
        near (hz, kC4Hz / n, std::max (0.2, kC4Hz / n * 0.002), "VCO1 SUB1 at f/" + std::to_string (n));
    }

    auto p = quietParams();
    p.vcoFreq[0] = 1.0f; // four octaves up: C8
    Engine e;
    startEngine (e, p, false);
    near (measureHz (e, outVco1, 0.5), kC4Hz * 16.0, 8.0, "VCO1 FREQ full = C8 (4186 Hz)");
}

void testWaveSwitch()
{
    std::puts ("wave switch");
    // Middle position: sub 1 saw normalled to VCO pulse width -> VCO duty must move.
    auto p = quietParams();
    p.wave[0] = Wave::mixed;
    p.subDiv[0][0] = 4;
    Engine e;
    startEngine (e, p, false);
    double minDuty = 1.0, maxDuty = 0.0;
    int highCount = 0, cycleLen = 0;
    double prev = e.outputVoltage (outVco1);
    run (e, (int) (0.2 * kRate), [&] (int)
    {
        const double v = e.outputVoltage (outVco1);
        ++cycleLen;
        if (v > 0.0) ++highCount;
        if (prev < 0.0 && v >= 0.0)
        {
            if (cycleLen > 20)
            {
                const double d = (double) highCount / cycleLen;
                minDuty = std::min (minDuty, d);
                maxDuty = std::max (maxDuty, d);
            }
            highCount = 0;
            cycleLen = 0;
        }
        prev = v;
    });
    check (maxDuty - minDuty > 0.3, "mixed mode: VCO pulse width is modulated by SUB 1");

    // PWM patch overrides the normal: patch VCF EG (0 V, idle) -> fixed 50 %.
    Patch patch = emptyPatch();
    patch[inVco1Pwm] = outVcfEg;
    Engine e2;
    startEngine (e2, p, false);
    e2.setPatch (patch);
    minDuty = 1.0; maxDuty = 0.0; highCount = 0; cycleLen = 0;
    prev = e2.outputVoltage (outVco1);
    run (e2, (int) (0.2 * kRate), [&] (int)
    {
        const double v = e2.outputVoltage (outVco1);
        ++cycleLen;
        if (v > 0.0) ++highCount;
        if (prev < 0.0 && v >= 0.0)
        {
            if (cycleLen > 20)
            {
                const double d = (double) highCount / cycleLen;
                minDuty = std::min (minDuty, d);
                maxDuty = std::max (maxDuty, d);
            }
            highCount = 0;
            cycleLen = 0;
        }
        prev = v;
    });
    check (maxDuty - minDuty < 0.08, "patched PWM input replaces the SUB 1 normal");
}

int countStepChanges (Engine& e, int seq, double seconds)
{
    int changes = 0;
    int prev = e.currentStep (seq);
    run (e, (int) (seconds * kRate), [&] (int)
    {
        const int s = e.currentStep (seq);
        if (s != prev) ++changes;
        prev = s;
    });
    return changes;
}

void testRhythm()
{
    std::puts ("rhythm generators");
    // 600 BPM free clock = 10 pulses / s. RHYTHM 1 at /3 -> advance every 3 pulses.
    auto p = quietParams();
    p.tempoBpm = 600.0f;
    p.rhythmDiv[0] = 3;
    p.rhythmToSeq[0][0] = true;
    Engine e;
    startEngine (e, p);
    run (e, 10);
    const uint32_t pulses0 = e.clockPulses();
    run (e, (int) (2.995 * kRate));
    const uint32_t pulses = e.clockPulses() - pulses0;
    near (pulses, 30, 1, "600 BPM gives 10 clock pulses per second");
    near (e.rhythmFires (0), 10, 1, "rhythm /3 fires every third pulse");

    // Polyrhythm OR vs XOR on one sequencer: /2 and /3 over one 6-pulse cycle.
    auto fires = [] (Logic logic)
    {
        auto q = quietParams();
        q.tempoBpm = 600.0f;
        q.rhythmDiv[0] = 2;
        q.rhythmDiv[1] = 3;
        q.rhythmToSeq[0][0] = true;
        q.rhythmToSeq[1][0] = true;
        q.logic = logic;
        q.vcaDecay = 0.005f;
        Engine e;
        startEngine (e, q);
        int advances = 0;
        int prevStep = 0;
        // 12 pulses = 1.2 s; first advance after reset plays step 1 (no change).
        run (e, (int) (1.199 * kRate), [&] (int)
        {
            if (e.currentStep (0) != prevStep) ++advances;
            prevStep = e.currentStep (0);
        });
        return advances;
    };
    // pulses k = 0..11. /2 fires 0,2,4,6,8,10 ; /3 fires 0,3,6,9.
    // OR: 0,2,3,4,6,8,9,10 = 8 events, first one holds step 1 -> 7 changes.
    // XOR: 2,3,4,8,9,10 = 6 events (0 and 6 cancel) -> first event at 2 holds -> 5 changes.
    check (fires (Logic::orLogic) == 7, "OR logic advances on any rhythm (" + std::to_string (fires (Logic::orLogic)) + ")");
    check (fires (Logic::xorLogic) == 5, "XOR logic skips coincident pulses (" + std::to_string (fires (Logic::xorLogic)) + ")");

    // No rhythm assigned -> sequencer never moves.
    auto q = quietParams();
    q.tempoBpm = 600.0f;
    Engine e2;
    startEngine (e2, q);
    check (countStepChanges (e2, 0, 1.0) == 0, "sequencer without a rhythm source stays put");
}

void testTransport()
{
    std::puts ("transport");
    auto p = quietParams();
    p.tempoBpm = 600.0f;
    p.rhythmToSeq[0][0] = true;
    Engine e;
    startEngine (e, p);
    run (e, (int) (0.25 * kRate));
    check (e.currentStep (0) != 0, "sequencer advances while playing");

    e.pressReset();
    check (e.currentStep (0) == 0, "RESET returns to step 1");

    // Hold RESET: stays on step 1 while EGs keep firing.
    e.setResetHeld (true);
    run (e, (int) (0.5 * kRate));
    check (e.currentStep (0) == 0, "holding RESET holds step 1");
    check (e.vcaEnvLevel() > 0.0 || e.rhythmFires (0) > 0, "EGs still triggered while held");
    e.pressNext();
    check (e.currentStep (0) == 1, "NEXT advances while RESET is held");
    e.setResetHeld (false);

    // PLAY off -> no clock.
    e.setPlayOn (false);
    const auto pulses = e.clockPulses();
    run (e, (int) (0.5 * kRate));
    check (e.clockPulses() == pulses, "no clock while stopped");

    // NEXT while stopped still steps.
    const int s = e.currentStep (0);
    e.pressNext();
    check (e.currentStep (0) == (s + 1) % 4, "NEXT steps while stopped");
}

void testEnvelopes()
{
    std::puts ("envelopes");
    dsp::AdEnvelope env;
    env.prepare (kRate);
    env.setTimes (1.0, 1.0);
    env.trigger();
    double prev = 0.0;
    bool monotonic = true;
    for (int i = 0; i < (int) (0.9 * kRate); ++i)
    {
        if (i == (int) (0.5 * kRate))
            check (! env.trigger(), "trigger ignored during attack");
        const double v = env.tick();
        if (v < prev) monotonic = false;
        prev = v;
    }
    check (monotonic, "attack keeps rising through a retrigger");

    // Attack time: 1 s to reach the top.
    dsp::AdEnvelope e2;
    e2.prepare (kRate);
    e2.setTimes (0.1, 0.5);
    e2.trigger();
    int samplesToTop = 0;
    while (e2.getStage() == dsp::AdEnvelope::Stage::attack && samplesToTop < (int) kRate)
    {
        e2.tick();
        ++samplesToTop;
    }
    near (samplesToTop / kRate, 0.1, 0.005, "attack time");

    // Gate holds at max, decays after release.
    dsp::AdEnvelope e3;
    e3.prepare (kRate);
    e3.setTimes (0.001, 0.05);
    e3.gateOn();
    for (int i = 0; i < 4800; ++i) e3.tick();
    near (e3.level(), 1.0, 1e-9, "gate holds at maximum");
    e3.gateOff();
    for (int i = 0; i < 4800; ++i) e3.tick();
    check (e3.level() < 0.01, "decays after the gate ends");

    // EG OFF: sequencer steps don't fire the EGs; EG HELD: held open.
    auto p = quietParams();
    p.tempoBpm = 600.0f;
    p.rhythmToSeq[0][0] = true;
    p.egMode = EgMode::off;
    Engine e;
    startEngine (e, p);
    double peak = 0.0;
    run (e, (int) (0.5 * kRate), [&] (int) { peak = std::max (peak, e.vcaEnvLevel()); });
    check (peak == 0.0, "EG OFF: steps do not trigger envelopes");

    p.egMode = EgMode::held;
    e.setParams (p);
    run (e, (int) (0.1 * kRate));
    near (e.vcaEnvLevel(), 1.0, 1e-9, "EG HELD: envelopes held open");

    // TRIGGER button with EG OFF acts as a gate.
    p.egMode = EgMode::off;
    p.vcaDecay = 0.01f;
    e.setParams (p);
    run (e, (int) (0.2 * kRate));
    e.setTriggerHeld (true);
    run (e, (int) (0.2 * kRate));
    near (e.vcaEnvLevel(), 1.0, 1e-9, "TRIGGER held (EG off) gates the EGs");
    e.setTriggerHeld (false);
    run (e, (int) (0.2 * kRate));
    check (e.vcaEnvLevel() < 0.01, "release starts decay");
}

void testHostSync()
{
    std::puts ("host sync");
    auto p = quietParams();
    p.sync = true;
    p.syncPulseBeats = 0.25;
    Engine e;
    e.setParams (p);
    e.prepare (kRate);

    HostInfo h { true, true, 0.0, 120.0 };
    const int block = 512;
    float buf[block];
    double ppq = 0.0;
    int samples = 0;
    while (samples < (int) kRate)
    {
        h.ppq = ppq;
        e.beginBlock (h, block);
        e.render (buf, block);
        samples += block;
        ppq += block * 120.0 / 60.0 / kRate;
    }
    // 120 BPM: 16ths every 0.125 s -> 8 pulses in the first second (0 .. 0.875 s).
    check (e.clockPulses() == 8 || e.clockPulses() == 9, "16th-note clock follows host tempo (" + std::to_string (e.clockPulses()) + ")");

    // Host stopped -> no pulses.
    const auto before = e.clockPulses();
    h.playing = false;
    for (int i = 0; i < 50; ++i) { e.beginBlock (h, block); e.render (buf, block); }
    check (e.clockPulses() == before, "no clock while host is stopped");

    // Restart mid-song resets the sequencers and starts on the next 16th.
    p.rhythmToSeq[0][0] = true;
    e.setParams (p);
    e.pressNext();
    h.playing = true;
    h.ppq = 10.1;
    e.beginBlock (h, block);
    check (e.currentStep (0) == 0, "host start resets to step 1");
}

void testPatching()
{
    std::puts ("patching");
    // SEQ 1 -> VCO 2 input: Sequencer 1 transposes VCO 2.
    auto p = quietParams();
    p.step[0] = { 1.0f, 0.0f, 0.0f, 0.0f }; // +2 octaves at ±2
    p.seqOct = 1;
    Engine e;
    startEngine (e, p, false);
    Patch patch = emptyPatch();
    patch[inVco2] = outSeq1;
    e.setPatch (patch);
    run (e, 64);
    near (e.currentVcoHz (1), kC4Hz * 4.0, 1.0, "SEQ 1 out into VCO 2 in transposes VCO 2");
    near (e.currentVcoHz (0), kC4Hz, 0.5, "VCO 1 unaffected (OSC 1 not assigned)");

    // VCO 1 input normalled to VCO 2.
    patch = emptyPatch();
    patch[inVco1] = outSeq1;
    e.setPatch (patch);
    run (e, 64);
    near (e.currentVcoHz (1), kC4Hz * 4.0, 1.0, "VCO 1 input is normalled to VCO 2");

    // CLOCK out -> VCA in opens the VCA on each clock pulse.
    auto q = quietParams();
    q.tempoBpm = 300.0f;
    q.egMode = EgMode::off;
    q.vcoLevel[0] = 0.7f;
    q.cutoffHz = 20000.0f;
    Engine e2;
    startEngine (e2, q);
    Patch p2 = emptyPatch();
    p2[inVca] = outClock;
    e2.setPatch (p2);
    double peak = 0.0;
    run (e2, (int) (0.5 * kRate), [&] (int) { peak = std::max (peak, std::abs (e2.outputVoltage (outVca))); });
    check (peak > 0.5, "CLOCK -> VCA gates the sound");

    // Sub CV input changes the divisor.
    auto r = quietParams();
    r.subDiv[0][0] = 8;
    Engine e3;
    startEngine (e3, r, false);
    Patch p3 = emptyPatch();
    p3[inVco1Sub] = outVcfEg; // 8 V when held open
    e3.setPatch (p3);
    r.egMode = EgMode::held;
    e3.setParams (r);
    run (e3, (int) (0.05 * kRate));
    check (e3.effectiveSubDiv (0, 0) == 1, "positive SUB CV lowers the divisor (" + std::to_string (e3.effectiveSubDiv (0, 0)) + ")");
}

void testSequencerAssign()
{
    std::puts ("sequencer assign");
    auto p = quietParams();
    p.seqOct = 1;            // ±2
    p.quant = Quant::et12;
    p.step[0] = { 0.5f, 0.0f, 0.0f, 0.0f }; // +1 octave
    p.assign[0] = { true, false, false };
    p.subDiv[0][0] = 4;
    Engine e;
    startEngine (e, p, false);
    run (e, 64);
    near (e.currentVcoHz (0), kC4Hz * 2.0, 1.0, "OSC assign: step transposes VCO");

    p.assign[0] = { false, true, false };
    e.setParams (p);
    run (e, 64);
    near (e.currentVcoHz (0), kC4Hz, 0.5, "SUB assign: VCO unchanged");
    check (e.effectiveSubDiv (0, 0) == 1, "SUB assign: step lowers the sub divisor (" + std::to_string (e.effectiveSubDiv (0, 0)) + ")");
    check (e.effectiveSubDiv (0, 1) == 1, "SUB 2 not assigned keeps its knob");

    // Quantized step lands on semitones.
    p.assign[0] = { true, false, false };
    p.step[0][0] = 7.3f / 24.0f;
    e.setParams (p);
    run (e, 64);
    near (e.currentVcoHz (0), kC4Hz * std::pow (2.0, 7.0 / 12.0), 0.5, "12-ET step snaps to a fifth");
}

void testMidi()
{
    std::puts ("midi");
    auto p = quietParams();
    Engine e;
    startEngine (e, p, false);
    e.noteOn (72);
    run (e, 2400);
    near (e.currentVcoHz (0), kC4Hz * 2.0, 0.5, "note 72 = +1 octave from C4");
    check (e.vcaEnvLevel() > 0.99, "note gates the envelopes");
    e.noteOff (72);
    run (e, (int) (1.0 * kRate));
    check (e.vcaEnvLevel() < 0.05, "note off releases");
    near (e.currentVcoHz (0), kC4Hz * 2.0, 0.5, "pitch held after release");

    e.controlChange (103, 0);
    run (e, 8);
    check (e.effectiveSubDiv (0, 0) == 16, "CC103 0 -> SUB 1 /16");
    e.controlChange (103, 127);
    run (e, 8);
    check (e.effectiveSubDiv (0, 0) == 1, "CC103 127 -> SUB 1 /1");
    e.clearSubOverride (0, 0);
    run (e, 8);
    check (e.effectiveSubDiv (0, 0) == 1, "knob takes over again");

    e.controlChange (4, 127);
    e.controlChange (36, 127);
    run (e, 4800);
    near (e.currentVcoHz (0), kC4Hz * 2.0 * std::pow (2.0, 2.5), 3.0, "CC4/36 adds +2.5 octaves");
}

void testAudio()
{
    std::puts ("audio");
    auto p = quietParams();
    p.tempoBpm = 480.0f;
    p.rhythmToSeq[0][0] = true;
    p.rhythmToSeq[1][1] = true;
    p.rhythmDiv[1] = 3;
    p.assign[0][0] = p.assign[1][0] = true;
    p.step[0] = { 0.0f, 0.3f, -0.2f, 0.6f };
    p.step[1] = { 0.1f, -0.4f, 0.25f, 0.0f };
    p.vcoLevel = { 1.0f, 1.0f };
    p.subLevel[0] = { 1.0f, 1.0f };
    p.subLevel[1] = { 1.0f, 1.0f };
    p.subDiv[0] = { 2, 3 };
    p.subDiv[1] = { 5, 7 };
    p.resonance = 0.9f;
    p.egAmt = 0.6f;
    p.cutoffHz = 400.0f;
    p.volume = 1.0f;
    Engine e;
    startEngine (e, p);
    std::vector<float> buf ((size_t) kRate * 2);
    e.render (buf.data(), (int) buf.size());
    double peak = 0.0, sumSq = 0.0;
    bool finite = true;
    for (float s : buf)
    {
        if (! std::isfinite (s)) finite = false;
        peak = std::max (peak, (double) std::abs (s));
        sumSq += (double) s * s;
    }
    const double rms = std::sqrt (sumSq / (double) buf.size());
    check (finite, "output is finite");
    check (peak < 1.2, "output stays bounded at full drive (peak " + std::to_string (peak) + ")");
    check (rms > 0.02, "full patch produces sound (rms " + std::to_string (rms) + ")");

    // Self-oscillation: no oscillators, resonance max, EG held open.
    auto q = quietParams();
    q.resonance = 1.0f;
    q.cutoffHz = 800.0f;
    q.egMode = EgMode::held;
    q.volume = 1.0f;
    Engine e2;
    startEngine (e2, q, false);
    std::vector<float> b2 ((size_t) kRate);
    e2.render (b2.data(), (int) b2.size());
    double rms2 = 0.0;
    for (size_t i = b2.size() / 2; i < b2.size(); ++i) rms2 += (double) b2[i] * b2[i];
    rms2 = std::sqrt (rms2 / (b2.size() / 2.0));
    check (rms2 > 0.01, "filter self-oscillates at maximum resonance (rms " + std::to_string (rms2) + ")");
}
} // namespace

int main()
{
    testQuantize();
    testSubharmonics();
    testWaveSwitch();
    testRhythm();
    testTransport();
    testEnvelopes();
    testHostSync();
    testPatching();
    testSequencerAssign();
    testMidi();
    testAudio();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
