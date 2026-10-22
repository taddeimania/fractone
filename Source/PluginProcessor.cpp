#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/Dice.h"

using namespace fractone;

namespace
{
const juce::Identifier kPatchTag   { "Patch" };
const juce::Identifier kCableTag   { "Cable" };
const juce::Identifier kOutAttr    { "out" };
const juce::Identifier kInAttr     { "in" };
const juce::Identifier kPresetAttr { "presetName" };
const juce::Identifier kPlayAttr   { "playOn" };
const juce::Identifier kVersion    { "stateVersion" };

// Settings that belong to this instance in this Live set, not to a sound.
const juce::StringArray& instanceParams()
{
    static const juce::StringArray ids { fractone::ids::sync, fractone::ids::midiChannel, fractone::ids::fineTune };
    return ids;
}
} // namespace

FractoneProcessor::FractoneProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FractoneState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            params[withId->paramID] = apvts.getRawParameterValue (withId->paramID);

    for (auto& s : patchSrc)
        s.store (-1);

    // Any panel change marks the loaded preset as edited (shown as "*").
    for (auto& kv : params)
        if (! instanceParams().contains (kv.first))
            apvts.addParameterListener (kv.first, this);
}

FractoneProcessor::~FractoneProcessor()
{
    cancelPendingUpdate();
    for (auto& kv : params)
        apvts.removeParameterListener (kv.first, this);
}

void FractoneProcessor::parameterChanged (const juce::String& id, float newValue)
{
    for (int s = 0; s < 2; ++s)
        if (id == ids::diceSeq (s))
        {
            // Roll when the trigger switches on. This can arrive on the audio
            // thread (automation), so the roll itself happens on the message thread.
            const bool on = newValue > 0.5f;
            if (on && ! diceLast[s].exchange (on))
            {
                dicePending[s].store (true);
                triggerAsyncUpdate();
            }
            diceLast[s].store (on);
            return; // the rolled steps mark the preset as edited themselves
        }

    presets.markEdited();
}

void FractoneProcessor::handleAsyncUpdate()
{
    for (int s = 0; s < 2; ++s)
        if (dicePending[s].exchange (false))
            rollDice (s);
}

void FractoneProcessor::triggerDice (int seq)
{
    if (auto* param = apvts.getParameter (ids::diceSeq (seq)))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (1.0f);
        param->setValueNotifyingHost (0.0f);
        param->endChangeGesture();
    }
}

float FractoneProcessor::get (const juce::String& id) const
{
    auto it = params.find (id);
    jassert (it != params.end());
    return it != params.end() ? it->second->load() : 0.0f;
}

bool FractoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FractoneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate);
    engine.setPlayOn (playOnRestore);
    scratch.assign ((size_t) juce::jmax (512, samplesPerBlock), 0.0f);
}

EngineParams FractoneProcessor::readParams() const
{
    EngineParams p;
    for (int s = 0; s < 2; ++s)
    {
        for (int i = 0; i < 4; ++i)
            p.step[(size_t) s][(size_t) i] = get (ids::step (s, i));
        for (int t = 0; t < 3; ++t)
            p.assign[(size_t) s][(size_t) t] = get (ids::assign (s, t)) > 0.5f;
    }
    for (int r = 0; r < 4; ++r)
    {
        p.rhythmDiv[(size_t) r] = juce::roundToInt (get (ids::rhythm (r)));
        for (int s = 0; s < 2; ++s)
            p.rhythmToSeq[(size_t) r][(size_t) s] = get (ids::rhythmToSeq (r, s)) > 0.5f;
    }
    p.logic = juce::roundToInt (get (ids::logic)) == 1 ? Logic::xorLogic : Logic::orLogic;

    p.tempoBpm = get (ids::tempo);
    p.sync = get (ids::sync) > 0.5f;
    p.syncPulseBeats = ids::syncRateBeats (juce::roundToInt (get (ids::syncRate)));

    for (int v = 0; v < 2; ++v)
    {
        p.vcoFreq[(size_t) v] = get (ids::vcoFreq (v));
        p.wave[(size_t) v] = (Wave) juce::jlimit (0, 2, juce::roundToInt (get (ids::vcoWave (v))));
        p.vcoLevel[(size_t) v] = get (ids::vcoLevel (v));
        for (int j = 0; j < 2; ++j)
        {
            p.subDiv[(size_t) v][(size_t) j] = juce::roundToInt (get (ids::subDiv (v, j)));
            p.subLevel[(size_t) v][(size_t) j] = get (ids::subLevel (v, j));
        }
    }
    p.seqOct = juce::roundToInt (get (ids::seqOct));
    p.quant  = (Quant) juce::jlimit (0, 4, juce::roundToInt (get (ids::quantize)));

    p.cutoffHz  = get (ids::cutoff);
    p.resonance = get (ids::resonance);
    p.egAmt     = get (ids::egAmt);
    p.volume    = get (ids::volume);
    p.vcfAttack = get (ids::vcfAttack);
    p.vcfDecay  = get (ids::vcfDecay);
    p.vcaAttack = get (ids::vcaAttack);
    p.vcaDecay  = get (ids::vcaDecay);
    p.egMode    = (EgMode) juce::jlimit (0, 2, juce::roundToInt (get (ids::egMode)));

    p.seqOutUnquantized = get (ids::seqOutUnq) > 0.5f;
    p.seqOutFixedRange  = get (ids::seqOutFixed) > 0.5f;
    p.fineCents = get (ids::fineTune);
    p.drift     = get (ids::drift);
    return p;
}

void FractoneProcessor::handleMidi (const juce::MidiMessage& m)
{
    const int ch = juce::roundToInt (get (ids::midiChannel));
    if (ch != 0 && m.getChannel() != ch)
        return;

    if (m.isNoteOn())
    {
        engine.noteOn (m.getNoteNumber());
        uiMidi.fetch_add (1);
    }
    else if (m.isNoteOff())
    {
        engine.noteOff (m.getNoteNumber());
    }
    else if (m.isController())
    {
        engine.controlChange (m.getControllerNumber(), m.getControllerValue());
        uiMidi.fetch_add (1);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        engine.allNotesOff();
    }
}

void FractoneProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = audio.getNumSamples();
    audio.clear();
    if (numSamples == 0)
        return;
    if ((int) scratch.size() < numSamples)
        scratch.resize ((size_t) numSamples); // only if the host exceeds the announced block size

    // --- Parameters & patch ------------------------------------------------------
    const EngineParams p = readParams();
    engine.setParams (p);
    engine.setPatch (currentPatch());

    // A knob moved since the last block takes over from any MIDI CC value.
    for (int v = 0; v < 2; ++v)
        for (int j = 0; j < 2; ++j)
        {
            const int k = p.subDiv[(size_t) v][(size_t) j];
            if (k != lastSubKnob[(size_t) v][(size_t) j])
                engine.clearSubOverride (v, j);
            lastSubKnob[(size_t) v][(size_t) j] = k;
        }
    const float egKnobs[] { p.vcfAttack, p.vcfDecay, p.vcaAttack, p.vcaDecay };
    for (int i = 0; i < 4; ++i)
    {
        if (std::abs (egKnobs[i] - lastEgKnob[(size_t) i]) > 1.0e-7f)
            engine.clearEgOverride (i);
        lastEgKnob[(size_t) i] = egKnobs[i];
    }
    if ((int) p.logic != lastLogicKnob)
        engine.clearLogicOverride();
    lastLogicKnob = (int) p.logic;

    // --- Host transport ---------------------------------------------------------
    HostInfo host;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            host.valid   = true;
            host.playing = pos->getIsPlaying();
            host.bpm     = pos->getBpm().orFallback (120.0);
            host.ppq     = pos->getPpqPosition().orFallback (0.0);
        }
    }
    engine.beginBlock (host, numSamples);

    // --- Panel actions -------------------------------------------------------------
    if (pendingPlayState.exchange (false))
        engine.setPlayOn (pendingPlayValue.load());
    if (const int t = playToggles.load(); t != seenPlayToggles)
    {
        if (((t - seenPlayToggles) & 1) != 0)
            engine.togglePlay();
        seenPlayToggles = t;
    }
    if (const int r = resetPresses.load(); r != seenResetPresses)
    {
        engine.pressReset();
        seenResetPresses = r;
    }
    engine.setResetHeld (resetHeld.load());
    if (const int n = nextPresses.load(); n != seenNextPresses)
    {
        for (int i = seenNextPresses; i != n; ++i)
            engine.pressNext();
        seenNextPresses = n;
    }
    engine.setTriggerHeld (triggerHeld.load());

    // --- Render, split at MIDI events ------------------------------------------------
    float* out = scratch.data();
    int cursor = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > cursor)
        {
            engine.render (out + cursor, at - cursor);
            cursor = at;
        }
        handleMidi (meta.getMessage());
    }
    if (cursor < numSamples)
        engine.render (out + cursor, numSamples - cursor);

    for (int c = 0; c < audio.getNumChannels(); ++c)
        audio.copyFrom (c, 0, out, numSamples);

    // --- Readouts ------------------------------------------------------------------------
    uiStep[0].store (engine.currentStep (0));
    uiStep[1].store (engine.currentStep (1));
    uiPlayOn.store (engine.isPlayOn());
    uiRunning.store (engine.isRunning());
    uiHostPlaying.store (host.valid && host.playing);
    uiHostBpm.store (host.bpm);
    for (int r = 0; r < 4; ++r)
    {
        uiRhythmFires[r].store (engine.rhythmFires (r));
        uiRhythmDiv[r].store (engine.effectiveRhythmDiv (r));
    }
    uiClock.store (engine.clockPulses());
    uiVcaEnv.store ((float) engine.vcaEnvLevel());
    uiVcfEnv.store ((float) engine.vcfEnvLevel());
    for (int v = 0; v < 2; ++v)
    {
        uiVcoHz[v].store ((float) engine.currentVcoHz (v));
        for (int j = 0; j < 2; ++j)
            uiSubDiv[v][j].store (engine.effectiveSubDiv (v, j));
    }
}

// ---------------------------------------------------------------------------
// Patchbay
// ---------------------------------------------------------------------------
void FractoneProcessor::connect (int output, int input)
{
    if (input < 0 || input >= kNumInputs || output < 0 || output >= kNumOutputs)
        return;
    patchSrc[(size_t) input].store (output);
    patchVersion.fetch_add (1);
}

void FractoneProcessor::disconnect (int input)
{
    if (input < 0 || input >= kNumInputs)
        return;
    patchSrc[(size_t) input].store (-1);
    patchVersion.fetch_add (1);
}

void FractoneProcessor::rollDice (int seq)
{
    if (seq < 0 || seq > 1)
        return;
    const float anchor = get (ids::step (seq, 0));
    const int   range  = seqOctRange (juce::roundToInt (get (ids::seqOct)));
    const auto  quant  = (Quant) juce::jlimit (0, 4, juce::roundToInt (get (ids::quantize)));
    const auto  steps  = dice::rollSteps (anchor, range, quant, (uint64_t) juce::Random::getSystemRandom().nextInt64());

    for (int i = 0; i < 3; ++i)
        if (auto* param = apvts.getParameter (ids::step (seq, i + 1)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (steps[(size_t) i]));
            param->endChangeGesture();
        }
}

void FractoneProcessor::clearPatch()
{
    for (auto& s : patchSrc)
        s.store (-1);
    patchVersion.fetch_add (1);
}

Patch FractoneProcessor::currentPatch() const noexcept
{
    Patch p;
    for (int i = 0; i < kNumInputs; ++i)
        p[(size_t) i] = patchSrc[(size_t) i].load();
    return p;
}

void FractoneProcessor::setPatch (const Patch& p)
{
    for (int i = 0; i < kNumInputs; ++i)
        patchSrc[(size_t) i].store (p[(size_t) i]);
    patchVersion.fetch_add (1);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
juce::ValueTree FractoneProcessor::captureState()
{
    auto state = apvts.copyState();
    state.setProperty (kVersion, 1, nullptr);
    state.setProperty (kPresetAttr, presets.currentName(), nullptr);
    state.setProperty (kPlayAttr, uiPlayOn.load(), nullptr);

    if (auto old = state.getChildWithName (kPatchTag); old.isValid())
        state.removeChild (old, nullptr);

    juce::ValueTree patch (kPatchTag);
    for (int i = 0; i < kNumInputs; ++i)
    {
        const int src = patchSrc[(size_t) i].load();
        if (src >= 0)
        {
            juce::ValueTree cable (kCableTag);
            cable.setProperty (kOutAttr, src, nullptr);
            cable.setProperty (kInAttr, i, nullptr);
            patch.appendChild (cable, nullptr);
        }
    }
    state.appendChild (patch, nullptr);
    return state;
}

void FractoneProcessor::restoreState (const juce::ValueTree& stateIn, bool asPreset)
{
    if (! stateIn.hasType (apvts.state.getType()))
        return;

    auto state = stateIn.createCopy();

    // Patch cables
    Patch patch = emptyPatch();
    if (auto pt = state.getChildWithName (kPatchTag); pt.isValid())
    {
        for (auto cable : pt)
        {
            const int o = cable.getProperty (kOutAttr, -1);
            const int i = cable.getProperty (kInAttr, -1);
            if (i >= 0 && i < kNumInputs && o >= 0 && o < kNumOutputs)
                patch[(size_t) i] = o;
        }
        state.removeChild (pt, nullptr);
    }

    if (asPreset)
    {
        // Keep this instance's own settings.
        for (auto& id : instanceParams())
            for (auto child : state)
                if (child.getProperty ("id").toString() == id)
                    if (auto* param = apvts.getParameter (id))
                        child.setProperty ("value", param->convertFrom0to1 (param->getValue()), nullptr);
    }
    else if (state.hasProperty (kPlayAttr))
    {
        playOnRestore = (bool) state[kPlayAttr];
        pendingPlayValue.store (playOnRestore);
        pendingPlayState.store (true);
    }

    // Restoring a session or preset never rolls: triggers always come back off.
    for (int s = 0; s < 2; ++s)
        for (auto child : state)
            if (child.getProperty ("id").toString() == ids::diceSeq (s))
                child.setProperty ("value", 0.0f, nullptr);
    for (auto& d : diceLast)
        d.store (true); // ignore any edge caused by the restore itself

    apvts.replaceState (state);
    setPatch (patch);

    for (int s = 0; s < 2; ++s)
    {
        dicePending[s].store (false);
        diceLast[s].store (get (ids::diceSeq (s)) > 0.5f);
    }
}

void FractoneProcessor::applyFactoryPreset (const Preset& preset)
{
    // Back to defaults, keeping per-instance settings.
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! instanceParams().contains (withId->paramID))
                withId->setValueNotifyingHost (withId->getDefaultValue());

    for (const auto& [id, value] : preset.values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));

    Patch patch = emptyPatch();
    for (const auto& [o, i] : preset.cables)
        if (i >= 0 && i < kNumInputs)
            patch[(size_t) i] = o;
    setPatch (patch);
}

void FractoneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FractoneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    const auto state = juce::ValueTree::fromXml (*xml);
    restoreState (state, false);
    presets.restoreName (state.getProperty (kPresetAttr, "Init").toString());
}

// ---------------------------------------------------------------------------
// Programs (the host's preset list mirrors the panel's)
// ---------------------------------------------------------------------------
// Only the factory presets are exposed as host programs, and the count never
// changes. Hosts latch the program count when they load the plugin, so a list
// that grew as user presets were saved left the host clamping to its stale
// maximum and writing that index straight back, which loaded the last factory
// preset over whatever had just been chosen or saved.
int FractoneProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

int FractoneProcessor::getCurrentProgram()
{
    // Track the panel while it is on a factory preset; hold the last one while
    // it is on a user preset, since those have no program of their own.
    const int index = presets.currentIndex();
    if (index >= 0 && index < getNumPrograms() && ! presets.currentIsUser())
        lastProgram = index;
    return lastProgram;
}

void FractoneProcessor::setCurrentProgram (int i)
{
    i = juce::jlimit (0, getNumPrograms() - 1, i);

    // While a user preset is loaded we report lastProgram as a placeholder, so
    // the host echoing that same index back is not a request to change program.
    if (i == lastProgram && presets.currentIsUser())
        return;

    lastProgram = i;
    presets.loadIndex (i);   // factory presets occupy the first entries of list()
}

const juce::String FractoneProcessor::getProgramName (int index)
{
    const auto& factory = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) factory.size())
         ? factory[(size_t) index].name : juce::String();
}

juce::AudioProcessorEditor* FractoneProcessor::createEditor()
{
    return new FractoneEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FractoneProcessor();
}
