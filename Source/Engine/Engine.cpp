#include "Engine.h"

namespace fractone
{
using dsp::clamp;

namespace
{
    constexpr double kOversample   = 2.0;
    constexpr double kSubStepsPerV = 1.5;   // SUB / RHYTHM CV inputs: divisor steps per volt
    constexpr double kSeqSubPerV   = 3.0;   // sequencer -> sub divisor: steps per volt of step CV
    constexpr double kEgCutoffOct  = 7.0;   // VCF EG AMT at full: octaves of cutoff sweep
    constexpr double kMaxVcoHz     = 14000.0;
    constexpr double kTriggerSec   = 0.001;
    constexpr double kMaxGateSec   = 0.020;

    inline double levelTaper (double x) { x = clamp (x, 0.0, 1.0); return x * std::sqrt (x); }
}

Engine::Engine() = default;

void Engine::prepare (double hostSampleRate)
{
    hostRate = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;
    fs = hostRate * kOversample;

    ladder.prepare (fs);
    vcfEnv.prepare (fs);
    vcaEnv.prepare (fs);
    dcBlock.prepare (hostRate);
    decimator.reset();

    for (auto& s : knobPitchSmooth) s.prepare (fs, 0.002);
    for (auto& s : ccPitchSmooth)   s.prepare (fs, 0.004);
    for (auto& s : vcoLevelSmooth)  s.prepare (fs, 0.008);
    for (auto& row : subLevelSmooth)
        for (auto& s : row) s.prepare (fs, 0.008);
    cutoffSmooth.prepare (fs, 0.004);
    resSmooth.prepare (fs, 0.008);
    egAmtSmooth.prepare (fs, 0.008);
    volSmooth.prepare (fs, 0.010);

    reset();
}

void Engine::reset()
{
    for (auto& b : bank) b.reset();
    ladder.reset();
    vcfEnv.reset();
    vcaEnv.reset();
    decimator.reset();
    outV.fill (0.0);

    for (int v = 0; v < 2; ++v)
    {
        knobPitchSmooth[(size_t) v].snap (quantizeOctaves (params.vcoFreq[(size_t) v] * 4.0, params.quant));
        ccPitchSmooth[(size_t) v].snap (ccPitchV[(size_t) v]);
        vcoLevelSmooth[(size_t) v].snap (levelTaper (params.vcoLevel[(size_t) v]));
        for (int j = 0; j < 2; ++j)
            subLevelSmooth[(size_t) v][(size_t) j].snap (levelTaper (params.subLevel[(size_t) v][(size_t) j]));
    }
    cutoffSmooth.snap (std::log2 (std::max (20.0, (double) params.cutoffHz) / 20.0));
    resSmooth.snap (params.resonance);
    egAmtSmooth.snap (params.egAmt);
    volSmooth.snap (params.volume * params.volume);

    doReset();
    internalPhase = 1.0;
    hostWasPlaying = false;
    havePpq = false;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------
void Engine::beginBlock (const HostInfo& h, int numSamples) noexcept
{
    host = h;
    blockSamples = numSamples;
    const bool hostPlaying = h.valid && h.playing;
    const double len = std::max (1.0e-3, params.syncPulseBeats);

    if (params.sync)
    {
        hostPpqPerTick = std::max (1.0, h.bpm) / 60.0 / fs;
        if (hostPlaying)
        {
            const bool started = ! hostWasPlaying;
            const bool jumped  = havePpq && std::abs (h.ppq - expectedPpq) > 0.05;
            if (started || jumped)
            {
                doReset();
                lastGrid = (int64_t) std::ceil (h.ppq / len - 1.0e-7) - 1;
            }
            else if (std::abs (lastGridLen - len) > 1.0e-9)
            {
                lastGrid = (int64_t) std::floor (h.ppq / len + 1.0e-9);
            }
            hostPpq = h.ppq;
            expectedPpq = h.ppq + (double) numSamples * std::max (1.0, h.bpm) / 60.0 / hostRate;
            havePpq = true;
        }
        else
        {
            havePpq = false;
        }
    }
    lastGridLen = len;

    hostPlayingNow = hostPlaying;
    hostWasPlaying = hostPlaying;
    running = (params.sync ? hostPlaying : true) && playOn;
}

void Engine::setPlayOn (bool on) noexcept
{
    if (on && ! playOn && ! params.sync)
        internalPhase = 1.0; // first pulse right away
    playOn = on;
    running = (params.sync ? hostPlayingNow : true) && playOn;
}

void Engine::doReset() noexcept
{
    k = 0;
    stepIndex = { 0, 0 };
    armed = { true, true };
}

void Engine::pressReset() noexcept
{
    doReset();
}

void Engine::pressNext() noexcept
{
    for (int s = 0; s < 2; ++s)
    {
        bool hasSource = false;
        for (int r = 0; r < 4; ++r)
            hasSource = hasSource || params.rhythmToSeq[(size_t) r][(size_t) s];
        if (hasSource)
            advanceSeq (s, true);
    }
}

Logic Engine::currentLogic() const noexcept
{
    if (logicOverride >= 0)
        return logicOverride > 0 ? Logic::xorLogic : Logic::orLogic;
    return params.logic;
}

void Engine::masterPulse() noexcept
{
    ++masterCount;
    if (ticksSincePulse > 8.0)
        pulsePeriod = ticksSincePulse;
    ticksSincePulse = 0.0;

    const int gateTicks = (int) std::min (pulsePeriod * 0.5, kMaxGateSec * fs);
    clockOutTicks = std::max (1, gateTicks);

    std::array<bool, 4> fire {};
    for (int r = 0; r < 4; ++r)
    {
        double div = params.rhythmDiv[(size_t) r];
        if (isPatched (inRhythm1 + r))
            div -= inV (inRhythm1 + r) * kSubStepsPerV;
        const int n = (int) clamp (std::round (div), 1.0, 16.0);
        lastRhythmDiv[(size_t) r] = n;
        fire[(size_t) r] = (k % (uint32_t) n) == 0;
        if (fire[(size_t) r])
            ++fireCount[(size_t) r];
    }
    k = (k + 1) % dsp::DividerBank::kCycleWrap;

    const Logic logic = currentLogic();
    bool anyAdvanced = false;
    for (int s = 0; s < 2; ++s)
    {
        int count = 0;
        for (int r = 0; r < 4; ++r)
            if (fire[(size_t) r] && params.rhythmToSeq[(size_t) r][(size_t) s])
                ++count;

        const bool adv = logic == Logic::orLogic ? count > 0 : (count % 2) == 1;
        if (adv)
        {
            advanceSeq (s, false);
            anyAdvanced = true;
        }
    }

    if (anyAdvanced && params.egMode == EgMode::on)
        triggerEgs();
}

void Engine::advanceSeq (int s, bool fromNext) noexcept
{
    const int gateTicks = (int) std::min (pulsePeriod * 0.5, kMaxGateSec * fs);
    seqClkTicks[(size_t) s] = std::max (1, gateTicks);

    if (! fromNext && (resetHeld || armed[(size_t) s]))
    {
        armed[(size_t) s] = false; // play step 1 on the first clock after a reset
        return;
    }
    armed[(size_t) s] = false;
    stepIndex[(size_t) s] = (stepIndex[(size_t) s] + 1) % 4;
}

void Engine::triggerEgs() noexcept
{
    if (params.egMode == EgMode::held)
        return;
    const bool a = vcfEnv.trigger();
    const bool b = vcaEnv.trigger();
    if (gateActive)
    {
        vcfEnv.gateOn();
        vcaEnv.gateOn();
    }
    if (a || b)
        triggerOutTicks = std::max (1, (int) (kTriggerSec * fs));
}

void Engine::setTriggerHeld (bool down) noexcept
{
    if (down && ! triggerButton && params.egMode == EgMode::on)
        triggerEgs();
    triggerButton = down;
}

void Engine::updateGate() noexcept
{
    const bool desired = numHeld > 0
                      || (triggerButton && params.egMode == EgMode::off)
                      || params.egMode == EgMode::held;

    if (desired && ! gateActive)
    {
        const bool a = vcfEnv.gateOn();
        const bool b = vcaEnv.gateOn();
        if (a || b)
            triggerOutTicks = std::max (1, (int) (kTriggerSec * fs));
    }
    else if (! desired && gateActive)
    {
        vcfEnv.gateOff();
        vcaEnv.gateOff();
    }
    gateActive = desired;
}

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------
void Engine::noteOn (int note) noexcept
{
    // Remove if already held, then push as most recent.
    int w = 0;
    for (int i = 0; i < numHeld; ++i)
        if (heldNotes[(size_t) i] != note)
            heldNotes[(size_t) w++] = heldNotes[(size_t) i];
    numHeld = w;
    if (numHeld == (int) heldNotes.size())
    {
        for (int i = 1; i < numHeld; ++i)
            heldNotes[(size_t) (i - 1)] = heldNotes[(size_t) i];
        --numHeld;
    }
    const bool wasHeld = numHeld > 0;
    heldNotes[(size_t) numHeld++] = note;
    midiPitchV = (note - 60) / 12.0;

    if (wasHeld && params.egMode != EgMode::held)
    {
        // A new note while another is held retriggers, keeping the gate open.
        const bool a = vcfEnv.trigger();
        const bool b = vcaEnv.trigger();
        vcfEnv.gateOn();
        vcaEnv.gateOn();
        if (a || b)
            triggerOutTicks = std::max (1, (int) (kTriggerSec * fs));
    }
    updateGate();
}

void Engine::noteOff (int note) noexcept
{
    int w = 0;
    for (int i = 0; i < numHeld; ++i)
        if (heldNotes[(size_t) i] != note)
            heldNotes[(size_t) w++] = heldNotes[(size_t) i];
    numHeld = w;
    if (numHeld > 0)
        midiPitchV = (heldNotes[(size_t) (numHeld - 1)] - 60) / 12.0;
    // The last pitch is kept after release, like a held CV.
    updateGate();
}

void Engine::allNotesOff() noexcept
{
    numHeld = 0;
    updateGate();
}

void Engine::controlChange (int cc, int value) noexcept
{
    value = std::clamp (value, 0, 127);

    auto pitchFor = [this] (int v)
    {
        const double code = ccPitchMsb[(size_t) v] * 128.0 + ccPitchLsb[(size_t) v];
        ccPitchV[(size_t) v] = (code / 16383.0 * 2.0 - 1.0) * 2.5;
    };
    auto subFromCc = [] (int val) { return 16 - val / 8; };
    auto egFor = [this] (int i, int lsb)
    {
        const double n = (egMsb[(size_t) i] * 128.0 + lsb) / 16383.0;
        egOverride[(size_t) i] = (i == 0 || i == 2) ? attackSecondsFromNorm (n) : decaySecondsFromNorm (n);
    };

    switch (cc)
    {
        case 4:   ccPitchMsb[0] = value; ccPitchLsb[0] = 0; pitchFor (0); break;
        case 36:  ccPitchLsb[0] = value; if (ccPitchMsb[0] >= 0) pitchFor (0); break;
        case 12:  ccPitchMsb[1] = value; ccPitchLsb[1] = 0; pitchFor (1); break;
        case 44:  ccPitchLsb[1] = value; if (ccPitchMsb[1] >= 0) pitchFor (1); break;
        case 103: subOverride[0][0] = subFromCc (value); break;
        case 104: subOverride[0][1] = subFromCc (value); break;
        case 105: subOverride[1][0] = subFromCc (value); break;
        case 106: subOverride[1][1] = subFromCc (value); break;
        case 23:  egMsb[0] = value; egFor (0, 0); break;
        case 55:  egFor (0, value); break;
        case 24:  egMsb[1] = value; egFor (1, 0); break;
        case 56:  egFor (1, value); break;
        case 28:  egMsb[2] = value; egFor (2, 0); break;
        case 60:  egFor (2, value); break;
        case 29:  egMsb[3] = value; egFor (3, 0); break;
        case 61:  egFor (3, value); break;
        case 113: logicOverride = value >= 64 ? 1 : 0; break;
        case 120:
        case 123: allNotesOff(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
double Engine::inV (int input) const noexcept
{
    const int src = patch[(size_t) input];
    return src >= 0 && src < kNumOutputs ? outV[(size_t) src] : 0.0;
}

double Engine::stepVolts (int seq, int stepIdx) const noexcept
{
    const double v = params.step[(size_t) seq][(size_t) stepIdx] * seqOctRange (params.seqOct);
    return quantizeOctaves (v, params.quant);
}

void Engine::render (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double a = tick();
        const double b = tick();
        const double y = dcBlock.process (decimator.process (a, b));
        // Output stage: makeup gain into a gentle saturator.
        out[i] = (float) (std::tanh (y * 3.0) * 0.85);
    }
}

double Engine::tick() noexcept
{
    // ---- Patched gate / clock inputs ----------------------------------------
    if (isPatched (inPlay))
    {
        const int e = playIn.process (inV (inPlay));
        if (e > 0) setPlayOn (true);
        else if (e < 0) setPlayOn (false);
    }
    else playIn.reset();

    bool resetNow = resetButton;
    if (isPatched (inReset))
    {
        resetIn.process (inV (inReset));
        resetNow = resetNow || resetIn.isHigh();
    }
    else resetIn.reset();
    if (resetNow && ! resetHeld)
        doReset();
    resetHeld = resetNow;

    if (isPatched (inTrigger))
    {
        if (triggerIn.process (inV (inTrigger)) > 0)
            triggerEgs();
    }
    else triggerIn.reset();

    // ---- Clock --------------------------------------------------------------
    ticksSincePulse += 1.0;
    const bool clockPatched = isPatched (inClock);
    if (clockPatched)
    {
        if (clockIn.process (inV (inClock)) > 0 && running)
            masterPulse();
    }
    else clockIn.reset();

    if (params.sync)
    {
        if (hostPlayingNow)
        {
            const double len = std::max (1.0e-3, params.syncPulseBeats);
            const auto g = (int64_t) std::floor (hostPpq / len + 1.0e-9);
            if (g > lastGrid)
            {
                lastGrid = g;
                if (running && ! clockPatched)
                    masterPulse();
            }
            hostPpq += hostPpqPerTick;
        }
    }
    else
    {
        if (running)
        {
            internalPhase += clamp (params.tempoBpm, 20.0f, 3000.0f) / 60.0 / fs;
            if (internalPhase >= 1.0)
            {
                internalPhase -= std::floor (internalPhase);
                if (! clockPatched)
                    masterPulse();
            }
        }
    }

    // ---- Envelopes ------------------------------------------------------------
    updateGate();
    vcfEnv.setTimes (egOverride[0] >= 0.0 ? egOverride[0] : params.vcfAttack,
                     egOverride[1] >= 0.0 ? egOverride[1] : params.vcfDecay);
    vcaEnv.setTimes (egOverride[2] >= 0.0 ? egOverride[2] : params.vcaAttack,
                     egOverride[3] >= 0.0 ? egOverride[3] : params.vcaDecay);
    const double eVcf = vcfEnv.tick();
    const double eVca = vcaEnv.tick();

    // ---- Analog drift -----------------------------------------------------------
    if (--driftCounter <= 0)
    {
        driftCounter = (int) (0.7 * fs);
        driftTarget[0] = rng.bipolar();
        driftTarget[1] = rng.bipolar();
    }
    const double driftCoef = 1.0 / (1.5 * fs);
    for (int v = 0; v < 2; ++v)
        driftValue[(size_t) v] += (driftTarget[(size_t) v] - driftValue[(size_t) v]) * driftCoef;

    // ---- Oscillators --------------------------------------------------------------
    std::array<double, 2> vco {};
    std::array<std::array<double, 2>, 2> sub {};

    for (int v = 0; v < 2; ++v)
    {
        const auto vi = (size_t) v;
        const double knobV = knobPitchSmooth[vi].next (quantizeOctaves (params.vcoFreq[vi] * 4.0, params.quant));
        const double ccV   = ccPitchSmooth[vi].next (ccPitchV[vi]);
        const double stepV = stepVolts (v, stepIndex[vi]);

        double cvIn = 0.0;
        if (v == 0)
            cvIn = inV (inVco1);
        else
            cvIn = isPatched (inVco2) ? inV (inVco2) : inV (inVco1); // VCO 1 input is normalled to VCO 2

        const double pitch = knobV
                           + (params.assign[vi][0] ? stepV : 0.0)
                           + cvIn + midiPitchV + ccV
                           + params.fineCents / 1200.0
                           + driftValue[vi] * params.drift * (6.0 / 1200.0);

        const double hz = clamp (kC4Hz * std::exp2 (pitch), 2.0, std::min (kMaxVcoHz, 0.45 * fs));
        lastVcoHz[vi] = hz;
        const double inc = hz / fs;
        bank[vi].advance (inc);

        const int subIn = v == 0 ? inVco1Sub : inVco2Sub;
        std::array<int, 2> n {};
        for (int j = 0; j < 2; ++j)
        {
            const auto ji = (size_t) j;
            double d = subOverride[vi][ji] > 0 ? subOverride[vi][ji] : params.subDiv[vi][ji];
            if (params.assign[vi][1 + ji])
                d -= stepV * kSeqSubPerV;
            if (isPatched (subIn))
                d -= inV (subIn) * kSubStepsPerV;
            n[ji] = (int) clamp (std::round (d), 1.0, 16.0);
            lastSubDiv[vi][ji] = n[ji];
        }

        const double t0 = bank[vi].masterPhase();
        const double t1 = bank[vi].subPhase (n[0]);
        const double t2 = bank[vi].subPhase (n[1]);
        const double dt1 = inc / n[0], dt2 = inc / n[1];

        const int pwmIn = v == 0 ? inVco1Pwm : inVco2Pwm;
        const bool pwmPatched = isPatched (pwmIn);
        const double pwmDuty = 0.5 + inV (pwmIn) * 0.098;

        switch (params.wave[vi])
        {
            case Wave::square:
            {
                const double duty = pwmPatched ? pwmDuty : 0.5;
                vco[vi]    = dsp::pulseWave (t0, inc, duty);
                sub[vi][0] = dsp::pulseWave (t1, dt1, duty);
                sub[vi][1] = dsp::pulseWave (t2, dt2, duty);
                break;
            }
            case Wave::mixed:
            {
                sub[vi][0] = dsp::sawWave (t1, dt1);
                sub[vi][1] = dsp::sawWave (t2, dt2);
                // SUB 1's sawtooth is normalled to VCO pulse width.
                const double duty = pwmPatched ? pwmDuty : 0.5 + 0.45 * sub[vi][0];
                vco[vi] = dsp::pulseWave (t0, inc, duty);
                break;
            }
            case Wave::saw:
                vco[vi]    = dsp::sawWave (t0, inc);
                sub[vi][0] = dsp::sawWave (t1, dt1);
                sub[vi][1] = dsp::sawWave (t2, dt2);
                break;
        }
    }

    // ---- Mixer --------------------------------------------------------------------
    double sum = 0.0;
    for (int v = 0; v < 2; ++v)
    {
        const auto vi = (size_t) v;
        sum += vcoLevelSmooth[vi].next (levelTaper (params.vcoLevel[vi])) * vco[vi];
        for (int j = 0; j < 2; ++j)
            sum += subLevelSmooth[vi][(size_t) j].next (levelTaper (params.subLevel[vi][(size_t) j])) * sub[vi][(size_t) j];
    }
    constexpr double bias = 0.12;
    const double mix = std::tanh (sum * 0.55 + bias) - std::tanh (bias); // warm when pushed

    // ---- Filter ---------------------------------------------------------------------
    const double baseOct = cutoffSmooth.next (std::log2 (clamp (params.cutoffHz, 20.0f, 20000.0f) / 20.0));
    double oct = baseOct + egAmtSmooth.next (params.egAmt) * eVcf * kEgCutoffOct;
    if (isPatched (inCutoff))
        oct += inV (inCutoff);
    const double fc = 20.0 * std::exp2 (clamp (oct, -3.0, 11.0));
    const double kRes = resSmooth.next (params.resonance) * 4.6;
    const double noise = rng.bipolar() * 2.0e-4; // analog noise floor, also seeds self-oscillation
    double y = ladder.process (mix + noise, fc, kRes);

    // ---- VCA --------------------------------------------------------------------------
    double gain = eVca;
    if (isPatched (inVca))
        gain += inV (inVca) / 8.0;
    gain = clamp (gain, 0.0, 1.0);
    const double vol = volSmooth.next (params.volume * params.volume);
    y *= gain * vol * 0.9;

    // ---- Patch outputs (volts) ------------------------------------------------------
    outV[outVca]      = y * 5.0;
    outV[outVco1]     = vco[0] * 5.0;
    outV[outVco1Sub1] = sub[0][0] * 5.0;
    outV[outVco1Sub2] = sub[0][1] * 5.0;
    outV[outVco2]     = vco[1] * 5.0;
    outV[outVco2Sub1] = sub[1][0] * 5.0;
    outV[outVco2Sub2] = sub[1][1] * 5.0;
    outV[outVcaEg]    = eVca * 8.0;
    outV[outVcfEg]    = eVcf * 8.0;

    for (int s = 0; s < 2; ++s)
    {
        const auto si = (size_t) s;
        const int range = params.seqOutFixedRange ? 5 : seqOctRange (params.seqOct);
        double cv = params.step[si][(size_t) stepIndex[si]] * range;
        if (! params.seqOutUnquantized)
            cv = quantizeOctaves (cv, params.quant);
        outV[s == 0 ? outSeq1 : outSeq2] = cv;
        outV[s == 0 ? outSeq1Clk : outSeq2Clk] = seqClkTicks[si] > 0 ? 5.0 : 0.0;
        if (seqClkTicks[si] > 0) --seqClkTicks[si];
    }

    outV[outClock] = (clockOutTicks > 0 && running) ? 10.0 : 0.0;
    if (clockOutTicks > 0) --clockOutTicks;
    outV[outTrigger] = triggerOutTicks > 0 ? 5.0 : 0.0;
    if (triggerOutTicks > 0) --triggerOutTicks;

    return y;
}
} // namespace fractone
