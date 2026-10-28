#ifndef TEMPO_H
#define TEMPO_H

#include "daisy_petal.h"

namespace tempo
{

enum class Source
{
    Knob,
    Tap,
    Midi,
};

enum class SyncMode
{
    Tap,
    Midi,
};

static constexpr float kDefaultBeatSec = 0.5f;
static constexpr float kMinBeatSec     = 0.125f;
static constexpr float kMaxBeatSec     = 4.0f;
static constexpr uint32_t kMidiClockTimeoutMs = 500;
static constexpr uint32_t kTapMidiLockoutMs = 8000;
static constexpr uint32_t kLedPulseMs = 35;
static constexpr float kMidiRetakeThresholdSec = 0.010f;
static constexpr float kMidiSmoothing = 0.08f;

static float samplerate = 48000.0f;
static float beat_sec = kDefaultBeatSec;
static float smoothed_delay_samples = kDefaultBeatSec * 48000.0f;
static Source source = Source::Knob;
static SyncMode sync_mode = SyncMode::Tap;

static uint32_t last_tap_ms = 0;
static uint32_t last_midi_clock_us = 0;
static uint32_t last_midi_clock_ms = 0;
static uint32_t midi_beat_start_us = 0;
static uint32_t ignore_midi_until_ms = 0;
static uint32_t last_beat_ms = 0;
static uint32_t led_pulse_until_ms = 0;
static float pulse_multiplier = 1.0f;
static uint16_t midi_clock_count = 0;
static uint8_t midi_tempo_clock_count = 0;
static uint16_t midi_bar_clock_count = 0;
static uint32_t midi_total_clock_count = 0;

inline void TriggerBeatPulse(uint32_t now_ms)
{
    last_beat_ms = now_ms;
    led_pulse_until_ms = now_ms + kLedPulseMs;
}

inline void Init(float sample_rate)
{
    samplerate = sample_rate;
    beat_sec = kDefaultBeatSec;
    smoothed_delay_samples = beat_sec * samplerate;
    source = Source::Knob;
    sync_mode = SyncMode::Tap;
    last_tap_ms = 0;
    last_midi_clock_us = 0;
    last_midi_clock_ms = 0;
    midi_beat_start_us = 0;
    ignore_midi_until_ms = 0;
    pulse_multiplier = 1.0f;
    midi_clock_count = 0;
    midi_tempo_clock_count = 0;
    midi_bar_clock_count = 0;
    midi_total_clock_count = 0;
    TriggerBeatPulse(daisy::System::GetNow());
}

inline void SetSyncMode(SyncMode mode)
{
    sync_mode = mode;
}

inline bool MidiSyncEnabled()
{
    return source == Source::Midi;
}

inline bool TapSyncEnabled()
{
    return source == Source::Tap;
}

inline bool Active()
{
    return source == Source::Tap || source == Source::Midi;
}

inline Source CurrentSource()
{
    return source;
}

inline bool MidiClockPresent()
{
    return (daisy::System::GetNow() - last_midi_clock_ms) <= kMidiClockTimeoutMs;
}

inline uint32_t MidiClockCount()
{
    return midi_total_clock_count;
}

inline bool MidiAtSixteenth()
{
    return (midi_bar_clock_count % 6) == 0;
}

inline bool MidiAtBar()
{
    return midi_bar_clock_count == 0;
}

inline float BeatSeconds()
{
    return beat_sec;
}

inline float DivisionMultiplier(float knob)
{
    if(knob < 0.0909f)
        return 0.25f; // 1/16
    if(knob < 0.1818f)
        return 0.5f; // 1/8
    if(knob < 0.2727f)
        return 0.333333f; // 1/8 triplet
    if(knob < 0.3636f)
        return 0.75f; // dotted 1/8
    if(knob < 0.4545f)
        return 0.666667f; // 1/4 triplet
    if(knob < 0.5455f)
        return 1.0f; // 1/4
    if(knob < 0.6364f)
        return 1.5f; // dotted 1/4
    if(knob < 0.7273f)
        return 2.0f; // 1/2
    if(knob < 0.8182f)
        return 4.0f; // 1/1
    if(knob < 0.9091f)
        return 8.0f; // 2 bars
    return 16.0f;    // 4 bars
}

inline void RegisterTap()
{
    const uint32_t now = daisy::System::GetNow();

    if(last_tap_ms != 0)
    {
        const float interval_sec = (now - last_tap_ms) * 0.001f;
        if(interval_sec >= kMinBeatSec && interval_sec <= kMaxBeatSec)
        {
            beat_sec = (beat_sec * 0.7f) + (interval_sec * 0.3f);
        }
    }

    last_tap_ms = now;
    source = Source::Tap;
    ignore_midi_until_ms = now + kTapMidiLockoutMs;
    TriggerBeatPulse(now);
}

inline void RegisterExternalTap()
{
    RegisterTap();
}

inline void HandleMidiClock()
{
    const uint32_t now_us = daisy::System::GetUs();
    const uint32_t now_ms = daisy::System::GetNow();

    if(now_ms < ignore_midi_until_ms)
    {
        last_midi_clock_us = now_us;
        last_midi_clock_ms = now_ms;
        midi_beat_start_us = now_us;
        midi_tempo_clock_count = 0;
        midi_clock_count = 0;
        midi_total_clock_count += 1;
        midi_bar_clock_count = (midi_bar_clock_count + 1) % 96;
        return;
    }

    last_midi_clock_us = now_us;
    last_midi_clock_ms = now_ms;
    midi_total_clock_count += 1;
    midi_bar_clock_count = (midi_bar_clock_count + 1) % 96;

    if(midi_beat_start_us == 0)
    {
        midi_beat_start_us = now_us;
        midi_tempo_clock_count = 0;
        return;
    }

    midi_tempo_clock_count += 1;

    if(midi_tempo_clock_count >= 24)
    {
        const uint32_t beat_us = now_us - midi_beat_start_us;
        const float new_beat_sec = beat_us * 0.000001f;

        if(new_beat_sec >= kMinBeatSec && new_beat_sec <= kMaxBeatSec)
        {
            const bool midi_can_retake
                = source != Source::Tap
                  || new_beat_sec > beat_sec + kMidiRetakeThresholdSec
                  || new_beat_sec < beat_sec - kMidiRetakeThresholdSec;

            if(midi_can_retake)
            {
                beat_sec = (beat_sec * (1.0f - kMidiSmoothing)) + (new_beat_sec * kMidiSmoothing);
                source = Source::Midi;
            }
        }

        midi_beat_start_us = now_us;
        midi_tempo_clock_count = 0;
    }

    if(source == Source::Midi)
    {
        midi_clock_count += 1;
        const uint16_t clock_threshold
            = static_cast<uint16_t>((24.0f * pulse_multiplier) + 0.5f);
        if(midi_clock_count >= clock_threshold)
        {
            midi_clock_count = 0;
            TriggerBeatPulse(now_ms);
        }
    }
}

inline void Process(float division_knob)
{
    const uint32_t now = daisy::System::GetNow();
    pulse_multiplier = DivisionMultiplier(division_knob);

    if(source == Source::Midi && (now - last_midi_clock_ms) > kMidiClockTimeoutMs)
    {
        source = last_tap_ms == 0 ? Source::Knob : Source::Tap;
    }

    if(source == Source::Tap)
    {
        const uint32_t pulse_ms = static_cast<uint32_t>((beat_sec * pulse_multiplier * 1000.0f) + 0.5f);
        if(pulse_ms > 0 && now - last_beat_ms >= pulse_ms)
        {
            TriggerBeatPulse(now);
        }
    }
}

inline float DelaySamples(float knob)
{
    const float target = beat_sec * DivisionMultiplier(knob) * samplerate;
    smoothed_delay_samples = (smoothed_delay_samples * 0.995f) + (target * 0.005f);
    return smoothed_delay_samples;
}

inline bool LedPulseActive()
{
    return daisy::System::GetNow() < led_pulse_until_ms;
}

} // namespace tempo

#endif
