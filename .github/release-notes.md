The first release of Fractone, a polyrhythmic undertone synthesizer for any DAW that hosts VST3 or Audio Unit instruments.

### What's in it
- **Two oscillators with two subharmonics each.** Every sub is the oscillator's pitch divided by 1 to 16, locked in phase. A three-way wave switch picks square, pulse with saw subs, or saw.
- **Four rhythm generators and two 4-step sequencers.** Each rhythm divides the clock by 1 to 16 and can drive either sequencer, or both, for interlocking polyrhythms. Sequencers move the oscillator pitch, the sub divisors, or both. Steps can be quantized to 12- or 8-note scales in equal temperament or just intonation.
- **A resonant 4-pole ladder filter** that saturates as you drive it and self-oscillates at full resonance, plus a six-channel mixer and attack/decay envelopes for the filter and VCA.
- **A 32-point patchbay** with draggable cables for rerouting signals inside the instrument.
- **Follows your DAW's tempo and transport**, with the clock rate selectable from 1/1 to 1/64. It can also run free from its own TEMPO knob.
- **MIDI**: clip notes transpose the oscillators and open the envelopes, and a set of CCs controls pitch, sub divisors and envelope times.
- **Presets**: 11 factory patches, plus your own presets saved as files with every knob, switch and cable.

### Install
Download the file for your system below.

- **macOS** (universal, Apple silicon + Intel): unzip, move `Fractone.vst3` into `~/Library/Audio/Plug-Ins/VST3` and `Fractone.component` into `~/Library/Audio/Plug-Ins/Components`, then run:
  ```sh
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Fractone.vst3 ~/Library/Audio/Plug-Ins/Components/Fractone.component
  ```
  The build isn't signed with an Apple developer certificate, so without this step your DAW won't load it. `Fractone.app` is a standalone version you can run without a DAW.
- **Windows**: unzip and copy the `Fractone.vst3` folder into `C:\Program Files\Common Files\VST3`. `Fractone.exe` is the standalone version.
- **Linux**: extract, then copy `Fractone.vst3` into `~/.vst3`.

Then rescan plug-ins in your DAW. Fractone makes its own sound: drop it on a MIDI track and press play. The README has a section-by-section guide to the panel.
