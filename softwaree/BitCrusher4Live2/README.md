# BitCrusher4Live2

Live-Version auf Basis von BitCrusher4Live. Phaser wird per Shift + Switch 1 links gelatcht, Flanger per Shift + Switch 1 rechts. Beide haben je 5 eigene Presets. The Phaser and Flanger use local Funbox cores.

## Signal Modes

| Control | Description |
| --- | --- |
| Switch 1 left | Bitcrush/sample-rate reducer into filter. |
| Switch 1 middle | Thru into filter. |
| Switch 1 right | Ringmod with fixed sine oscillator, then second sample-rate reducer, then filter. |
| Switch 2 left | Normal live route with expression assignment 1. |
| Switch 2 middle | Normal live route, expression disabled. |
| Switch 2 right | Normal live route with expression assignment 2. |
| Switch 3 left | Lowpass filter. |
| Switch 3 middle | Bandpass filter. |
| Switch 3 right | Highpass filter. |

## Normal Controls

| Control | Description |
| --- | --- |
| Knob 1 | Bit depth. In ringmod mode: sine oscillator frequency. |
| Knob 2 | Sample-rate reduction. In ringmod mode: second sample-rate reducer after ringmod. |
| Knob 3 | Filter cutoff. |
| Knob 4 | Envelope amount to bit depth. In ringmod mode: envelope amount to sine frequency. Bipolar, center is neutral. |
| Knob 5 | Envelope amount to sample-rate reduction. Bipolar, center is neutral. |
| Knob 6 | Envelope amount to cutoff. Bipolar, center is neutral. Uses an intensified range for stronger filter sweeps. |
| Shift + Knob 3 | Filter resonance. |
| Shift + Knob 4 | Envelope follower input gain. Stored globally, independent of presets, and used by all modes. Phaser and Flanger multiply this by 3 internally. |
| Shift + Knob 5 | Envelope attack. Stored globally, independent of presets. |
| Shift + Knob 6 | Envelope release. Stored globally, independent of presets. |

Each Switch 1 position has its own normal 5-preset bank. Moving Switch 1 in normal route mode loads the same preset number from the new bank, or preset 1 if no preset number is active. Switch 1 and Switch 2 are not stored in presets, so their physical positions stay live when a preset is loaded.

## Phaser Controls

Hold Footswitch 1 to enter Shift mode, then move Switch 1 left to latch Phaser mode. Phaser stays active until Switch 1 is moved again, then it returns to the Switch 1 layer that was active before Phaser was latched.

Phaser Shift values are separate from the normal Shift layer. For example, Phaser Shift + Knob 3 controls Phaser feedback and does not change the normal filter resonance. When re-entering a layer, the stored value stays active until the physical knob is moved, then it jumps to the knob position.

| Control | Description |
| --- | --- |
| Knob 1 | Manual offset frequency. |
| Knob 2 | LFO speed. |
| Knob 3 | LFO depth. |
| Knob 4 | Envelope to offset frequency. Bipolar, center is neutral. Wide range for strong frequency sweeps. |
| Knob 5 | Envelope to LFO speed. Bipolar, center is neutral. Phaser envelope detection uses 3x input gain for stronger response. |
| Knob 6 | Envelope to LFO depth. Bipolar, center is neutral. |
| Shift + Knob 1 | Stereo LFO phase spread. Left is normal, right moves up to inverted phase at full clockwise. Full counterclockwise is same phase. |
| Shift + Knob 2 | LFO waveform: triangle, ramp, saw, square, sample-and-hold random. Ramp, saw, square, and random have light slew. |
| Shift + Knob 3 | Resonance/feedback. Local Phaser core allows stronger feedback than stock DaisySP. |
| Shift + Knob 4 | Envelope to stereo LFO phase spread. Bipolar, center is neutral. |
| Shift + Knob 5 | Phaser stages: 2, 4, 6, or 8. |
| Shift + Knob 6 | Envelope to resonance/feedback. Bipolar, center is neutral. |

## Flanger Controls

Hold Footswitch 1 to enter Shift mode, then move Switch 1 right to latch Flanger mode. Flanger stays active until Switch 1 is moved again, then it returns to the Switch 1 layer that was active before Flanger was latched.

Flanger uses the same normal controls as Phaser, but Knob 1 controls center delay time instead of Phaser frequency. The range goes from very short flanger times through chorus into short delay times.

| Control | Description |
| --- | --- |
| Knob 1 | Center delay time: flanger, chorus, short delay. |
| Knob 2 | LFO speed. |
| Knob 3 | LFO depth. |
| Knob 4 | Envelope to center delay time. Bipolar, center is neutral. |
| Knob 5 | Envelope to LFO speed. Bipolar, center is neutral. |
| Knob 6 | Envelope to LFO depth. Bipolar, center is neutral. |
| Shift + Knob 1 | Stereo LFO phase spread. Left is normal, right moves up to inverted phase at full clockwise. Full counterclockwise is same phase. |
| Shift + Knob 2 | LFO waveform: triangle, ramp, saw, square, sample-and-hold random. |
| Shift + Knob 3 | Feedback. Bipolar, center is neutral. |
| Shift + Knob 4 | Envelope to stereo LFO phase spread. Bipolar, center is neutral. |
| Shift + Knob 5 | LFO slew rate for ramp, saw, square, and random waveforms. |
| Shift + Knob 6 | Envelope to feedback. Bipolar, center is neutral. |

## Expression

Switch 2 selects the expression assignment slot.

| Action | Result |
| --- | --- |
| Switch 2 left + hold both footswitches | Set/edit expression assignment 1. |
| Switch 2 right + hold both footswitches | Set/edit expression assignment 2. |
| Switch 2 middle | Expression disabled. |
| Hold both footswitches again while editing | Exit expression set mode and save the selected assignment to flash. |
| Hold both footswitches for 2 seconds | Clear and save the selected assignment. |

## Presets

There are 5 normal presets for each Switch 1 position, plus 5 Phaser presets and 5 Flanger presets. Preset selection does not change bypass, so a preset can be selected while the effect is off and then engaged with Footswitch 1. Switch 1 and Switch 2 are not stored in presets; Switch 3 is stored. When Phaser or Flanger is latched, preset 1 of that bank loads automatically. Footswitch 2 can still recall, save, and turn off presets in the active bank.

| Action | Result |
| --- | --- |
| 1-5 short clicks on Footswitch 2 | Recall preset 1-5 in the current route bank. If that preset is already active, save edits back to that preset. |
| Hold Footswitch 2 for 700-999 ms | Enter save-select mode as soon as the hold threshold is reached. LED2 lights while waiting for preset input. |
| In save-select mode, 1-5 short clicks | Save current state to preset 1-5 in the current route bank. |
| Hold Footswitch 2 for 1.5 seconds or longer | Presets off in the current bank. Phaser/Flanger will still auto-load preset 1 the next time their latch is entered. |

When a preset is active, LED2 blinks the preset number in a 200 ms pattern, then stays off for two 200 ms steps before repeating.

## Global

| Control | Description |
| --- | --- |
| DIP 1 | Stereo input when on, mono input copied to both channels when off. |
| DIP 4 | High envelope input gain: multiplies envelope detector gain by 3.5. |
| Footswitch 1 | Bypass. Hold for shift mode even while bypass is on; LED1 is dim while shift is active. |
| Footswitch 2 | Preset recall/save. |
| LED 1 | Effect on/off, shift, and expression feedback. |
| LED 2 | Preset/save and expression feedback. |

Audio block size is 16 samples.

## Build

```bash
cd /i/Daisy/FunBox/FunBox/softwaree/BitCrusher4Live2
make
make program-dfu
```
