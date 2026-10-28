# Philoop Looper (experimental)

Stereo FunBox looper with MIDI-clock-quantized start and end, plus granular
playback controls. See the [control cheat sheet](looper_cheatsheet.svg).

This is a work-in-progress effect built for the FunBox hardware. Back up any
working firmware before testing it.

Prebuilt firmware: [`build/looper.bin`](build/looper.bin) and
[`build/looper.hex`](build/looper.hex).

From the repository root, after building libDaisy and DaisySP:

```sh
make -C software/Philoop/Looper
make -C software/Philoop/Looper program-dfu
```
