A bug-fix release. Fractone's sound and panel are unchanged from 1.0.0.

### Fixed
- **Saving or loading one of your own presets no longer snaps the panel back to the last factory preset.** Fractone offered its whole preset list, factory patches and yours together, as the host's program list. A host settles on how many programs a plugin has when it loads it, so saving a preset made the list longer than the host believed it to be; the host clamped to the highest index it knew about and wrote that straight back, which loaded the last factory preset over the patch you had just saved or picked. Only the eleven factory patches are offered as host programs now, and that number never changes. Your DAW's own preset menu still selects them as before.

Presets saved with 1.0.0 were always written to disk correctly, so nothing you saved has been lost. They load properly in this version.

### Install
Download the file for your system below.

- **macOS** (universal, Apple silicon + Intel): unzip, move `Fractone.vst3` into `~/Library/Audio/Plug-Ins/VST3` and `Fractone.component` into `~/Library/Audio/Plug-Ins/Components`, then run:
  ```sh
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Fractone.vst3 ~/Library/Audio/Plug-Ins/Components/Fractone.component
  ```
  The build isn't signed with an Apple developer certificate, so without this step your DAW won't load it. `Fractone.app` is a standalone version you can run without a DAW.
- **Windows**: unzip and copy the `Fractone.vst3` folder into `C:\Program Files\Common Files\VST3`. `Fractone.exe` is the standalone version.
- **Linux**: extract, then copy `Fractone.vst3` into `~/.vst3`.

Then rescan plug-ins in your DAW. If you are upgrading, replace the old files rather than keeping both.

Fractone is a polyrhythmic undertone synthesizer that makes its own sound: drop it on a MIDI track and press play. The README has a section-by-section guide to the panel.
