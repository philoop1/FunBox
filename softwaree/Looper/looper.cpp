// FunBox pure stereo looper with MIDI clock quantized start/end.

#include "daisy_petal.h"
#include "daisysp.h"
#include "funbox.h"
#include "looper_granular_player.h"
#include "tempo.h"

#include <algorithm>
#include <cmath>

using namespace daisy;
using namespace daisysp;
using namespace funbox;

DaisyPetal hw;
Led led1, led2;
CpuLoadMeter cpu_load_meter;

::daisy::Parameter knob1_grain_mix;
::daisy::Parameter knob3_volume;
::daisy::Parameter knob2_metro_volume;
::daisy::Parameter knob4_grain_size;
::daisy::Parameter knob5_grain_pitch;
::daisy::Parameter knob6_feedback;
::daisy::Parameter expression;

static constexpr float kInputPad = 0.80f;
static constexpr float kWritePad = 0.80f;
static constexpr float kMonitorPad = 0.90f;
static constexpr uint32_t kLooperClearHoldMs = 3000;
static constexpr uint32_t kLooperLongPressMs = 5000;
static constexpr uint32_t kExternalUndoHoldMs = 1500;
static constexpr uint32_t kShiftHoldMs = 600;
static constexpr uint32_t kLooperLengthPressMs = 250;
static constexpr uint32_t kExternalSwitchDebounceMs = 12;
static constexpr uint32_t kLooperLedAckTicks = 120;
static constexpr uint32_t kLooperLoopStartBlinkTicks = 12;
static constexpr uint32_t kLooperErrorBlinkHalfTicks = 12;
static constexpr uint32_t kLooperErrorBlinkTicks = kLooperErrorBlinkHalfTicks * 6;
static constexpr size_t kLooperSeamFadeSamples = 512;
static constexpr uint32_t kLooperLengthTransitionSamples = 1024;
static constexpr uint32_t kPlaybackStepSlewSamples = 512;
static constexpr uint32_t kLooperSwitchFadeSamples = 1024;
static constexpr uint32_t kLooperWrapFadeSamples = 512;
static constexpr uint32_t kLooperStopFadeSamples = 48000 * 2;
static constexpr uint32_t kLooperClearFadeSamples = 48000 * 2;
static constexpr size_t kLooperMinRecordSamples = 4800;
static constexpr size_t kLooperMaxSamples = 48000 * 120;
static constexpr size_t kUndoMaxSamples = 48000 * 28;
static constexpr size_t kGranularBufferSamples = 1;
static constexpr size_t kGranularPlayerSamples = kGranularBufferSamples;
static constexpr float kGranularLevel = 5.0f;
static constexpr float kFixedGrainWindowShape = 0.75f;
static constexpr float kPickupJumpThreshold = 0.006f;
static constexpr float kInternalTempoMinBpm = 30.0f;
static constexpr float kInternalTempoMaxBpm = 240.0f;
static constexpr uint32_t kMidiLedPulseMs = 35;
static constexpr uint32_t kMidiClocksPerBeat = 24;
static constexpr uint32_t kMidiClocksPerThreeFourBar = 72;
static constexpr uint32_t kMidiClocksPerFourFourBar = 96;
static constexpr uint32_t kMetroClickSamples = 1200;
static constexpr float kMetroNormalPitch = 1200.0f;
static constexpr float kMetroOnePitch = 1800.0f;
static constexpr float kMetroLoopStartPitch = 2800.0f;
static constexpr float kMetroLoopStartGain = 1.5f;
static constexpr uint32_t kOverdubFadeSamples = 960;
static constexpr uint32_t kExternalComboClearHoldMs = 100;
static constexpr uint32_t kExternalComboResetHoldMs = 600;
static constexpr uint32_t kSerialPrintIntervalMs = 1000;
static constexpr uint8_t kExternalFsw1Bit = 0x01;
static constexpr uint8_t kExternalFsw2Bit = 0x02;

DSY_SDRAM_BSS float loop_buffer_l[kLooperMaxSamples];
DSY_SDRAM_BSS float loop_buffer_r[kLooperMaxSamples];
DSY_SDRAM_BSS float undo_buffer_l[kUndoMaxSamples];
DSY_SDRAM_BSS float undo_buffer_r[kUndoMaxSamples];
DSY_SDRAM_BSS uint8_t undo_written_flags[kUndoMaxSamples];
DSY_SDRAM_BSS float granular_buffer[kGranularBufferSamples];
LooperGranularPlayer granular_player;
size_t granular_write = 0;
float grain_mix = 0.0f;
float grain_size_ms = 110.0f;
float grain_density = 0.0f;
float grain_window_shape = 1.0f;
float grain_buffer_age = 0.0f;
bool grain_age_continuous = false;
float grain_position_random = 0.0f;
float grain_size_random = 0.0f;
float grain_speed = 1.0f;
float grain_speed_knob_value = 0.5f;
float grain_random_amount = 0.0f;
float grain_pitch_cents = 0.0f;
float grain_pitch_knob_value = 0.5f;
float grain_pitch_random_cents = 0.0f;
float internal_tempo_bpm = 120.0f;
float audio_sample_rate = 48000.0f;
size_t granular_loop_source_length = 0;
uint32_t last_serial_print_ms = 0;

struct PickupParam
{
    float value;
    float last_knob;
    bool locked;
    bool touched;
};

PickupParam k1_mix_pickup = {0.0f, 0.0f, false, false};
PickupParam k1_age_pickup = {0.0f, 0.0f, false, false};
PickupParam k2_speed_pickup = {0.5f, 0.5f, false, false};
PickupParam k2_random_pickup = {0.0f, 0.0f, false, false};
PickupParam k4_size_pickup = {0.18f, 0.18f, false, false};
PickupParam k4_density_pickup = {0.0f, 0.0f, false, false};
PickupParam k5_pitch_pickup = {0.5f, 0.5f, false, false};
PickupParam k5_pitch_random_pickup = {0.0f, 0.0f, false, false};

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
uint32_t looper_wrap_fade_remaining = 0;
uint32_t looper_stop_fade_remaining = 0;
uint32_t looper_clear_fade_remaining = 0;
bool looper_clear_keep_window_pending = false;
size_t looper_clear_samples_remaining = 0;
bool looper_quant_point_valid = false;
size_t looper_last_quant_write = 0;
uint32_t looper_last_quant_clock = 0;
bool undo_pass_valid = false;
size_t undo_pass_length = 0;
bool undo_pass_mono = false;
bool undo_pass_flags_cleared = true;
uint8_t undo_pass_epoch = 1;

bool overdub_enabled = false;
float overdub_gain_current = 0.0f;
float overdub_gain_target = 0.0f;
float overdub_gain_step = 0.0f;
uint32_t overdub_gain_slew_remaining = 0;
bool shift_enabled = false;
bool shift_pickup_refresh_pending = false;
bool switch2_pickup_refresh_pending = false;
bool switch2_internal_clock_start_pending = false;
uint32_t footswitch1_down_ms = 0;
bool footswitch1_was_pressed = false;
bool footswitch1_shift_handled = false;
uint32_t footswitch2_down_ms = 0;
bool footswitch2_was_pressed = false;
bool footswitch2_long_handled = false;
bool footswitch2_clear_handled = false;
bool footswitch2_started_loop_on_press = false;
uint8_t footswitch2_click_count = 0;
uint32_t footswitch2_last_click_ms = 0;

uint8_t external_switch_raw_bits = 0;
uint8_t external_switch_stable_bits = 0;
uint32_t external_switch_change_ms = 0;
float external_switch_adc_value = 1.0f;
float external_switch_adc_min = 1.0f;
float external_switch_adc_max = 0.0f;
uint8_t external_switch_reported_bits = 0xff;
uint32_t external_fsw1_down_ms = 0;
bool external_fsw1_was_pressed = false;
bool external_fsw1_long_handled = false;
uint32_t external_fsw2_down_ms = 0;
bool external_fsw2_was_pressed = false;
bool external_fsw2_long_handled = false;
bool external_fsw2_clear_handled = false;
bool external_fsw2_started_loop_on_press = false;
bool external_combo_was_pressed = false;
bool external_combo_clear_handled = false;
bool external_combo_block_individuals = false;
bool external_combo_reset_handled = false;
uint32_t external_combo_down_ms = 0;

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
bool internal_clock_running = false;
uint32_t internal_clock_count = 0;
float internal_clock_phase = 0.0f;

bool metro_anchor_valid = false;
uint32_t metro_anchor_clock = 0;
uint32_t metro_last_beat_index = 0xffffffff;
uint32_t metro_click_remaining = 0;
float metro_click_phase = 0.0f;
float metro_click_phase_inc = 0.0f;
float metro_click_gain = 1.0f;

float LooperSoftLimit(float value)
{
    const float x = fclamp(value, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

bool LoopIsActive()
{
    return looper_state == LooperState::Playing;
}

bool GrainModeActive()
{
    return switch1_right && !switch1_left;
}

float ApplyCenterDeadzone(float value, float deadzone)
{
    if(value > -deadzone && value < deadzone)
        return 0.0f;

    if(value > 0.0f)
        return (value - deadzone) / (1.0f - deadzone);

    return (value + deadzone) / (1.0f - deadzone);
}

float SnapTop(float value, float threshold)
{
    return value >= threshold ? 1.0f : value;
}

void TriggerLed2Ack();
int MetroBeatsPerBar();

bool CrossedPickupValue(float previous, float current, float target)
{
    return (previous <= target && current >= target)
           || (previous >= target && current <= target);
}

void LockActivePickupParams(float knob1, float knob2, float knob4, float knob5)
{
    PickupParam& k1_param = shift_enabled ? k1_age_pickup : k1_mix_pickup;
    PickupParam& k2_param = shift_enabled ? k2_random_pickup : k2_speed_pickup;
    PickupParam& k4_param = shift_enabled ? k4_density_pickup : k4_size_pickup;
    PickupParam& k5_param = shift_enabled ? k5_pitch_random_pickup : k5_pitch_pickup;

    k1_param.locked = true;
    k1_param.last_knob = knob1;
    k2_param.locked = true;
    k2_param.last_knob = knob2;
    k4_param.locked = true;
    k4_param.last_knob = knob4;
    k5_param.locked = true;
    k5_param.last_knob = knob5;
}

void LockK2K4K5PickupParams(float knob2, float knob4, float knob5)
{
    PickupParam& k2_param = shift_enabled ? k2_random_pickup : k2_speed_pickup;
    PickupParam& k4_param = shift_enabled ? k4_density_pickup : k4_size_pickup;
    PickupParam& k5_param = shift_enabled ? k5_pitch_random_pickup : k5_pitch_pickup;

    k2_param.locked = true;
    k2_param.last_knob = knob2;
    k4_param.locked = true;
    k4_param.last_knob = knob4;
    k5_param.locked = true;
    k5_param.last_knob = knob5;
}

void ProcessPickupParam(PickupParam& param, float knob)
{
    if(param.locked)
    {
        if(fabsf(knob - param.last_knob) > kPickupJumpThreshold
           || fabsf(knob - param.value) < 0.015f
           || CrossedPickupValue(param.last_knob, knob, param.value))
        {
            param.locked = false;
            param.value = knob;
            param.touched = true;
            TriggerLed2Ack();
        }
    }
    else
    {
        param.value = knob;
        param.touched = true;
    }

    param.last_knob = knob;
}

int LoadPercent(float load)
{
    if(load != load)
        return 0;
    return static_cast<int>(fclamp(load, 0.0f, 9.99f) * 100.0f + 0.5f);
}

void PrintSerialStatus()
{
    const uint32_t now = System::GetNow();
    if(now - last_serial_print_ms < kSerialPrintIntervalMs)
        return;

    last_serial_print_ms = now;
    DaisySeed::PrintLine(
        "cpu:%d/%d/%d sh:%d k1szrnd:%d k2dir:%d k4space:%d age:%d pos:%d k5pr:%d lock:%d%d%d%d",
        LoadPercent(cpu_load_meter.GetAvgCpuLoad()),
        LoadPercent(cpu_load_meter.GetMinCpuLoad()),
        LoadPercent(cpu_load_meter.GetMaxCpuLoad()),
        shift_enabled ? 1 : 0,
        static_cast<int>(grain_size_random * 100.0f + 0.5f),
        static_cast<int>(grain_random_amount * 100.0f + 0.5f),
        static_cast<int>(grain_density * 100.0f + 0.5f),
        static_cast<int>(grain_buffer_age * 100.0f + 0.5f),
        static_cast<int>(grain_position_random * 100.0f + 0.5f),
        static_cast<int>(grain_pitch_random_cents + 0.5f),
        k1_age_pickup.locked ? 1 : 0,
        k2_random_pickup.locked ? 1 : 0,
        k4_density_pickup.locked ? 1 : 0,
        k5_pitch_random_pickup.locked ? 1 : 0);
    external_switch_adc_min = external_switch_adc_value;
    external_switch_adc_max = external_switch_adc_value;
    cpu_load_meter.Reset();
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
void ResetGranularBuffer();
void SmoothLoopSeam(size_t length);
void SetOverdubEnabled(bool enabled);

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

bool MeterModeActive()
{
    return MetroBeatsPerBar() > 0;
}

bool ExternalQuantClockActive()
{
    return MidiClockUsable();
}

uint32_t QuantClockCount()
{
    return ExternalQuantClockActive() ? tempo::MidiClockCount() : internal_clock_count;
}

bool QuantizeStartToExternalBar()
{
    return MeterModeActive() && MidiTransportBarActive();
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
    return IsAllowedQuantizedBarLength(QuantClockCount() - anchor_clock);
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
    return MeterModeActive() || MidiClockUsable();
}

uint32_t MaxAllowedQuantClocks()
{
    return CurrentBarClockLength() * 64;
}

bool TransportAtBar()
{
    if(!QuantizeStartToExternalBar())
        return false;

    const uint32_t elapsed_clocks = tempo::MidiClockCount() - midi_transport_start_clock;
    const uint32_t bar_clocks = CurrentBarClockLength();
    return elapsed_clocks > 0 && (elapsed_clocks % bar_clocks) == 0;
}

bool CurrentMetroClockIsBarOne()
{
    if(MetroBeatsPerBar() == 0 || !metro_anchor_valid)
        return false;

    const uint32_t elapsed_clocks = QuantClockCount() - metro_anchor_clock;
    const uint32_t bar_clocks = CurrentBarClockLength();
    return bar_clocks > 0 && (elapsed_clocks % bar_clocks) == 0;
}

void TriggerMetroClick(bool bar_one, bool loop_start)
{
    const bool loop_start_one = bar_one && loop_start;
    const float pitch = loop_start_one ? kMetroLoopStartPitch
                                       : (bar_one ? kMetroOnePitch : kMetroNormalPitch);
    metro_click_phase_inc = TWOPI_F * pitch / hw.AudioSampleRate();
    if(loop_start_one)
        metro_click_gain = kMetroLoopStartGain;
    if(metro_click_remaining > 0)
        return;

    metro_click_remaining = kMetroClickSamples;
    metro_click_phase = 0.0f;
    metro_click_gain = loop_start_one ? kMetroLoopStartGain : 1.0f;
}

void TriggerMetroClick(bool bar_one)
{
    TriggerMetroClick(bar_one, false);
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

void ProcessMetroClock(uint32_t clock_count);

void SetInternalClockAnchor(bool trigger_now)
{
    internal_clock_running = MeterModeActive();
    internal_clock_count = 0;
    internal_clock_phase = 0.0f;
    SetMetroAnchor(internal_clock_count, trigger_now);
}

void ProcessInternalClockSample()
{
    if(!MeterModeActive() || ExternalQuantClockActive() || !internal_clock_running)
        return;

    const float tick_hz = (internal_tempo_bpm / 60.0f)
                          * static_cast<float>(kMidiClocksPerBeat);
    internal_clock_phase += tick_hz / audio_sample_rate;
    while(internal_clock_phase >= 1.0f)
    {
        internal_clock_phase -= 1.0f;
        internal_clock_count += 1;
        ProcessMetroClock(internal_clock_count);
    }
}

void ProcessMetroClock(uint32_t clock_count)
{
    const int beats_per_bar = MetroBeatsPerBar();
    if(beats_per_bar == 0 || !metro_anchor_valid)
        return;

    const uint32_t elapsed_clocks = clock_count - metro_anchor_clock;
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
    const float sample = LooperSoftLimit(sinf(metro_click_phase) * env * volume * metro_click_gain);
    metro_click_phase += metro_click_phase_inc;
    if(metro_click_phase >= TWOPI_F)
        metro_click_phase -= TWOPI_F;
    metro_click_remaining -= 1;
    if(metro_click_remaining == 0)
        metro_click_gain = 1.0f;
    return sample;
}

void TriggerLed2Ack()
{
    looper_led_ack_ticks = kLooperLedAckTicks;
}

void TriggerLed2LoopStartBlink()
{
    looper_loopstart_blink_ticks = kLooperLoopStartBlinkTicks;
    if(CurrentMetroClockIsBarOne())
        TriggerMetroClick(true, true);
}

void TriggerLed2ErrorBlink()
{
    looper_error_blink_ticks = kLooperErrorBlinkTicks;
}

void InvalidateUndoSnapshot()
{
    undo_pass_valid = false;
    undo_pass_length = 0;
    undo_pass_flags_cleared = true;
}

void PrepareUndoPass()
{
    if(looper_state != LooperState::Playing
       || looper_length == 0
       || looper_length > kUndoMaxSamples)
    {
        InvalidateUndoSnapshot();
        TriggerLed2ErrorBlink();
        return;
    }

    undo_pass_valid = true;
    undo_pass_length = looper_length;
    undo_pass_mono = dip3_mono;
    undo_pass_flags_cleared = false;
    undo_pass_epoch += 1;
    if(undo_pass_epoch == 0)
    {
        for(size_t i = 0; i < kUndoMaxSamples; ++i)
            undo_written_flags[i] = 0;
        undo_pass_epoch = 1;
    }
}

void RestoreUndoSnapshot()
{
    if(!undo_pass_valid
       || looper_state != LooperState::Playing
       || looper_length != undo_pass_length
       || dip3_mono != undo_pass_mono)
    {
        TriggerLed2ErrorBlink();
        return;
    }

    if(dip3_mono)
    {
        for(size_t i = 0; i < undo_pass_length; ++i)
        {
            if(undo_written_flags[i] == undo_pass_epoch)
                MonoBufferWrite(i, undo_buffer_l[i]);
        }
    }
    else
    {
        for(size_t i = 0; i < undo_pass_length; ++i)
        {
            if(undo_written_flags[i] == undo_pass_epoch)
            {
                loop_buffer_l[i] = undo_buffer_l[i];
                loop_buffer_r[i] = undo_buffer_r[i];
            }
        }
    }

    SmoothLoopSeam(looper_length);
    BeginLoopLengthTransition();
    ResetGranularBuffer();
    PrepareUndoPass();
    SetOverdubEnabled(true);
    TriggerLed2Ack();
}

void SetOverdubEnabled(bool enabled)
{
    overdub_enabled = enabled;
    overdub_gain_target = enabled ? 1.0f : 0.0f;
    overdub_gain_slew_remaining = kOverdubFadeSamples;
    overdub_gain_step = (overdub_gain_target - overdub_gain_current)
                        / static_cast<float>(kOverdubFadeSamples);
}

void ToggleOverdub()
{
    if(!overdub_enabled && looper_state == LooperState::Playing)
        PrepareUndoPass();
    else if(!overdub_enabled)
        InvalidateUndoSnapshot();
    SetOverdubEnabled(!overdub_enabled);
    TriggerLed2Ack();
}

void UpdateOverdubGainSlew()
{
    if(overdub_gain_slew_remaining == 0)
    {
        overdub_gain_current = overdub_gain_target;
        return;
    }

    overdub_gain_current += overdub_gain_step;
    overdub_gain_slew_remaining -= 1;
    if(overdub_gain_slew_remaining == 0)
    {
        overdub_gain_current = overdub_gain_target;
        overdub_gain_step = 0.0f;
    }
}

void BeginLoopLengthTransition()
{
    looper_transition_start_l = looper_last_output_l;
    looper_transition_start_r = looper_last_output_r;
    looper_transition_remaining = kLooperLengthTransitionSamples;
}

void StartLoopRecording()
{
    InvalidateUndoSnapshot();
    looper_state = LooperState::Recording;
    looper_write = 0;
    looper_length = 0;
    looper_repeat_source_length = 0;
    looper_repeat_fill_index = 0;
    looper_read = 0.0f;
    looper_pending_length_op = LooperLengthOp::None;
    looper_length_ops_enabled = false;
    looper_switch_fade_remaining = 0;
    looper_wrap_fade_remaining = 0;
    looper_stop_fade_remaining = 0;
    looper_clear_fade_remaining = 0;
    looper_clear_keep_window_pending = false;
    looper_clear_samples_remaining = 0;
    looper_quant_point_valid = false;
    looper_last_quant_write = 0;
    looper_last_quant_clock = looper_start_clock;
    ResetGranularBuffer();

    if(QuantizeEnabled() && !ExternalQuantClockActive() && !metro_anchor_valid)
        SetMetroAnchor(looper_start_clock, true);
}

void RequestLoopStart()
{
    if(!QuantizeEnabled())
    {
        StartLoopRecording();
    }
    else if(QuantizeStartToExternalBar())
    {
        looper_start_clock = QuantClockCount();
        looper_state = LooperState::WaitingStart;
    }
    else if(ExternalQuantClockActive())
    {
        looper_start_clock = QuantClockCount();
        if(MeterModeActive())
            SetMetroAnchor(looper_start_clock, true);
        StartLoopRecording();
    }
    else
    {
        SetInternalClockAnchor(true);
        looper_start_clock = QuantClockCount();
        StartLoopRecording();
    }
}

void SmoothLoopSeam(size_t length);
void FinishLoopRecording()
{
    InvalidateUndoSnapshot();
    if(looper_write < kLooperMinRecordSamples)
    {
        looper_state = LooperState::Empty;
        looper_write = 0;
        looper_length = 0;
        looper_repeat_source_length = 0;
        looper_repeat_fill_index = 0;
        looper_pending_length_op = LooperLengthOp::None;
        looper_length_ops_enabled = false;
        looper_clear_fade_remaining = 0;
        looper_clear_keep_window_pending = false;
        looper_clear_samples_remaining = 0;
        looper_quant_point_valid = false;
        ResetGranularBuffer();
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
    looper_wrap_fade_remaining = 0;
    looper_stop_fade_remaining = 0;
    looper_clear_fade_remaining = 0;
    looper_clear_keep_window_pending = false;
    looper_clear_samples_remaining = 0;
    looper_quant_point_valid = false;
    ResetGranularBuffer();
}

void StopLoop()
{
    InvalidateUndoSnapshot();
    looper_switch_fade_remaining = 0;
    looper_wrap_fade_remaining = 0;
    looper_stop_fade_remaining = 0;
    looper_clear_fade_remaining = 0;
    looper_clear_keep_window_pending = false;
    looper_clear_samples_remaining = 0;
    looper_state = LooperState::Empty;
    looper_write = 0;
    looper_length = 0;
    looper_repeat_source_length = 0;
    looper_repeat_fill_index = 0;
    looper_read = 0.0f;
    looper_pending_length_op = LooperLengthOp::None;
    looper_length_ops_enabled = false;
    looper_quant_point_valid = false;
    ResetGranularBuffer();
}

void RequestStopLoop()
{
    footswitch2_click_count = 0;
    looper_clear_fade_remaining = 0;
    looper_clear_keep_window_pending = false;
    looper_clear_samples_remaining = 0;
    if(looper_state == LooperState::Playing)
    {
        looper_stop_fade_remaining = kLooperStopFadeSamples;
        return;
    }

    StopLoop();
}

void ResetLoopImmediate()
{
    footswitch2_click_count = 0;
    looper_led_ack_ticks = 0;
    looper_loopstart_blink_ticks = 0;
    looper_error_blink_ticks = 0;
    StopLoop();
    led2.Set(0.0f);
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

    InvalidateUndoSnapshot();
    looper_length /= 2;
    looper_repeat_source_length = 0;
    looper_repeat_fill_index = 0;
    if(looper_length < 32)
        looper_length = 32;
    SmoothLoopSeam(looper_length);
    while(looper_read >= static_cast<float>(looper_length))
        looper_read -= static_cast<float>(looper_length);
    ResetGranularBuffer();
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

    InvalidateUndoSnapshot();
    looper_repeat_source_length = old_length;
    looper_repeat_fill_index = 0;
    looper_length = new_length;
    ResetGranularBuffer();
    return true;
}

void QueueLoopLengthOp(LooperLengthOp op)
{
    if(looper_state != LooperState::Playing || looper_length == 0)
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

void ClearLoopContentKeepWindow()
{
    if(looper_length == 0)
        return;

    InvalidateUndoSnapshot();
    const size_t length = std::min(looper_length, MaxLooperSamples());
    if(dip3_mono)
    {
        for(size_t i = 0; i < length; ++i)
            MonoBufferWrite(i, 0.0f);
    }
    else
    {
        for(size_t i = 0; i < length; ++i)
        {
            loop_buffer_l[i] = 0.0f;
            loop_buffer_r[i] = 0.0f;
        }
    }

    looper_repeat_source_length = 0;
    looper_repeat_fill_index = 0;
    looper_pending_length_op = LooperLengthOp::None;
    looper_clear_fade_remaining = 0;
    looper_clear_keep_window_pending = false;
    ResetGranularBuffer();
}

void RequestClearLoopContentKeepWindow()
{
    if(looper_state != LooperState::Playing || looper_length == 0)
        return;

    footswitch2_click_count = 0;
    looper_pending_length_op = LooperLengthOp::None;
    InvalidateUndoSnapshot();
    looper_clear_keep_window_pending = true;
    looper_clear_fade_remaining = 0;
    looper_clear_samples_remaining = looper_length;
    TriggerLed2ErrorBlink();
}

void RegisterFootswitch2LoopClick(uint32_t now)
{
    if(looper_state != LooperState::Playing)
        return;

    if(now - footswitch2_last_click_ms > kLooperLengthPressMs)
        footswitch2_click_count = 0;

    if(footswitch2_click_count < 2)
        footswitch2_click_count += 1;
    footswitch2_last_click_ms = now;
    TriggerLed2Ack();
}

void ProcessFootswitch2ClickQueue(uint32_t now)
{
    if(footswitch2_click_count == 0)
        return;
    if(now - footswitch2_last_click_ms < kLooperLengthPressMs)
        return;

    const uint8_t clicks = footswitch2_click_count;
    footswitch2_click_count = 0;

    if(looper_state != LooperState::Playing)
        return;

    if(clicks == 1)
        QueueLoopLengthOp(LooperLengthOp::Halve);
    else if(clicks >= 2)
        QueueLoopLengthOp(LooperLengthOp::Double);
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

void ReadLoopStereoAt(float position, size_t source_length, float& out_l, float& out_r)
{
    if(dip3_mono)
    {
        const float mono = ReadLoopSampleMono(position, source_length);
        out_l = mono;
        out_r = mono;
    }
    else
    {
        out_l = ReadLoopSample(loop_buffer_l, position, source_length);
        out_r = ReadLoopSample(loop_buffer_r, position, source_length);
    }
}

void ResetGranularBuffer()
{
    for(size_t i = 0; i < kGranularBufferSamples; ++i)
        granular_buffer[i] = 0.0f;
    granular_write = 0;
}

void FeedGranularBuffer(float sample)
{
    granular_buffer[granular_write] = LooperSoftLimit(sample * kWritePad);
    granular_write += 1;
    if(granular_write >= kGranularBufferSamples)
        granular_write = 0;
}

float ReadGranularLoopMono(float position)
{
    if(granular_loop_source_length == 0)
        return 0.0f;
    return ReadLoopSampleMono(position, granular_loop_source_length);
}

void ReadGranularLoopStereo(float position, float& out_l, float& out_r)
{
    if(granular_loop_source_length == 0)
    {
        out_l = 0.0f;
        out_r = 0.0f;
        return;
    }
    ReadLoopStereoAt(position, granular_loop_source_length, out_l, out_r);
}

void ProcessGranularPlayer(float& out_l, float& out_r)
{
    granular_loop_source_length = LooperPlaybackSourceLength();
    float grain_l;
    float grain_r;
    granular_player.ProcessFromStereoReader(looper_read,
                                            static_cast<int>(granular_loop_source_length),
                                            ReadGranularLoopStereo,
                                            grain_speed,
                                            grain_pitch_cents,
                                            grain_size_ms,
                                            grain_random_amount,
                                            grain_density,
                                            grain_window_shape,
                                            grain_buffer_age,
                                            grain_age_continuous,
                                            grain_position_random,
                                            grain_size_random,
                                            grain_pitch_random_cents,
                                            grain_l,
                                            grain_r);
    out_l = LooperSoftLimit(grain_l * kGranularLevel);
    out_r = LooperSoftLimit(grain_r * kGranularLevel);
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

void BackupUndoSample(size_t index, float old_l, float old_r)
{
    if(!undo_pass_valid
       || overdub_gain_current <= 0.0001f
       || looper_length != undo_pass_length
       || dip3_mono != undo_pass_mono
       || index >= kUndoMaxSamples
       || undo_written_flags[index] == undo_pass_epoch)
        return;

    undo_buffer_l[index] = old_l;
    if(!dip3_mono)
        undo_buffer_r[index] = old_r;
    undo_written_flags[index] = undo_pass_epoch;
}

void ProcessRepeatFill(float input_l, float input_r, float feedback)
{
    if(!RepeatFillActive())
        return;

    const size_t source_index = looper_repeat_fill_index;
    const size_t dest_index = looper_repeat_source_length + looper_repeat_fill_index;
    const float fb = fclamp(feedback, 0.0f, 1.0f);
    const float overdub_gain = overdub_gain_current;

    if(dip3_mono)
    {
        const float mono_input = (input_l + input_r) * 0.5f;
        const float source = MonoBufferRead(source_index);
        BackupUndoSample(dest_index, MonoBufferRead(dest_index), 0.0f);
        MonoBufferWrite(dest_index,
                        LooperSoftLimit((source * fb) + (mono_input * overdub_gain * kWritePad)));
    }
    else
    {
        BackupUndoSample(dest_index, loop_buffer_l[dest_index], loop_buffer_r[dest_index]);
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
        looper_wrap_fade_remaining = kLooperWrapFadeSamples;
        TriggerLed2LoopStartBlink();
        ApplyPendingLoopLengthOp();
    }
    else if(looper_read < 0.0f)
    {
        while(looper_read < 0.0f)
            looper_read += static_cast<float>(looper_length);
        looper_length_ops_enabled = true;
        looper_wrap_fade_remaining = kLooperWrapFadeSamples;
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
        if(held_ms >= kLooperClearHoldMs)
        {
            footswitch2_click_count = 0;
            RequestClearLoopContentKeepWindow();
            return;
        }
        if(held_ms < kLooperLengthPressMs)
        {
            QueueLoopLengthOp(LooperLengthOp::Halve);
        }
        else
        {
            footswitch2_click_count = 0;
            QueueLoopLengthOp(LooperLengthOp::Double);
        }
        return;
    }

    (void)held_ms;
}

void ProcessLooperFootswitch2Input(bool pressed,
                                   uint32_t now,
                                   uint32_t& down_ms,
                                   bool& was_pressed,
                                   bool& long_handled,
                                   bool& clear_handled,
                                   bool& started_loop_on_press)
{
    if(pressed && !was_pressed)
    {
        down_ms = now;
        long_handled = false;
        clear_handled = false;
        started_loop_on_press = false;

        if(looper_state == LooperState::Empty)
        {
            RequestLoopStart();
            started_loop_on_press = true;
        }
        else if(looper_state == LooperState::Recording)
        {
            if(QuantizeEnabled())
            {
                looper_end_clock = QuantClockCount();
                looper_state = LooperState::WaitingEnd;
            }
            else
            {
                FinishLoopRecording();
            }
            started_loop_on_press = true;
        }
        else if(looper_state == LooperState::WaitingStart)
        {
            StartLoopRecording();
            started_loop_on_press = true;
        }
        else if(looper_state == LooperState::WaitingEnd)
        {
            FinishLoopRecording();
            started_loop_on_press = true;
        }
    }
    else if(pressed && !long_handled)
    {
        const uint32_t held_ms = now - down_ms;
        if(!clear_handled
           && held_ms >= kLooperClearHoldMs
           && looper_state == LooperState::Playing)
        {
            footswitch2_click_count = 0;
            RequestClearLoopContentKeepWindow();
            clear_handled = true;
            DaisySeed::PrintLine("fsw2-clear");
        }
        if(held_ms >= kLooperLongPressMs)
        {
            ResetLoopImmediate();
            long_handled = true;
            DaisySeed::PrintLine("fsw2-reset");
        }
    }
    else if(!pressed && was_pressed)
    {
        if(started_loop_on_press)
            started_loop_on_press = false;
        else if(clear_handled)
            clear_handled = false;
        else
            HandleLoopTapRelease(now - down_ms, long_handled);
    }

    was_pressed = pressed;
}

uint8_t DecodeExternalSwitchBits(float value)
{
    if(value > 0.82f)
        return 0;
    if(value > 0.59f)
        return kExternalFsw2Bit;
    if(value > 0.45f)
        return kExternalFsw1Bit;
    return kExternalFsw1Bit | kExternalFsw2Bit;
}

void UpdateExternalSwitchBits(uint32_t now)
{
    external_switch_adc_value = expression.Process();
    external_switch_adc_min = std::min(external_switch_adc_min, external_switch_adc_value);
    external_switch_adc_max = std::max(external_switch_adc_max, external_switch_adc_value);
    const uint8_t decoded = DecodeExternalSwitchBits(external_switch_adc_value);
    if(decoded != external_switch_raw_bits)
    {
        external_switch_raw_bits = decoded;
        external_switch_change_ms = now;
    }

    if(now - external_switch_change_ms >= kExternalSwitchDebounceMs)
        external_switch_stable_bits = external_switch_raw_bits;

    if(external_switch_stable_bits != external_switch_reported_bits)
    {
        external_switch_reported_bits = external_switch_stable_bits;
        DaisySeed::PrintLine("ext-event adc:%d raw:%d st:%d e:%d%d",
                             static_cast<int>(external_switch_adc_value * 100.0f + 0.5f),
                             static_cast<int>(external_switch_raw_bits),
                             static_cast<int>(external_switch_stable_bits),
                             (external_switch_stable_bits & kExternalFsw1Bit) != 0 ? 1 : 0,
                             (external_switch_stable_bits & kExternalFsw2Bit) != 0 ? 1 : 0);
    }
}

void ProcessExternalOverdubUndoInput(bool pressed, uint32_t now)
{
    if(pressed && !external_fsw1_was_pressed)
    {
        external_fsw1_down_ms = now;
        external_fsw1_long_handled = false;
    }
    else if(pressed && !external_fsw1_long_handled)
    {
        const uint32_t held_ms = now - external_fsw1_down_ms;
        if(held_ms >= kExternalUndoHoldMs)
        {
            RestoreUndoSnapshot();
            external_fsw1_long_handled = true;
        }
    }
    else if(!pressed && external_fsw1_was_pressed)
    {
        if(!external_fsw1_long_handled)
            ToggleOverdub();
    }

    external_fsw1_was_pressed = pressed;
}

void ResetExternalIndividualInputs()
{
    external_fsw1_was_pressed = false;
    external_fsw1_long_handled = false;
    external_fsw2_was_pressed = false;
    external_fsw2_long_handled = false;
    external_fsw2_clear_handled = false;
    external_fsw2_started_loop_on_press = false;
}

void ProcessExternalComboClearInput(bool pressed, uint32_t now)
{
    if(pressed && !external_combo_was_pressed)
    {
        external_combo_down_ms = now;
        external_combo_clear_handled = false;
        external_combo_reset_handled = false;
    }
    else if(pressed)
    {
        const uint32_t held_ms = now - external_combo_down_ms;
        if(!external_combo_clear_handled && held_ms >= kExternalComboClearHoldMs)
        {
            RequestClearLoopContentKeepWindow();
            if(looper_state == LooperState::Playing && looper_length > 0)
                PrepareUndoPass();
            SetOverdubEnabled(true);
            external_combo_clear_handled = true;
            external_combo_block_individuals = true;
            DaisySeed::PrintLine("ext-combo-clear");
        }
        if(!external_combo_reset_handled && held_ms >= kExternalComboResetHoldMs)
        {
            ResetLoopImmediate();
            external_combo_reset_handled = true;
            external_combo_block_individuals = true;
            DaisySeed::PrintLine("ext-combo-reset");
        }
    }
    else if(!pressed && external_combo_was_pressed)
    {
        external_combo_clear_handled = false;
        external_combo_reset_handled = false;
    }

    external_combo_was_pressed = pressed;
}

void RememberQuantPointIfAllowed()
{
    if(!QuantizeEnabled())
        return;

    const uint32_t current_clock = QuantClockCount();
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

    const uint32_t elapsed_clocks = QuantClockCount() - looper_start_clock;
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
        else if(QuantizeStartToExternalBar())
        {
            if(QuantClockCount() != looper_start_clock
               && TransportAtBar())
            {
                looper_start_clock = QuantClockCount();
                StartLoopRecording();
            }
        }
        else if(QuantClockCount() != looper_start_clock)
        {
            looper_start_clock = QuantClockCount();
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
        else if(QuantClockCount() != looper_end_clock)
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
    ReadLoopStereoAt(looper_read, playback_source_length, loop_l, loop_r);
    const float raw_loop_l = loop_l;
    const float raw_loop_r = loop_r;
    if(GrainModeActive())
    {
        float grain_l;
        float grain_r;
        ProcessGranularPlayer(grain_l, grain_r);
        float mix = fclamp(grain_mix, 0.0f, 1.0f);
        if(mix > 0.96f)
            mix = 1.0f;
        else if(mix < 0.04f)
            mix = 0.0f;
        loop_l = loop_l * (1.0f - mix) + grain_l * mix;
        loop_r = loop_r * (1.0f - mix) + grain_r * mix;
    }
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
    if(looper_wrap_fade_remaining > 0)
    {
        const float progress = 1.0f
                               - (static_cast<float>(looper_wrap_fade_remaining)
                                  / static_cast<float>(kLooperWrapFadeSamples));
        const float smooth = progress * progress * (3.0f - 2.0f * progress);
        loop_l *= smooth;
        loop_r *= smooth;
        looper_wrap_fade_remaining -= 1;
    }
    if(looper_clear_fade_remaining > 0)
    {
        const float fade = static_cast<float>(looper_clear_fade_remaining)
                           / static_cast<float>(kLooperClearFadeSamples);
        loop_l *= fade;
        loop_r *= fade;
        looper_clear_fade_remaining -= 1;
        if(looper_clear_fade_remaining == 0 && looper_clear_keep_window_pending)
        {
            ClearLoopContentKeepWindow();
            loop_l = 0.0f;
            loop_r = 0.0f;
        }
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
    if(looper_clear_keep_window_pending)
    {
        if(dip3_mono)
            MonoBufferWrite(write_index, 0.0f);
        else
        {
            loop_buffer_l[write_index] = 0.0f;
            loop_buffer_r[write_index] = 0.0f;
        }
        loop_l = 0.0f;
        loop_r = 0.0f;
        if(looper_clear_samples_remaining > 0)
            looper_clear_samples_remaining -= 1;
        if(looper_clear_samples_remaining == 0)
        {
            looper_clear_keep_window_pending = false;
            ResetGranularBuffer();
        }
    }
    const bool repeat_fill_was_active = RepeatFillActive();
    if(!looper_clear_keep_window_pending)
        ProcessRepeatFill(input_l, input_r, feedback);

    const bool should_write = !looper_clear_keep_window_pending
                              && !repeat_fill_was_active
                              && (fabsf(PlaybackStep()) >= 1.0f || LooperReadIsOnSample());
    if(should_write)
    {
        const float fb = fclamp(feedback, 0.0f, 1.0f);
        const float overdub_gain = overdub_gain_current;
        if(dip3_mono)
        {
            const float mono_input = (input_l + input_r) * 0.5f;
            BackupUndoSample(write_index, MonoBufferRead(write_index), 0.0f);
            MonoBufferWrite(write_index,
                            LooperSoftLimit((raw_loop_l * fb)
                                            + (mono_input * overdub_gain * kWritePad)));
        }
        else
        {
            BackupUndoSample(write_index, loop_buffer_l[write_index], loop_buffer_r[write_index]);
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
    if(switch2_was_middle && !switch2_middle && ExternalQuantClockActive())
        SetMetroAnchor(tempo::MidiClockCount(), true);
    else if(switch2_was_middle && !switch2_middle && !MidiClockUsable())
        switch2_internal_clock_start_pending = true;
    if(!switch2_was_middle && switch2_middle)
    {
        switch2_pickup_refresh_pending = true;
        switch2_internal_clock_start_pending = false;
        internal_clock_running = false;
        ResetMetroAnchor();
    }
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
    const uint32_t now = System::GetNow();
    UpdateExternalSwitchBits(now);
    const bool external_overdub_pressed = (external_switch_stable_bits & kExternalFsw2Bit) != 0;
    const bool external_loop_pressed = (external_switch_stable_bits & kExternalFsw1Bit) != 0;
    const bool external_combo_pressed = external_overdub_pressed && external_loop_pressed;
    ProcessExternalComboClearInput(external_combo_pressed, now);
    if(external_combo_pressed)
        external_combo_block_individuals = true;
    if(external_combo_block_individuals && !external_overdub_pressed && !external_loop_pressed)
        external_combo_block_individuals = false;

    if(external_combo_pressed || external_combo_block_individuals)
        ResetExternalIndividualInputs();
    else
        ProcessExternalOverdubUndoInput(external_overdub_pressed, now);

    const bool fsw1_pressed = hw.switches[Funbox::FOOTSWITCH_1].Pressed();
    const bool fsw2_pressed = hw.switches[Funbox::FOOTSWITCH_2].Pressed();

    if(fsw1_pressed && !footswitch1_was_pressed)
    {
        footswitch1_down_ms = now;
        footswitch1_shift_handled = false;
    }
    else if(fsw1_pressed && !footswitch1_shift_handled && !fsw2_pressed && !external_loop_pressed)
    {
        const uint32_t held_ms = now - footswitch1_down_ms;
        if(held_ms >= kShiftHoldMs)
        {
            shift_enabled = !shift_enabled;
            shift_pickup_refresh_pending = true;
            footswitch1_shift_handled = true;
            TriggerLed2Ack();
        }
    }
    else if(!fsw1_pressed && footswitch1_was_pressed)
    {
        if(!footswitch1_shift_handled)
            ToggleOverdub();
        footswitch1_shift_handled = false;
    }

    ProcessLooperFootswitch2Input(fsw2_pressed,
                                  now,
                                  footswitch2_down_ms,
                                  footswitch2_was_pressed,
                                  footswitch2_long_handled,
                                  footswitch2_clear_handled,
                                  footswitch2_started_loop_on_press);
    if(!external_combo_pressed && !external_combo_block_individuals)
    {
        ProcessLooperFootswitch2Input(external_loop_pressed,
                                      now,
                                      external_fsw2_down_ms,
                                      external_fsw2_was_pressed,
                                      external_fsw2_long_handled,
                                      external_fsw2_clear_handled,
                                      external_fsw2_started_loop_on_press);
    }

    footswitch1_was_pressed = fsw1_pressed;
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

float Led2WaitingQuantBrightness()
{
    const float base = 0.35f;
    if(looper_state != LooperState::WaitingEnd || !QuantizeEnabled())
        return base;

    const uint32_t quarter_phase = QuantClockCount() % kMidiClocksPerBeat;
    if(quarter_phase >= 6)
        return base;

    const float pulse = 1.0f - (static_cast<float>(quarter_phase) / 6.0f);
    return base + (1.0f - base) * pulse;
}

void UpdateLeds()
{
    if(MidiClockUsable())
    {
        const float base = shift_enabled ? 0.35f : (overdub_enabled ? 1.0f : 0.0f);
        const bool midi_pulse = System::GetNow() < midi_led_pulse_until_ms;
        led1.Set(midi_pulse ? (base > 0.0f ? 0.0f : 1.0f) : base);
    }
    else
    {
        led1.Set(shift_enabled ? 0.35f : (overdub_enabled ? 1.0f : 0.0f));
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
    else if(looper_state == LooperState::WaitingEnd)
    {
        led2.Set(Led2WaitingQuantBrightness());
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
    cpu_load_meter.OnBlockStart();

    UpdateControls();

    const float gain = knob3_volume.Process();
    const float knob2_value = knob2_metro_volume.Process();
    const float feedback = SnapTop(knob6_feedback.Process(), 0.96f);
    const float knob1_value = knob1_grain_mix.Process();
    const float knob4_value = knob4_grain_size.Process();
    const float knob5_value = knob5_grain_pitch.Process();
    const bool metro_off = MetroBeatsPerBar() == 0;
    if(shift_pickup_refresh_pending)
    {
        LockActivePickupParams(knob1_value, knob2_value, knob4_value, knob5_value);
        shift_pickup_refresh_pending = false;
    }
    if(switch2_pickup_refresh_pending)
    {
        LockK2K4K5PickupParams(knob2_value, knob4_value, knob5_value);
        switch2_pickup_refresh_pending = false;
    }
    if(shift_enabled)
        ProcessPickupParam(k1_age_pickup, knob1_value);
    else
        ProcessPickupParam(k1_mix_pickup, knob1_value);
    grain_mix = k1_mix_pickup.value;
    grain_size_random = k1_age_pickup.value;
    grain_age_continuous = false;
    if(metro_off)
    {
        if(shift_enabled)
            ProcessPickupParam(k2_random_pickup, knob2_value);
        else
            ProcessPickupParam(k2_speed_pickup, knob2_value);
    }
    else
    {
        grain_random_amount = 0.0f;
    }
    if(shift_enabled)
        ProcessPickupParam(k4_density_pickup, knob4_value);
    else if(metro_off)
        ProcessPickupParam(k4_size_pickup, knob4_value);
    if(!metro_off)
        internal_tempo_bpm = kInternalTempoMinBpm
                             + knob5_value * (kInternalTempoMaxBpm - kInternalTempoMinBpm);
    if(switch2_internal_clock_start_pending)
    {
        if(MeterModeActive() && !MidiClockUsable())
            SetInternalClockAnchor(true);
        switch2_internal_clock_start_pending = false;
    }
    grain_speed_knob_value = k2_speed_pickup.value;
    grain_random_amount = metro_off ? k2_random_pickup.value : 0.0f;
    grain_size_ms = 10.0f * powf(100.0f, k4_size_pickup.value);
    const float grain_space = k4_density_pickup.value;
    grain_density = grain_space;
    grain_buffer_age = grain_space;
    grain_position_random = grain_space > 0.75f ? (grain_space - 0.75f) * 4.0f : 0.0f;
    const float grain_speed_knob = ((1.0f - grain_speed_knob_value) * 2.0f - 1.0f);
    grain_speed = metro_off ? ApplyCenterDeadzone(grain_speed_knob, 0.04f) * 2.0f : 1.0f;
    if(metro_off)
    {
        if(shift_enabled)
            ProcessPickupParam(k5_pitch_random_pickup, knob5_value);
        else
            ProcessPickupParam(k5_pitch_pickup, knob5_value);
    }
    grain_pitch_knob_value = k5_pitch_pickup.value;
    grain_window_shape = kFixedGrainWindowShape;
    grain_pitch_cents = metro_off ? ((grain_pitch_knob_value * 24.0f - 12.0f) * 100.0f)
                                  : 1200.0f;
    grain_pitch_random_cents = k5_pitch_random_pickup.value * 1200.0f;
    const float metro_volume = metro_off ? 0.0f : knob2_value;

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
        ProcessInternalClockSample();
        UpdateOverdubGainSlew();
        ProcessLooperSample(input_l, input_r, feedback, loop_l, loop_r);

        const float metro = ProcessMetroSample(metro_volume);
        out[0][i] = LooperSoftLimit((input_l + loop_l * gain + metro) * kMonitorPad);
        out[1][i] = LooperSoftLimit((input_r + loop_r * gain + metro) * kMonitorPad);
    }

    UpdateLeds();
    led1.Update();
    led2.Update();
    cpu_load_meter.OnBlockEnd();
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
                if(ExternalQuantClockActive())
                    ProcessMetroClock(tempo::MidiClockCount());
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
                internal_clock_running = false;
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
    audio_sample_rate = samplerate;
    hw.SetAudioBlockSize(48);
    cpu_load_meter.Init(samplerate, 48);
    DaisySeed::StartLog(false);
    DaisySeed::PrintLine("Looper serial ready");

    knob1_grain_mix.Init(hw.knob[Funbox::KNOB_1], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    knob2_metro_volume.Init(hw.knob[Funbox::KNOB_2], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    knob3_volume.Init(hw.knob[Funbox::KNOB_3], 0.0f, 2.0f, ::daisy::Parameter::LINEAR);
    knob4_grain_size.Init(hw.knob[Funbox::KNOB_4], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    knob5_grain_pitch.Init(hw.knob[Funbox::KNOB_5], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    knob6_feedback.Init(hw.knob[Funbox::KNOB_6], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    expression.Init(hw.expression, 0.0f, 1.0f, ::daisy::Parameter::LINEAR);

    tempo::Init(samplerate);
    tempo::SetSyncMode(tempo::SyncMode::Midi);
    ResetGranularBuffer();
    granular_player.Init(granular_buffer, static_cast<int>(kGranularPlayerSamples), samplerate);

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
        PrintSerialStatus();
    }
}
