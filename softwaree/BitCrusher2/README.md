# BitCrusher2

Zweite BitCrusher-Version. Die originale Version bleibt in `softwaree/BitCrusher`.

## Signal Modes

| Control | Description |
| --- | --- |
| Switch 1 left | Bitcrush/sample-rate reducer into filter. |
| Switch 1 middle | Thru into filter. |
| Switch 1 right | Ringmod with fixed sine oscillator, then second sample-rate reducer, then filter. |
| Switch 2 left | Normal route: Switch 1 into filter. |
| Switch 2 middle | Triple-comb route with three parallel comb filters. |
| Switch 2 right | Triple-flanger route with three parallel flangers. |
| Switch 3 left | Lowpass filter. |
| Switch 3 middle | Bandpass filter. |
| Switch 3 right | Highpass filter. |

Notch/Soap is removed in this version.

## Normal Route Controls

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

## Triple-Comb Route Controls

Switch 2 middle selects Triple-Comb. It uses its own preset bank.

| Control | Description |
| --- | --- |
| Knob 1 | Comb 1 frequency. |
| Knob 2 | Comb 2 frequency. |
| Knob 3 | Comb 3 frequency. |
| Knob 4 | Envelope amount to Comb 1 frequency. Bipolar, center is neutral. |
| Knob 5 | Envelope amount to Comb 2 frequency. Bipolar, center is neutral. |
| Knob 6 | Envelope amount to Comb 3 frequency. Bipolar, center is neutral. |
| Shift + Knob 1 | Global dry/wet for all three combs. |
| Shift + Knob 2 | Sine LFO frequency for comb-frequency modulation. |
| Shift + Knob 3 | Global resonance/decay for all three combs. |
| Shift + Knob 4 | LFO amount to Comb 1 frequency. Bipolar, center is neutral. |
| Shift + Knob 5 | LFO amount to Comb 2 frequency. Bipolar, center is neutral. |
| Shift + Knob 6 | LFO amount to Comb 3 frequency. Bipolar, center is neutral. |

Knob 4-6 envelope amounts and Shift + Knob 4-6 LFO amounts have a small center deadband, so middle position is really neutral.

## Triple-Flanger Route Controls

Switch 2 right selects Triple-Flanger. It uses its own preset bank.

| Control | Description |
| --- | --- |
| Knob 1 | Flanger 1 delay/color, 1-19 ms. |
| Knob 2 | Flanger 2 delay/color, 1-19 ms. |
| Knob 3 | Flanger 3 delay/color, 1-19 ms. |
| Knob 4 | Envelope amount to Flanger 1 delay. Bipolar, center is neutral. |
| Knob 5 | Envelope amount to Flanger 2 delay. Bipolar, center is neutral. |
| Knob 6 | Envelope amount to Flanger 3 delay. Bipolar, center is neutral. |
| Shift + Knob 1 | Global dry/wet for all three flangers. |
| Shift + Knob 2 | Flanger LFO frequency for all three flangers. |
| Shift + Knob 3 | Global feedback/resonance for all three flangers. |
| Shift + Knob 4 | Flanger 1 LFO depth. |
| Shift + Knob 5 | Flanger 2 LFO depth. |
| Shift + Knob 6 | Flanger 3 LFO depth. |

## Global

| Control | Description |
| --- | --- |
| DIP 1 | Stereo input when on, mono input copied to both channels when off. |
| DIP 4 | High envelope input gain: multiplies envelope detector gain by 3.5. |
| Footswitch 1 | Bypass. Hold for shift mode. Hold both footswitches for expression set/reset. |
| Footswitch 2 | Preset recall/save. |
| LED 1 | Effect on/off and shift indication. |
| LED 2 | Preset/save feedback. |

## Presets

There are 5 normal presets, 5 Triple-Comb presets, and 5 Triple-Flanger presets. Presets store knobs, shift values, switches, DIP states, and expression assignment.

Preset selection does not change bypass. You can choose a preset while the effect is off, then turn it on with Footswitch 1.

| Action | Result |
| --- | --- |
| 1-5 short clicks on Footswitch 2 | Recall preset 1-5 in the current route bank. If that preset is already active, save edits back to that preset. |
| Hold Footswitch 2 for 700-999 ms | Enter save-select mode. LED2 blinks fast. |
| In save-select mode, 1-5 short clicks | Save current state to preset 1-5 in the current route bank. |
| Hold Footswitch 2 for 1 second or longer | Presets off, normal mode. |

When a preset is active, LED2 blinks the preset number in a 200 ms pattern, then pauses once.

## Build

```bash
cd /i/Daisy/FunBox/FunBox/softwaree/BitCrusher2
make
make program-dfu
```
