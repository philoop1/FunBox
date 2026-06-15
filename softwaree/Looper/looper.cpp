// FunBox pure stereo looper with MIDI clock quantized start/end.

#include "daisy_petal.h"
#include "daisysp.h"
#include "funbox.h"
#include "tempo.h"

#include <algorithm>

using namespace daisy;
using namespace daisysp;
using namespace funbox;

DaisyPetal hw;
Led led1, led2;

::daisy::Parameter knob3_volume;
::daisy::Parameter knob2_metro_volume;
::daisy::Parameter knob6_feedback;
::daisy::Parameter expression;

static constexpr float kInputPad = 0.80f;
static constexpr float kWritePad = 0.80f;
static constexpr float kMonitorPad = 0.90f;
static constexpr uint32_t kLooperLongPressMs = 1500;
static constexpr uint32_t kLooperDoublePressMs = 300;
static constexpr uint32_t kLooperLedAckTicks = 120;
static constexpr uint32_t kLooperLoopStartBlinkTicks = 12;
static constexpr uint32_t kLooperErrorBlinkHalfTicks = 12;
static constexpr uint32_t kLooperErrorBlinkTicks = kLooperErrorBlinkHalfTicks * 6;
static constexpr size_t kLooperSeamFadeSamples = 512;
static constexpr uint32_t kLooperLengthTransitionSamples = 1024;
static constexpr uint32_t kPlaybackStepSlewSamples = 512;
static constexpr uint32_t kLooperSwitchFadeSamples = 1024;
static constexpr uint32_t kLooperStopFadeSamples = 2048;
static constexpr size_t kLooperMinRecordSamples = 4800;
static constexpr size_t kLooperMaxSamples = 48000 * 140;
static constexpr uint32_t kMidiLedPulseMs = 35;
static constexpr uint32_t kMidiClocksPerBeat = 24;
static constexpr uint32_t kMidiClocksPerThreeFourBar = 72;
static constexpr uint32_t kMidiClocksPerFourFourBar = 96;
static constexpr uint32_t kMetroClickSamples = 1200;
static constexpr float kMetroNormalPitch = 1200.0f;
static constexpr float kMetroOnePitch = 1800.0f;

DSY_SDRAM_BSS float loop_buffer_l[kLooperMaxSamples];
DSY_SDRAM_BSS float loop_buffer_r[kLooperMaxSamples];

enum class LooperState
{
    Empty,
    WaitingStart,
    Recording,
    WaitingEnd,
    Playing,
};

LooperState looper_state = LooperState::Empty;
size_t looper_write = 0;
size_t looper_length = 0;
size_t looper_repeat_source_length = 0;
size_t looper_repeat_fill_index = 0;
float looper_read = 0.0f;
uint32_t looper_start_clock = 0;
uint32_t looper_end_clock = 0;

enum class LooperLengthOp
{
    None,
    Halve,
    Double,
};

LooperLengthOp looper_pending_length_op = LooperLengthOp::None;
uint32_t looper_led_ack_ticks = 0;
uint32_t looper_loopstart_blink_ticks = 0;
uint32_t looper_error_blink_ticks = 0;
bool looper_length_ops_enabled = false;
uint32_t looper_transition_remaining = 0;
float looper_transition_start_l = 0.0f;
float looper_transition_start_r = 0.0f;
float looper_last_output_l = 0.0f;
float looper_last_output_r = 0.0f;
float playback_step_current = 1.0f;
float playback_step_target = 1.0f;
float playback_step_slew_delta = 0.0f;
uint32_t playback_step_slew_remaining = 0;
uint32_t looper_switch_fade_remaining = 0;
uint32_t looper_stop_fade_remaining = 0;
bool looper_quant_point_valid = false;
size_t looper_last_quant_write = 0;
uint32_t looper_last_quant_clock = 0;

bool overdub_enabled = false;
uint32_t footswitch2_down_ms = 0;
bool footswitch2_was_pressed = false;
bool footswitch2_long_handled = false;
bool footswitch2_started_loop_on_press = false;

uint32_t external_tap_down_ms = 0;
bool external_tap_pressed = false;
bool external_tap_long_handled = false;
bool external_tap_started_loop_on_press = false;

bool switch3_left = false;
bool switch3_right = false;
bool switch2_left = false;
bool switch2_right = false;
bool switch1_left = false;
bool switch1_right = false;
bool switch2_was_middle = true;
bool dip1_miso = false;
bool dip2_ignore_midi = false;
bool dip3_mono = false;

uint8_t midi_clock_led_count = 0;
uint32_t midi_led_pulse_until_ms = 0;
bool midi_transport_running = false;
uint32_t midi_transport_start_clock = 0;

bool metro_anchor_valid = false;
uint32_t metro_anchor_clock = 0;
uint32_t metro_last_beat_index = 0xffffffff;
uint32_t metro_click_remaining = 0;
float metro_click_phase = 0.0f;
float metro_click_phase_inc = 0.0f;

float LooperSoftLimit(float value)
{
    const float x = fclamp(value, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

bool LoopIsActive()
{
    return looper_state == LooperState::Playing;
}

size_t MaxLooperSamples()
{
    return dip3_mono ? kLooperMaxSamples * 2 : kLooperMaxSamples;
}

float MonoBufferRead(size_t index)
{
    if(index < kLooperMaxSamples)
        return loop_buffer_l[index];
    return loop_buffer_r[index - kLooperMaxSamples];
}

void MonoBufferWrite(size_t index, float value)
{
    if(index < kLooperMaxSamples)
        loop_buffer_l[index] = value;
    else
        loop_buffer_r[index - kLooperMaxSamples] = value;
}

void BeginLoopLengthTransition();

float TargetPlaybackStepFromSwitch()
{
    const float direction = switch1_left ? -1.0f : 1.0f;
    if(switch3_left)
        return direction * 2.0f;
    if(switch3_right)
        return direction * 0.5f;
    return direction;
}

float PlaybackStep()
{
    return playback_step_current;
}

void UpdatePlaybackStepSlew()
{
    if(playback_step_slew_remaining == 0)
        return;

    playback_step_current += playback_step_slew_delta;
    playback_step_slew_remaining -= 1;
    if(playback_step_slew_remaining == 0)
    {
        playback_step_current = playback_step_target;
        playback_step_slew_delta = 0.0f;
    }
}

void SetPlaybackStepTarget(float target)
{
    if(target == playback_step_target)
        return;

    playback_step_target = target;
    playback_step_slew_remaining = kPlaybackStepSlewSamples;
    playback_step_slew_delta = (playback_step_target - playback_step_current)
                               / static_cast<float>(kPlaybackStepSlewSamples);
    if(looper_state == LooperState::Playing)
    {
        BeginLoopLengthTransition();
        looper_switch_fade_remaining = kLooperSwitchFadeSamples;
    }
}

bool MidiClockUsable()
{
    return !dip2_ignore_midi && tempo::MidiClockPresent();
}

bool MidiTransportBarActive()
{
    return midi_transport_running && MidiClockUsable();
}

uint32_t CurrentBarClockLength()
{
    if(switch2_left)
        return kMidiClocksPerThreeFourBar;
    return kMidiClocksPerFourFourBar;
}

bool IsAllowedQuantizedBarLength(uint32_t elapsed_clocks)
{
    const uint32_t bar_clocks = CurrentBarClockLength();
    if(bar_clocks == 0 || elapsed_clocks == 0 || (elapsed_clocks % bar_clocks) != 0)
        return false;

    const uint32_t bars = elapsed_clocks / bar_clocks;
    return bars == 1 || bars == 2 || bars == 4 || bars == 8
           || bars == 16 || bars == 32 || bars == 64;
}

bool ClockAtAllowedQuantLength(uint32_t anchor_clock)
{
    return IsAllowedQuantizedBarLength(tempo::MidiClockCount() - anchor_clock);
}

bool MidiTransportAtAllowedQuantLength()
{
    return MidiTransportBarActive() && ClockAtAllowedQuantLength(midi_transport_start_clock);
}

int MetroBeatsPerBar()
{
    if(switch2_left)
        return 3;
    if(switch2_right)
        return 4;
    return 0;
}

bool QuantizeEnabled()
{
    return MetroBeatsPerBar() > 0 && MidiClockUsable();
}

uint32_t MaxAllowedQuantClocks()
{
    return CurrentBarClockLength() * 64;
}

bool TransportAtBar()
{
    if(!MidiTransportBarActive())
        return false;

    const uint32_t elapsed_clocks = tempo::MidiClockCount() - midi_transport_start_clock;
    const uint32_t bar_clocks = CurrentBarClockLength();
    return elapsed_clocks > 0 && (elapsed_clocks % bar_clocks) == 0;
}

void TriggerMetroClick(bool bar_one)
{
    metro_click_remaining = kMetroClickSamples;
    metro_click_phase = 0.0f;
    const float pitch = bar_one ? kMetroOnePitch : kMetroNormalPitch;
    metro_click_phase_inc = TWOPI_F * pitch / hw.AudioSampleRate();
}

void SetMetroAnchor(uint32_t clock_count, bool trigger_now)
{
    metro_anchor_valid = true;
    metro_anchor_clock = clock_count;
    metro_last_beat_index = 0;
    if(trigger_now && MetroBeatsPerBar() > 0)
        TriggerMetroClick(true);
}

void ResetMetroAnchor()
{
    metro_anchor_valid = false;
    metro_last_beat_index = 0xffffffff;
}

void ProcessMetroClock()
{
    const int beats_per_bar = MetroBeatsPerBar();
    if(beats_per_bar == 0 || !metro_anchor_valid)
        return;

    const uint32_t elapsed_clocks = tempo::MidiClockCount() - metro_anchor_clock;
    if((elapsed_clocks % kMidiClocksPerBeat) != 0)
        return;

    const uint32_t beat_index = elapsed_clocks / kMidiClocksPerBeat;
    if(beat_index == metro_last_beat_index)
        return;

    metro_last_beat_index = beat_index;
    TriggerMetroClick((beat_index % static_cast<uint32_t>(beats_per_bar)) == 0);
}

float ProcessMetroSample(float volume)
{
    if(metro_click_remaining == 0)
        return 0.0f;

    const float age = static_cast<float>(kMetroClickSamples - metro_click_remaining)
                      / static_cast<float>(kMetroClickSamples);
    const float env = (1.0f - age) * (1.0f - age);
    const float sample = sinf(metro_click_phase) * env * volume;
    metro_click_phase += metro_click_phase_inc;
    if(metro_click_phase >= TWOPI_F)
        metro_click_phase -= TWOPI_F;
    metro_click_remaining -= 1;
    return sample;
}

void TriggerLed2Ack()
{
    looper_led_ack_ticks = kLooperLedAckTicks;
}

void TriggerLed2LoopStartBlink()
{
    looper_loopstart_blink_ticks = kLooperLoopStartBlinkTicks;
}

void TriggerLed2ErrorBlink()
{
    looper_error_blink_ticks = kLooperErrorBlinkTicks;
}

void BeginLoopLengthTransition()
{
    looper_transition_start_l = looper_last_output_l;
    looper_transition_start_r = looper_last_output_r;
    looper_transition_remaining = kLooperLengthTransitionSamples;
}

void StartLoopRecording()
{
    looper_state = LooperState::Recording;
    looper_write = 0;
    looper_length = 0;
    looper_repeat_source_length = 0;
    looper_repeat_fill_index = 0;
    looper_read = 0.0f;
    looper_pending_length_op = LooperLengthOp::None;
    looper_length_ops_enabled = false;
    looper_switch_fade_remaining = 0;
    looper_stop_fade_remaining = 0;
    looper_quant_point_valid = false;
    looper_last_quant_write = 0;
    looper_last_quant_clock = looper_start_clock;

    if(QuantizeEnabled() && !MidiTransportBarActive())
        SetMetroAnchor(looper_start_clock, true);
}

void RequestLoopStart()
{
    if(!QuantizeEnabled())
    {
        StartLoopRecording();
    }
    else if(MidiTransportBarActive())
    {
        looper_start_clock = tempo::MidiClockCount();
        looper_state = LooperState::WaitingStart;
    }
    else if(MidiClockUsable())
    {
        looper_start_clock = tempo::MidiClockCount();
        looper_state = LooperState::WaitingStart;
    }
    else
    {
        StartLoopRecording();
    }
}

void SmoothLoopSeam(size_t length);
void FinishLoopRecording()
{
    if(looper_write < kLooperMinRecordSamples)
    {
        looper_state = LooperState::Empty;
        looper_write = 0;
        looper_length = 0;
        looper_repeat_source_length = 0;
        looper_repeat_fill_index = 0;
        looper_pending_length_op = LooperLengthOp::None;
        looper_length_ops_enabled = false;
        looper_quant_point_valid = false;
        return;
    }

    looper_length = looper_write;
    if(looper_length > MaxLooperSamples())
        looper_length = MaxLooperSamples();
    if((looper_length & 1) != 0)
        looper_length -= 1;
    looper_write = looper_length;
    SmoothLoopSeam(looper_length);
    BeginLoopLengthTransition();

    looper_read = 0.0f;
    looper_state = LooperState::Playing;
    looper_repeat_source_length = 0;
    looper_repeat_fill_index = 0;
    looper_pending_length_op = LooperLengthOp::None;
    looper_length_ops_enabled = false;
    looper_switch_fade_remaining = 0;
    looper_stop_fade_remaining = 0;
    looper_quant_point_valid = false;
}

void StopLoop()
{
    looper_switch_fade_remaining = 0;
    looper_stop_fade_remaining = 0;
    looper_state = LooperState::Empty;
    looper_write = 0;
    looper_length = 0;
    looper_repeat_source_length = 0;
    looper_repeat_fill_index = 0;
    looper_read = 0.0f;
    looper_pending_length_op = LooperLengthOp::None;
    looper_length_ops_enabled = false;
    looper_quant_point_valid = false;
}

void RequestStopLoop()
{
    if(looper_state == LooperState::Playing)
    {
        looper_stop_fade_remaining = kLooperStopFadeSamples;
        return;
    }

    StopLoop();
}

void SmoothLoopSeam(size_t length)
{
    const size_t fade_samples = std::min(kLooperSeamFadeSamples, length / 4);
    if(fade_samples < 2)
        return;

    for(size_t i = 0; i < fade_samples; ++i)
    {
        const size_t tail_index = length - fade_samples + i;
        const size_t head_index = i;
        const float x = static_cast<float>(i + 1) / static_cast<float>(fade_samples);
        const float smooth_x = x * x * (3.0f - 2.0f * x);
        if(dip3_mono)
        {
            const float tail = MonoBufferRead(tail_index);
            const float head = MonoBufferRead(head_index);
            MonoBufferWrite(tail_index, tail * (1.0f - smooth_x) + head * smooth_x);
        }
        else
        {
            loop_buffer_l[tail_index] = loop_buffer_l[tail_index] * (1.0f - smooth_x)
                                        + loop_buffer_l[head_index] * smooth_x;
            loop_buffer_r[tail_index] = loop_buffer_r[tail_index] * (1.0f - smooth_x)
                                        + loop_buffer_r[head_index] * smooth_x;
        }
    }
}

bool HalveLoopLength()
{
    if(looper_state != LooperState::Playing || looper_length < 64)
        return false;

    looper_length /= 2;
    looper_repeat_source_length = 0;
    looper_repeat_fill_index = 0;
    if(looper_length < 32)
        looper_length = 32;
    SmoothLoopSeam(looper_length);
    while(looper_read >= static_cast<float>(looper_length))
        looper_read -= static_cast<float>(looper_length);
    return true;
}

bool DoubleLoopLength()
{
    if(looper_state != LooperState::Playing || looper_length == 0)
        return false;

    const size_t old_length = looper_length;
    size_t new_length = looper_length * 2;
    if(new_length > MaxLooperSamples() || new_length == old_length)
    {
        TriggerLed2ErrorBlink();
        return false;
    }

    looper_repeat_source_length = old_length;
    looper_repeat_fill_index = 0;
    looper_length = new_length;
    return true;
}

void QueueLoopLengthOp(LooperLengthOp op)
{
    if(looper_state != LooperState::Playing || looper_length == 0 || !looper_length_ops_enabled)
        return;

    looper_pending_length_op = op;
    TriggerLed2Ack();
}

void ApplyPendingLoopLengthOp()
{
    if(looper_pending_length_op == LooperLengthOp::None)
        return;

    const LooperLengthOp op = looper_pending_length_op;
    looper_pending_length_op = LooperLengthOp::None;

    bool changed = false;
    if(op == LooperLengthOp::Halve)
        changed = HalveLoopLength();
    else if(op == LooperLengthOp::Double)
        changed = DoubleLoopLength();

    if(changed)
        BeginLoopLengthTransition();
}

size_t LooperPlaybackSourceLength()
{
    return looper_repeat_source_length > 0 ? looper_repeat_source_length : looper_length;
}

float ReadLinearWrapped(const float* buffer, float position, size_t source_length)
{
    if(source_length == 0)
        return 0.0f;

    while(position >= static_cast<float>(source_length))
        position -= static_cast<float>(source_length);

    const size_t index_a = static_cast<size_t>(position) % source_length;
    const size_t index_b = (index_a + 1) % source_length;
    const float frac = position - static_cast<float>(index_a);
    return buffer[index_a] + (buffer[index_b] - buffer[index_a]) * frac;
}

float ReadLinearWrappedMono(float position, size_t source_length)
{
    if(source_length == 0)
        return 0.0f;

    while(position >= static_cast<float>(source_length))
        position -= static_cast<float>(source_length);

    const size_t index_a = static_cast<size_t>(position) % source_length;
    const size_t index_b = (index_a + 1) % source_length;
    const float frac = position - static_cast<float>(index_a);
    const float a = MonoBufferRead(index_a);
    const float b = MonoBufferRead(index_b);
    return a + (b - a) * frac;
}

float ReadLoopSample(const float* buffer, float position, size_t source_length)
{
    if(source_length == 0)
        return 0.0f;

    while(position >= static_cast<float>(source_length))
        position -= static_cast<float>(source_length);

    const size_t fade_samples = std::min(kLooperSeamFadeSamples, source_length / 4);
    if(fade_samples < 2 || position < static_cast<float>(source_length - fade_samples))
        return ReadLinearWrapped(buffer, position, source_length);

    const float fade_pos = position - static_cast<float>(source_length - fade_samples);
    const float x = fclamp(fade_pos / static_cast<float>(fade_samples), 0.0f, 1.0f);
    const float smooth_x = x * x * (3.0f - 2.0f * x);
    const float tail = ReadLinearWrapped(buffer, position, source_length);
    const float head = ReadLinearWrapped(buffer, fade_pos, source_length);
    return tail * (1.0f - smooth_x) + head * smooth_x;
}

float ReadLoopSampleMono(float position, size_t source_length)
{
    if(source_length == 0)
        return 0.0f;

    while(position >= static_cast<float>(source_length))
        position -= static_cast<float>(source_length);

    const size_t fade_samples = std::min(kLooperSeamFadeSamples, source_length / 4);
    if(fade_samples < 2 || position < static_cast<float>(source_length - fade_samples))
        return ReadLinearWrappedMono(position, source_length);

    const float fade_pos = position - static_cast<float>(source_length - fade_samples);
    const float x = fclamp(fade_pos / static_cast<float>(fade_samples), 0.0f, 1.0f);
    const float smooth_x = x * x * (3.0f - 2.0f * x);
    const float tail = ReadLinearWrappedMono(position, source_length);
    const float head = ReadLinearWrappedMono(fade_pos, source_length);
    return tail * (1.0f - smooth_x) + head * smooth_x;
}

bool LooperReadIsOnSample()
{
    const size_t read_index = static_cast<size_t>(looper_read);
    const float frac = looper_read - static_cast<float>(read_index);
    return frac < 0.0001f || frac > 0.9999f;
}

void ApplyLoopLengthTransition(float& loop_l, float& loop_r)
{
    if(looper_transition_remaining == 0)
        return;

    const float progress = 1.0f
                           - (static_cast<float>(looper_transition_remaining)
                              / static_cast<float>(kLooperLengthTransitionSamples));
    const float smooth = progress * progress * (3.0f - 2.0f * progress);
    loop_l = looper_transition_start_l * (1.0f - smooth) + loop_l * smooth;
    loop_r = looper_transition_start_r * (1.0f - smooth) + loop_r * smooth;
    looper_transition_remaining -= 1;
}

bool RepeatFillActive()
{
    return looper_repeat_source_length > 0
           && looper_repeat_fill_index < looper_repeat_source_length
           && looper_length >= looper_repeat_source_length * 2;
}

void ProcessRepeatFill(float input_l, float input_r, float feedback)
{
    if(!RepeatFillActive())
        return;

    const size_t source_index = looper_repeat_fill_index;
    const size_t dest_index = looper_repeat_source_length + looper_repeat_fill_index;
    const float fb = fclamp(feedback, 0.0f, 1.0f);
    const float overdub_gain = overdub_enabled ? 1.0f : 0.0f;

    if(dip3_mono)
    {
        const float mono_input = (input_l + input_r) * 0.5f;
        const float source = MonoBufferRead(source_index);
        MonoBufferWrite(dest_index,
                        LooperSoftLimit((source * fb) + (mono_input * overdub_gain * kWritePad)));
    }
    else
    {
        loop_buffer_l[dest_index] = LooperSoftLimit((loop_buffer_l[source_index] * fb)
                                                    + (input_l * overdub_gain * kWritePad));
        loop_buffer_r[dest_index] = LooperSoftLimit((loop_buffer_r[source_index] * fb)
                                                    + (input_r * overdub_gain * kWritePad));
    }

    looper_repeat_fill_index += 1;
    if(looper_repeat_fill_index >= looper_repeat_source_length)
    {
        looper_repeat_source_length = 0;
        looper_repeat_fill_index = 0;
        SmoothLoopSeam(looper_length);
    }
}

void AdvanceLooperRead()
{
    looper_read += PlaybackStep();
    if(looper_read >= static_cast<float>(looper_length))
    {
        while(looper_read >= static_cast<float>(looper_length))
            looper_read -= static_cast<float>(looper_length);
        looper_length_ops_enabled = true;
        TriggerLed2LoopStartBlink();
        ApplyPendingLoopLengthOp();
    }
    else if(looper_read < 0.0f)
    {
        while(looper_read < 0.0f)
            looper_read += static_cast<float>(looper_length);
        looper_length_ops_enabled = true;
        TriggerLed2LoopStartBlink();
        ApplyPendingLoopLengthOp();
    }
}

void HandleLoopTapRelease(uint32_t held_ms, bool long_handled)
{
    if(held_ms >= kLooperLongPressMs || long_handled)
        return;

    if(looper_state == LooperState::Playing)
    {
        if(held_ms < kLooperDoublePressMs)
            QueueLoopLengthOp(LooperLengthOp::Halve);
        else
            QueueLoopLengthOp(LooperLengthOp::Double);
        return;
    }

    if(looper_state == LooperState::Empty)
    {
        RequestLoopStart();
    }
    else if(looper_state == LooperState::Recording)
    {
        if(QuantizeEnabled())
        {
            looper_end_clock = tempo::MidiClockCount();
            looper_state = LooperState::WaitingEnd;
        }
        else
        {
            FinishLoopRecording();
        }
    }
    else if(looper_state == LooperState::WaitingStart)
    {
        StartLoopRecording();
    }
    else if(looper_state == LooperState::WaitingEnd)
    {
        FinishLoopRecording();
    }
}

void ProcessExternalTapInput()
{
    const float external_tap_value = expression.Process();
    const bool pressed_now = external_tap_pressed
                                  ? external_tap_value < 0.55f
                                  : external_tap_value < 0.20f;
    const uint32_t now = System::GetNow();

    if(pressed_now && !external_tap_pressed)
    {
        external_tap_pressed = true;
        external_tap_down_ms = now;
        external_tap_long_handled = false;
        external_tap_started_loop_on_press = false;

        if(looper_state == LooperState::Empty)
        {
            RequestLoopStart();
            external_tap_started_loop_on_press = true;
        }
    }
    else if(pressed_now && !external_tap_long_handled)
    {
        const uint32_t held_ms = now - external_tap_down_ms;
        if(held_ms >= kLooperLongPressMs)
        {
            RequestStopLoop();
            external_tap_long_handled = true;
        }
    }
    else if(!pressed_now && external_tap_pressed)
    {
        external_tap_pressed = false;
        if(external_tap_started_loop_on_press)
            external_tap_started_loop_on_press = false;
        else
            HandleLoopTapRelease(now - external_tap_down_ms, external_tap_long_handled);
    }
}

void RememberQuantPointIfAllowed()
{
    if(!QuantizeEnabled())
        return;

    const uint32_t current_clock = tempo::MidiClockCount();
    if(current_clock == looper_last_quant_clock)
        return;

    const uint32_t elapsed_clocks = current_clock - looper_start_clock;
    if(IsAllowedQuantizedBarLength(elapsed_clocks))
    {
        looper_quant_point_valid = true;
        looper_last_quant_write = looper_write;
        looper_last_quant_clock = current_clock;
    }
}

bool QuantizeHasPassedLastAllowedLength()
{
    if(!QuantizeEnabled())
        return false;

    const uint32_t elapsed_clocks = tempo::MidiClockCount() - looper_start_clock;
    return elapsed_clocks > MaxAllowedQuantClocks();
}

void FinishAtLastQuantPointWithWarning()
{
    if(looper_quant_point_valid && looper_last_quant_write >= kLooperMinRecordSamples)
        looper_write = looper_last_quant_write;
    TriggerLed2ErrorBlink();
    FinishLoopRecording();
}

void ProcessLooperQuantize()
{
    if(looper_state == LooperState::WaitingStart)
    {
        if(!QuantizeEnabled())
            StartLoopRecording();
        else if(MidiTransportBarActive())
        {
            if(tempo::MidiClockCount() != looper_start_clock
               && TransportAtBar())
            {
                looper_start_clock = tempo::MidiClockCount();
                StartLoopRecording();
            }
        }
        else if(tempo::MidiClockCount() != looper_start_clock)
        {
            looper_start_clock = tempo::MidiClockCount();
            StartLoopRecording();
        }
    }
    else if(looper_state == LooperState::Recording)
    {
        RememberQuantPointIfAllowed();
    }
    else if(looper_state == LooperState::WaitingEnd)
    {
        if(!QuantizeEnabled())
            FinishLoopRecording();
        else if(QuantizeHasPassedLastAllowedLength())
        {
            FinishAtLastQuantPointWithWarning();
        }
        else if(tempo::MidiClockCount() != looper_end_clock)
        {
            RememberQuantPointIfAllowed();
            if(ClockAtAllowedQuantLength(looper_start_clock))
                FinishLoopRecording();
        }
    }
}

void ProcessLooperSample(float input_l,
                         float input_r,
                         float feedback,
                         float& loop_l,
                         float& loop_r)
{
    loop_l = 0.0f;
    loop_r = 0.0f;

    ProcessLooperQuantize();

    if(looper_state == LooperState::Recording || looper_state == LooperState::WaitingEnd)
    {
        if(looper_write >= MaxLooperSamples())
        {
            if(looper_state == LooperState::WaitingEnd && looper_quant_point_valid)
                FinishAtLastQuantPointWithWarning();
            else
                FinishLoopRecording();
            return;
        }

        if(dip3_mono)
        {
            const float mono_input = (input_l + input_r) * 0.5f;
            MonoBufferWrite(looper_write, LooperSoftLimit(mono_input * kWritePad));
        }
        else
        {
            loop_buffer_l[looper_write] = LooperSoftLimit(input_l * kWritePad);
            loop_buffer_r[looper_write] = LooperSoftLimit(input_r * kWritePad);
        }
        looper_write += 1;

        if(looper_write >= MaxLooperSamples())
        {
            if(looper_state == LooperState::WaitingEnd && looper_quant_point_valid)
                FinishAtLastQuantPointWithWarning();
            else
                FinishLoopRecording();
        }
        return;
    }

    if(looper_state != LooperState::Playing || looper_length == 0)
        return;

    UpdatePlaybackStepSlew();
    const size_t playback_source_length = LooperPlaybackSourceLength();
    const size_t write_index = static_cast<size_t>(looper_read) % looper_length;
    if(dip3_mono)
    {
        const float mono_loop = ReadLoopSampleMono(looper_read, playback_source_length);
        loop_l = mono_loop;
        loop_r = mono_loop;
    }
    else
    {
        loop_l = ReadLoopSample(loop_buffer_l, looper_read, playback_source_length);
        loop_r = ReadLoopSample(loop_buffer_r, looper_read, playback_source_length);
    }
    const float raw_loop_l = loop_l;
    const float raw_loop_r = loop_r;
    ApplyLoopLengthTransition(loop_l, loop_r);
    if(looper_switch_fade_remaining > 0)
    {
        const uint32_t elapsed = kLooperSwitchFadeSamples - looper_switch_fade_remaining;
        const float half = static_cast<float>(kLooperSwitchFadeSamples) * 0.5f;
        float fade = 1.0f;
        if(static_cast<float>(elapsed) < half)
            fade = 1.0f - (static_cast<float>(elapsed) / half);
        else
            fade = (static_cast<float>(elapsed) - half) / half;
        fade = fclamp(fade, 0.0f, 1.0f);
        const float smooth = fade * fade * (3.0f - 2.0f * fade);
        loop_l *= smooth;
        loop_r *= smooth;
        looper_switch_fade_remaining -= 1;
    }
    if(looper_stop_fade_remaining > 0)
    {
        const float fade = static_cast<float>(looper_stop_fade_remaining)
                           / static_cast<float>(kLooperStopFadeSamples);
        loop_l *= fade;
        loop_r *= fade;
        looper_stop_fade_remaining -= 1;
        if(looper_stop_fade_remaining == 0)
        {
            StopLoop();
            return;
        }
    }
    const bool repeat_fill_was_active = RepeatFillActive();
    ProcessRepeatFill(input_l, input_r, feedback);

    const bool should_write = !repeat_fill_was_active && (PlaybackStep() >= 1.0f || LooperReadIsOnSample());
    if(should_write)
    {
        const float fb = fclamp(feedback, 0.0f, 1.0f);
        const float overdub_gain = overdub_enabled ? 1.0f : 0.0f;
        if(dip3_mono)
        {
            const float mono_input = (input_l + input_r) * 0.5f;
            MonoBufferWrite(write_index,
                            LooperSoftLimit((raw_loop_l * fb)
                                            + (mono_input * overdub_gain * kWritePad)));
        }
        else
        {
            loop_buffer_l[write_index] = LooperSoftLimit((raw_loop_l * fb) + (input_l * overdub_gain * kWritePad));
            loop_buffer_r[write_index] = LooperSoftLimit((raw_loop_r * fb) + (input_r * overdub_gain * kWritePad));
        }
    }

    looper_last_output_l = loop_l;
    looper_last_output_r = loop_r;
    AdvanceLooperRead();
}

void UpdateSwitches()
{
    switch3_left = hw.switches[Funbox::SWITCH_3_LEFT].Pressed();
    switch3_right = hw.switches[Funbox::SWITCH_3_RIGHT].Pressed();
    switch2_left = hw.switches[Funbox::SWITCH_2_LEFT].Pressed();
    switch2_right = hw.switches[Funbox::SWITCH_2_RIGHT].Pressed();
    switch1_left = hw.switches[Funbox::SWITCH_1_LEFT].Pressed();
    switch1_right = hw.switches[Funbox::SWITCH_1_RIGHT].Pressed();
    SetPlaybackStepTarget(TargetPlaybackStepFromSwitch());
    const bool switch2_middle = !switch2_left && !switch2_right;
    if(switch2_was_middle && !switch2_middle && metro_anchor_valid && MidiClockUsable())
        SetMetroAnchor(tempo::MidiClockCount(), true);
    switch2_was_middle = switch2_middle;
    dip1_miso = hw.switches[Funbox::SWITCH_DIP_1].Pressed();
    dip2_ignore_midi = hw.switches[Funbox::SWITCH_DIP_2].Pressed();
    dip3_mono = hw.switches[Funbox::SWITCH_DIP_3].Pressed();

    if(dip2_ignore_midi)
    {
        midi_transport_running = false;
        ResetMetroAnchor();
    }
}

void UpdateControls()
{
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();

    UpdateSwitches();
    ProcessExternalTapInput();

    const bool fsw1_released = hw.switches[Funbox::FOOTSWITCH_1].FallingEdge();
    if(fsw1_released)
    {
        overdub_enabled = !overdub_enabled;
        TriggerLed2Ack();
    }

    const bool fsw2_pressed = hw.switches[Funbox::FOOTSWITCH_2].Pressed();
    const uint32_t now = System::GetNow();

    if(fsw2_pressed && !footswitch2_was_pressed)
    {
        footswitch2_down_ms = now;
        footswitch2_long_handled = false;
        footswitch2_started_loop_on_press = false;

        if(looper_state == LooperState::Empty)
        {
            RequestLoopStart();
            footswitch2_started_loop_on_press = true;
        }
    }
    else if(fsw2_pressed && !footswitch2_long_handled)
    {
        const uint32_t held_ms = now - footswitch2_down_ms;
        if(held_ms >= kLooperLongPressMs)
        {
            RequestStopLoop();
            footswitch2_long_handled = true;
        }
    }
    else if(!fsw2_pressed && footswitch2_was_pressed)
    {
        if(footswitch2_started_loop_on_press)
            footswitch2_started_loop_on_press = false;
        else
            HandleLoopTapRelease(now - footswitch2_down_ms, footswitch2_long_handled);
    }

    footswitch2_was_pressed = fsw2_pressed;
}

float Led2BaseBrightness()
{
    if(LoopIsActive())
        return 1.0f;
    if(looper_state == LooperState::Recording
       || looper_state == LooperState::WaitingStart
       || looper_state == LooperState::WaitingEnd)
        return 0.35f;
    return 0.0f;
}

void UpdateLeds()
{
    if(MidiClockUsable())
    {
        const float base = overdub_enabled ? 1.0f : 0.0f;
        const bool midi_pulse = System::GetNow() < midi_led_pulse_until_ms;
        led1.Set(midi_pulse ? (base > 0.0f ? 0.0f : 1.0f) : base);
    }
    else
    {
        led1.Set(overdub_enabled ? 1.0f : 0.0f);
    }

    if(looper_error_blink_ticks > 0)
    {
        const uint32_t elapsed = kLooperErrorBlinkTicks - looper_error_blink_ticks;
        const bool invert = ((elapsed / kLooperErrorBlinkHalfTicks) % 2) == 0;
        looper_error_blink_ticks -= 1;
        const float base = Led2BaseBrightness();
        led2.Set(invert ? (base > 0.0f ? 0.0f : 1.0f) : base);
    }
    else if(looper_loopstart_blink_ticks > 0)
    {
        looper_loopstart_blink_ticks -= 1;
        const float base = Led2BaseBrightness();
        led2.Set(base > 0.0f ? 0.0f : 1.0f);
    }
    else if(looper_led_ack_ticks > 0)
    {
        looper_led_ack_ticks -= 1;
        const float base = Led2BaseBrightness();
        led2.Set(base > 0.0f ? 0.0f : 1.0f);
    }
    else
    {
        led2.Set(Led2BaseBrightness());
    }
}

static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
{
    UpdateControls();

    const float gain = knob3_volume.Process();
    const float metro_volume = knob2_metro_volume.Process();
    const float feedback = knob6_feedback.Process();

    for(size_t i = 0; i < size; i++)
    {
        float input_l = in[0][i] * kInputPad;
        float input_r = (dip1_miso ? in[0][i] : in[1][i]) * kInputPad;
        if(dip3_mono)
        {
            const float mono_input = (input_l + input_r) * 0.5f;
            input_l = mono_input;
            input_r = mono_input;
        }

        float loop_l;
        float loop_r;
        ProcessLooperSample(input_l, input_r, feedback, loop_l, loop_r);

        const float metro = ProcessMetroSample(metro_volume);
        out[0][i] = LooperSoftLimit((input_l + loop_l * gain + metro) * kMonitorPad);
        out[1][i] = LooperSoftLimit((input_r + loop_r * gain + metro) * kMonitorPad);
    }

    UpdateLeds();
    led1.Update();
    led2.Update();
}

void HandleMidiMessage(MidiEvent m)
{
    if(dip2_ignore_midi)
        return;

    switch(m.type)
    {
        case SystemRealTime:
            if(m.srt_type == TimingClock)
            {
                tempo::HandleMidiClock();
                ProcessMetroClock();
                midi_clock_led_count += 1;
                if(midi_clock_led_count >= 24)
                {
                    midi_clock_led_count = 0;
                    midi_led_pulse_until_ms = System::GetNow() + kMidiLedPulseMs;
                }
            }
            else if(m.srt_type == Start)
            {
                midi_transport_running = true;
                midi_transport_start_clock = tempo::MidiClockCount();
                SetMetroAnchor(midi_transport_start_clock, true);
            }
            else if(m.srt_type == Stop)
            {
                midi_transport_running = false;
                ResetMetroAnchor();
            }
            break;
        default:
            break;
    }
}

int main(void)
{
    hw.Init();
    const float samplerate = hw.AudioSampleRate();
    hw.SetAudioBlockSize(48);

    knob2_metro_volume.Init(hw.knob[Funbox::KNOB_2], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    knob3_volume.Init(hw.knob[Funbox::KNOB_3], 0.0f, 2.0f, ::daisy::Parameter::LINEAR);
    knob6_feedback.Init(hw.knob[Funbox::KNOB_6], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    expression.Init(hw.expression, 0.0f, 1.0f, ::daisy::Parameter::LINEAR);

    tempo::Init(samplerate);
    tempo::SetSyncMode(tempo::SyncMode::Midi);

    led1.Init(hw.seed.GetPin(Funbox::LED_1), false);
    led2.Init(hw.seed.GetPin(Funbox::LED_2), false);
    led1.Set(0.0f);
    led2.Set(0.0f);
    led1.Update();
    led2.Update();

    hw.InitMidi();
    hw.midi.StartReceive();

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        hw.midi.Listen();
        while(hw.midi.HasEvents())
            HandleMidiMessage(hw.midi.PopEvent());
    }
}
