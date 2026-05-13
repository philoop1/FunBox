# BitCrusher

## Controls

| Control | Description |
| --- | --- |
| Knob 1 | Bit depth. In ringmod mode: oscillator frequency. |
| Knob 2 | Sample-rate reduction. In ringmod mode: waveform 1-5. |
| Knob 3 | Filter cutoff. |
| Knob 4 | Envelope amount to bit depth. In ringmod mode: envelope amount to frequency. Bipolar, center is neutral. |
| Knob 5 | Envelope amount to sample-rate reduction. Bipolar, center is neutral. |
| Knob 6 | Envelope amount to cutoff. Bipolar, center is neutral. |
| Shift + Knob 3 | Filter resonance / notch bandwidth. |
| Shift + Knob 4 | Envelope follower input gain. |
| Shift + Knob 5 | Envelope attack. |
| Shift + Knob 6 | Envelope release. |
| Switch 1 | Left: bitcrush into filter. Middle: thru into filter. Right: ringmod into filter. |
| Switch 2 | Only when Switch 3 is in comb mode: left comb, middle highpass, right notch/soap. |
| Switch 3 | Left ladder filter, middle bandpass, right comb/filter family. |
| DIP 1 | Stereo input when on, mono input copied to both channels when off. |
| DIP 4 | High envelope input gain: multiplies envelope detector gain by 3.5. |
| Footswitch 1 | Bypass. Hold for shift mode. Hold both footswitches for expression set/reset. |
| Footswitch 2 | Preset recall/save. |
| LED 1 | Effect on/off and shift indication. |
| LED 2 | Preset/save feedback. |

## Presets

There are 4 presets. Presets store knob values, shift values, switches, DIP states, and expression assignment.

| Action | Result |
| --- | --- |
| 1-4 short clicks on Footswitch 2 | Recall preset 1-4. If that preset is already active, save edits back to the active preset. |
| Hold Footswitch 2 for 700-999 ms | Enter save-select mode. LED2 blinks fast. |
| In save-select mode, 1-4 short clicks | Save current state to preset 1-4. |
| Hold Footswitch 2 for 1 second or longer | Presets off, normal mode. |

When a preset is active, LED2 blinks the preset number in a 200 ms pattern, then pauses once.

## Build

```bash
cd /i/Daisy/FunBox/FunBox/softwaree/BitCrusher
make
make program-dfu
```
