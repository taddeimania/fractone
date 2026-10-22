// PresetManager.h — factory presets plus user presets saved as files.
//
// User presets live in   <user app data>/Fractone/Presets/*.fractone
//   macOS: ~/Library/Application Support/Fractone/Presets
//   Windows: %APPDATA%\Fractone\Presets
//   Linux: ~/.config/Fractone/Presets
// Each file is the same XML the plugin stores in a Live set, so a preset
// carries every knob, switch and patch cable.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Presets.h"

class FractoneProcessor;

namespace fractone
{

class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    explicit PresetManager (FractoneProcessor& p) : proc (p) {}

    static juce::File presetFolder();
    static constexpr const char* extension = ".fractone";

    // Factory entries first, then user presets sorted by name.
    juce::Array<Entry> list() const;

    bool load (const Entry& e);
    bool loadIndex (int index);              // index into list()
    void step (int delta);                   // previous / next

    // Save the current panel as a user preset. Overwrites a user preset with the same name.
    bool saveAs (const juce::String& name);
    bool remove (const Entry& e);

    juce::String currentName() const { return current; }
    void restoreName (const juce::String& name);   // after the host restores a session
    void markEdited() { if (juce::Time::getMillisecondCounter() > ignoreEditsUntil.load()) edited = true; }
    bool isEdited() const { return edited.load(); }
    bool currentIsUser() const;
    int  currentIndex() const;

    static juce::String sanitise (const juce::String& name);

private:
    FractoneProcessor& proc;
    juce::String current { "Init" };
    bool currentUser = false;
    std::atomic<bool> edited { false };
    std::atomic<juce::uint32> ignoreEditsUntil { 0 };
    void settle() { edited = false; ignoreEditsUntil = juce::Time::getMillisecondCounter() + 300; }
};
} // namespace fractone
