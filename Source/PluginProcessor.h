#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/Engine.h"
#include "Params.h"
#include "PresetManager.h"

#include <atomic>
#include <unordered_map>

class FractoneProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::AsyncUpdater
{
public:
    FractoneProcessor();
    ~FractoneProcessor() override;

    // --- AudioProcessor ------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Patchbay (message thread) -------------------------------------------
    int  sourceFor (int input) const noexcept { return patchSrc[(size_t) input].load(); }
    void connect (int output, int input);
    void disconnect (int input);
    void clearPatch();
    fractone::Patch currentPatch() const noexcept;
    void setPatch (const fractone::Patch&);
    std::atomic<int> patchVersion { 0 };

    // --- Panel actions (message thread) --------------------------------------
    void uiTogglePlay()               { playToggles.fetch_add (1); }
    void uiResetPress()               { resetPresses.fetch_add (1); }
    void uiResetHeld (bool down)      { resetHeld.store (down); }
    void uiNext()                     { nextPresses.fetch_add (1); }
    void uiTriggerHeld (bool down)    { triggerHeld.store (down); }

    // DICE: keep step 1 of a sequencer and roll steps 2-4 to consonant
    // intervals from it (message thread; changes go through the host params).
    void rollDice (int seq);

    // Panel DICE button: pulses the "Dice Seq N" parameter on and off, so a
    // click goes through the same path as a MIDI-mapped or automated roll.
    void triggerDice (int seq);

    // Runs any rolls queued by the Dice parameters right away (for tests;
    // normally they run on the message thread moments later).
    void flushPendingDice() { handleUpdateNowIfNeeded(); }

    // --- State (presets & host) ----------------------------------------------
    juce::ValueTree captureState();
    // Restores a state. When `asPreset` is true the per-instance settings
    // (host sync, MIDI channel, fine tune) are left as they are.
    void restoreState (const juce::ValueTree& state, bool asPreset);
    void applyFactoryPreset (const fractone::Preset&);

    juce::AudioProcessorValueTreeState apvts;
    fractone::PresetManager presets { *this };

    // --- Readouts for the panel ------------------------------------------------
    std::atomic<int>  uiStep[2] { { 0 }, { 0 } };
    std::atomic<bool> uiPlayOn { true }, uiRunning { false }, uiHostPlaying { false };
    std::atomic<uint32_t> uiRhythmFires[4] { { 0 }, { 0 }, { 0 }, { 0 } };
    std::atomic<uint32_t> uiClock { 0 }, uiMidi { 0 };
    std::atomic<float> uiVcaEnv { 0.0f }, uiVcfEnv { 0.0f };
    std::atomic<int>  uiSubDiv[2][2] { { { 1 }, { 1 } }, { { 1 }, { 1 } } };
    std::atomic<int>  uiRhythmDiv[4] { { 1 }, { 1 }, { 1 }, { 1 } };
    std::atomic<float> uiVcoHz[2] { { 261.6f }, { 261.6f } };
    std::atomic<double> uiHostBpm { 120.0 };

    float get (const juce::String& id) const;

private:
    fractone::EngineParams readParams() const;
    void handleMidi (const juce::MidiMessage&);
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;

    // Dice parameters: last seen value (for switch-on detection) and queued rolls.
    std::atomic<bool> diceLast[2] { { false }, { false } };
    std::atomic<bool> dicePending[2] { { false }, { false } };

    fractone::Engine engine;
    std::array<std::atomic<int>, fractone::kNumInputs> patchSrc;
    std::unordered_map<juce::String, std::atomic<float>*> params;

    std::atomic<int>  playToggles { 0 }, resetPresses { 0 }, nextPresses { 0 };
    std::atomic<bool> resetHeld { false }, triggerHeld { false };
    int seenPlayToggles = 0, seenResetPresses = 0, seenNextPresses = 0;
    bool playOnRestore = true;
    std::atomic<bool> pendingPlayState { false };
    std::atomic<bool> pendingPlayValue { true };

    // Knob-moved detection for MIDI CC overrides
    std::array<std::array<int, 2>, 2> lastSubKnob {};
    std::array<float, 4> lastEgKnob { -1, -1, -1, -1 };
    int lastLogicKnob = -1;

    // The factory program the host last saw. Held while a user preset is
    // loaded, since user presets are not exposed as host programs.
    int lastProgram = 0;

    std::vector<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractoneProcessor)
};
