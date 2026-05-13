# BitCrusher4Live

Live-Version auf Basis von BitCrusher2. Comb und Flanger sind entfernt; Switch 2 waehlt stattdessen zwei gespeicherte Expression-Zuweisungen.

## Signal Modes

| Control | Description |
| --- | --- |
| Switch 1 left | Bitcrush/sample-rate reducer into filter. |
| Switch 1 middle | Thru into filter. |
| Switch 1 right | Ringmod with fixed sine oscillator, then second sample-rate reducer, then filter. |
| Switch 2 left | Expression assignment 1. |
| Switch 2 middle | No expression assignment. |
| Switch 2 right | Expression assignment 2. |
| Switch 3 left | Lowpass filter. |
| Switch 3 middle | Bandpass filter. |
| Switch 3 right | Highpass filter. |

## Controls

| Control | Description |
| --- | --- |
| Knob 1 | Bit depth. In ringmod mode: sine oscillator frequency. |
| Knob 2 | Sample-rate reduction. In ringmod mode: second sample-rate reducer after ringmod. |
| Knob 3 | Filter cutoff. |
| Knob 4 | Envelope amount to bit depth. In ringmod mode: envelope amount to sine frequency. Bipolar, center is neutral. |
| Knob 5 | Envelope amount to sample-rate reduction. Bipolar, center is neutral. |
| Knob 6 | Envelope amount to cutoff. Bipolar, center is neutral. |
| Shift + Knob 3 | Filter resonance. |
| Shift + Knob 4 | Envelope follower input gain. |
| Shift + Knob 5 | Envelope attack. |
| Shift + Knob 6 | Envelope release. |

Shift values are stored globally in flash after editing. Presets still store their own shift values and override the live values when recalled.

## Expression

Switch 2 selects which expression assignment is active.

| Action | Result |
| --- | --- |
| Switch 2 left + hold both footswitches | Set/edit expression assignment 1. |
| Switch 2 right + hold both footswitches | Set/edit expression assignment 2. |
| Switch 2 middle | Expression disabled. |
| Hold both footswitches again while editing | Exit expression set mode and save the selected assignment to flash. |
| Hold both footswitches for 2 seconds | Clear and save the selected assignment. |

Expression assignments are stored separately from presets.

## Presets

There are 5 presets. Preset selection does not change bypass, so a preset can be selected while the effect is off and then engaged with Footswitch 1.

| Action | Result |
| --- | --- |
| 1-5 short clicks on Footswitch 2 | Recall preset 1-5. If that preset is already active, save edits back to that preset. |
| Hold Footswitch 2 for 700-999 ms | Enter save-select mode. LED2 blinks fast. |
| In save-select mode, 1-5 short clicks | Save current state to preset 1-5. |
| Hold Footswitch 2 for 1 second or longer | Presets off, normal mode. |

When a preset is active, LED2 blinks the preset number in a 200 ms pattern, then pauses once. Leaving expression set mode suppresses the Footswitch 2 release, so it does not recall preset 1.

## Global

| Control | Description |
| --- | --- |
| DIP 1 | Stereo input when on, mono input copied to both channels when off. |
| DIP 4 | High envelope input gain: multiplies envelope detector gain by 3.5. |
| Footswitch 1 | Bypass. Hold for shift mode. |
| Footswitch 2 | Preset recall/save. |
| LED 1 | Effect on/off, shift, and expression feedback. |
| LED 2 | Preset/save and expression feedback. |

Audio block size is 16 samples.

## Build

```bash
cd /i/Daisy/FunBox/FunBox/softwaree/BitCrusher4Live
make
make program-dfu
```
