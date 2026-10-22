Fractone 1.1.0 adds DICE: one click writes a musical phrase for a sequencer.

### New
- **DICE** buttons on Sequencer 1 and Sequencer 2. Step 1 stays where you set it, and steps 2-4 each land a consonant interval above or below it: a 3rd, 4th, 5th, 6th, unison or octave, never a 2nd, 7th or tritone.
  - Rolls stay within the SEQ OCT range and follow QUANTIZE. In 8-ET and 8-JI every step stays in the scale. In 12-ET and 12-JI each roll is either major- or minor-flavoured, so its thirds and sixths never clash.
  - Smaller moves are favoured over big leaps, no note plays three times in a row, and every roll changes at least one step. Click again to re-roll; your host's undo brings back the previous steps.
- **DICE is MIDI-mappable.** The new **Dice Seq 1** and **Dice Seq 2** parameters roll whenever they switch on, so a pad, a Push button, a rack Macro or an automation lane can roll a new phrase. Map them as momentary buttons. Loading a preset or reopening a set never rolls.

DICE isn't on the original hardware. Everything else about Fractone's sound and panel is unchanged, and existing sets and presets load as before.

### Install
Download the file for your system below.

- **macOS** (universal, Apple silicon + Intel): unzip, move `Fractone.vst3` into `~/Library/Audio/Plug-Ins/VST3` and `Fractone.component` into `~/Library/Audio/Plug-Ins/Components`, then run:
  ```sh
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Fractone.vst3 ~/Library/Audio/Plug-Ins/Components/Fractone.component
  ```
  The build isn't signed with an Apple developer certificate, so without this step your DAW won't load it. `Fractone.app` is a standalone version you can run without a DAW.
- **Windows**: unzip and copy the `Fractone.vst3` folder into `C:\Program Files\Common Files\VST3`. `Fractone.exe` is the standalone version.
- **Linux**: extract, then copy `Fractone.vst3` into `~/.vst3`.

Replace the old plugin files rather than keeping both, then rescan plug-ins in your DAW.
