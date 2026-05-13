#include "daisy_petal.h"
#include "daisysp.h"
#include "funbox.h"
#include "expressionHandler.h"

#include "envelopeFollower.h"
#include "Effects/flanger.h"
#include "Effects/pitchshifter.h"
#include "moogladder.h"
#include "oscillator.h"
#include "sampleratereducer.h"
#include "svf.h"

using namespace daisy;
using namespace daisysp;
using namespace funbox;

enum class EffectMode
{
    CrushToFilter,
    ThruToFilter,
    RingMod,
};

enum class FilterMode
{
    Lowpass,
    Bandpass,
    Highpass,
};

enum class RouteMode
{
    Normal,
    TriplePitch,
    TripleFlanger,
};

struct Settings
{
    float knobs[6];
    float alternate_knobs[6];
    ExpressionHandler::State expression;
    int   switches[3];
    bool  dips[4];

    bool operator!=(const Settings& a) const
    {
        for(int i = 0; i < 6; i++)
        {
            if(a.knobs[i] != knobs[i] || a.alternate_knobs[i] != alternate_knobs[i]
               || a.expression.expression_up[i] != expression.expression_up[i]
               || a.expression.expression_down[i] != expression.expression_down[i]
               || a.expression.expression_knob_active[i]
                      != expression.expression_knob_active[i])
                return true;
        }
        for(int i = 0; i < 3; i++)
        {
            if(a.switches[i] != switches[i])
                return true;
        }
        for(int i = 0; i < 4; i++)
        {
            if(a.dips[i] != dips[i])
                return true;
        }
        return a.expression.expressionEngaged != expression.expressionEngaged;
    }
};

struct StorageData
{
    uint32_t magic;
    uint32_t version;
    Settings presets[5];
    Settings pitch_presets[5];
    Settings flanger_presets[5];

    bool operator!=(const StorageData& a) const
    {
        if(a.magic != magic || a.version != version)
            return true;
        for(int i = 0; i < 5; i++)
        {
            if(a.presets[i] != presets[i])
                return true;
            if(a.pitch_presets[i] != pitch_presets[i])
                return true;
            if(a.flanger_presets[i] != flanger_presets[i])
                return true;
        }
        return false;
    }
};

static constexpr uint32_t kStorageMagic = 0x42433350; // "BC3P"
static constexpr uint32_t kStorageVersion = 1;
static constexpr uint32_t kPresetClickTimeoutMs = 1000;
static constexpr uint32_t kSaveSelectTimeoutMs = 3000;
static constexpr uint32_t kPresetLedStepMs = 200;
static constexpr uint32_t kSaveLedStepMs = 80;
static constexpr uint32_t kSaveSelectHoldMs = 700;
static constexpr uint32_t kPresetOffHoldMs = 1000;

DaisyPetal hw;
Parameter  param1, param2, param3, param4, param5, param6, expression;
Led        led1, led2;

bool bypass = true;
bool alternateMode = false;
bool expression_pressed = false;
bool use_preset = false;
bool trigger_save = false;
bool update_switches = true;
uint32_t button2_down_ms = 0;
uint32_t preset_click_start_ms = 0;
uint32_t save_select_start_ms = 0;
uint32_t led2_pattern_ms = 0;
int  pending_preset_clicks = 0;
int  pending_save_clicks = 0;
int  active_preset = -1;
int  led2_phase = 0;
bool save_select_active = false;

bool pswitch1[2], pswitch2[2], pswitch3[2], pdip[4];
int  switch1[2], switch2[2], switch3[2], dip[4];
int  switchValues[3] = {1, 1, 1};
bool dipValues[4] = {false, false, false, false};

float knobValues[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
float pknobValues[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
float alternateKnobValues[6] = {0.5f, 0.5f, 0.2f, 0.0f, 0.333f, 0.404f};
float physicalKnobValues[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
float alternatePickupValues[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
bool  normalPickupActive[6] = {false, false, false, false, false, false};

EffectMode effectMode = EffectMode::ThruToFilter;
FilterMode filterMode = FilterMode::Lowpass;
RouteMode  routeMode = RouteMode::Normal;

struct RouteControls
{
    float knobs[6];
    float alternate_knobs[6];
    bool  valid;
};

RouteControls routeControls[3] = {};

ExpressionHandler expHandler;
PersistentStorage<StorageData> SavedSettings(hw.seed.qspi);

SampleRateReducer srReducerL, srReducerR;
SampleRateReducer ringSrReducerL, ringSrReducerR;
MoogLadder        ladderL, ladderR;
Svf               svfL, svfR;
DSY_SDRAM_BSS PitchShifter pitchL;
DSY_SDRAM_BSS PitchShifter pitchR;
DSY_SDRAM_BSS PitchShifter pitch2L;
DSY_SDRAM_BSS PitchShifter pitch2R;
DSY_SDRAM_BSS PitchShifter pitch3L;
DSY_SDRAM_BSS PitchShifter pitch3R;
Flanger           flangerL, flangerR;
Flanger           flanger2L, flanger2R, flanger3L, flanger3R;
Oscillator        ringOsc;
EnvelopeFollower  envFollower;

float samplerate = 48000.0f;
float envInputGain = 1.0f;
float envAttack = 0.01f;
float envRelease = 0.1f;
float filterResonance = 0.2f;
float cutoffControl = 0.5f;
float ringBaseFreq = 100.0f;

float Clamp01(float v)
{
    return fclamp(v, 0.0f, 1.0f);
}

float Bipolar(float v)
{
    return (v - 0.5f) * 2.0f;
}

float BipolarDeadband(float v)
{
    const float deadband = 0.06f;
    float       b        = Bipolar(v);
    if(fabsf(b) < deadband)
        return 0.0f;

    const float sign = b < 0.0f ? -1.0f : 1.0f;
    b                = (fabsf(b) - deadband) / (1.0f - deadband);
    return sign * Clamp01(b);
}

float ExpMap(float knob, float min_v, float max_v)
{
    return min_v * powf(max_v / min_v, Clamp01(knob));
}

float QuantizeBits(float in, float bits_control)
{
    const float bits = 1.0f + Clamp01(bits_control) * 15.0f;
    const float levels = powf(2.0f, bits) - 1.0f;
    return roundf(fclamp(in, -1.0f, 1.0f) * levels) / levels;
}

float ApplyEnv(float base, float amount, float env)
{
    return Clamp01(base + Bipolar(amount) * env);
}

float ApplyEnvDeadband(float base, float amount, float env)
{
    return Clamp01(base + BipolarDeadband(amount) * env);
}

float EnvScale(float amount, float env, float depth)
{
    return powf(2.0f, Bipolar(amount) * env * depth);
}

float EnvScaleDeadband(float amount, float env, float depth)
{
    return powf(2.0f, BipolarDeadband(amount) * env * depth);
}

int SwitchValue(bool left, bool right)
{
    if(left)
        return 0;
    if(right)
        return 2;
    return 1;
}

bool KnobMoved(float old_value, float new_value)
{
    const float tolerance = 0.005f;
    return new_value > old_value + tolerance || new_value < old_value - tolerance;
}

void ReadPhysicalKnobs()
{
    physicalKnobValues[0] = param1.Process();
    physicalKnobValues[1] = param2.Process();
    physicalKnobValues[2] = param3.Process();
    physicalKnobValues[3] = param4.Process();
    physicalKnobValues[4] = param5.Process();
    physicalKnobValues[5] = param6.Process();
}

void CaptureAlternatePickup()
{
    for(int i = 0; i < 6; i++)
        alternatePickupValues[i] = physicalKnobValues[i];
}

void CaptureNormalPickup()
{
    for(int i = 0; i < 6; i++)
    {
        pknobValues[i] = physicalKnobValues[i];
        normalPickupActive[i] = true;
    }
}

void UpdateSwitch1();
void UpdateSwitch2(bool restore_controls = true);
void UpdateSwitch3();

int RouteIndex(RouteMode route)
{
    if(route == RouteMode::TriplePitch)
        return 1;
    if(route == RouteMode::TripleFlanger)
        return 2;
    return 0;
}

RouteMode RouteFromSwitch2()
{
    if(switchValues[1] == 1)
        return RouteMode::TriplePitch;
    if(switchValues[1] == 2)
        return RouteMode::TripleFlanger;
    return RouteMode::Normal;
}

void SaveRouteControls(RouteMode route)
{
    RouteControls& c = routeControls[RouteIndex(route)];
    for(int i = 0; i < 6; i++)
    {
        c.knobs[i] = knobValues[i];
        c.alternate_knobs[i] = alternateKnobValues[i];
    }
    c.valid = true;
}

void RestoreRouteControls(RouteMode route)
{
    RouteControls& c = routeControls[RouteIndex(route)];
    if(!c.valid)
        return;

    for(int i = 0; i < 6; i++)
    {
        knobValues[i] = c.knobs[i];
        alternateKnobValues[i] = c.alternate_knobs[i];
    }
    CaptureNormalPickup();
    CaptureAlternatePickup();
}

void ApplyPreset(int index)
{
    StorageData& data = SavedSettings.GetSettings();
    if(data.magic != kStorageMagic || data.version != kStorageVersion)
        return;

    Settings& s = routeMode == RouteMode::TriplePitch     ? data.pitch_presets[index]
                  : routeMode == RouteMode::TripleFlanger ? data.flanger_presets[index]
                                                          : data.presets[index];
    for(int i = 0; i < 6; i++)
    {
        knobValues[i] = s.knobs[i];
        alternateKnobValues[i] = s.alternate_knobs[i];
    }
    for(int i = 0; i < 3; i++)
        switchValues[i] = s.switches[i];
    for(int i = 0; i < 4; i++)
        dipValues[i] = s.dips[i];
    expHandler.LoadState(s.expression);
    use_preset = true;
    active_preset = index;
    led2_phase = 0;
    led2_pattern_ms = System::GetNow();
    CaptureNormalPickup();
    CaptureAlternatePickup();
    led1.Set(bypass ? 0.0f : 1.0f);
    UpdateSwitch1();
    UpdateSwitch3();
    UpdateSwitch2(false);
    SaveRouteControls(routeMode);
}

void StorePreset(int index)
{
    StorageData& data = SavedSettings.GetSettings();
    data.magic = kStorageMagic;
    data.version = kStorageVersion;
    Settings& s = routeMode == RouteMode::TriplePitch     ? data.pitch_presets[index]
                  : routeMode == RouteMode::TripleFlanger ? data.flanger_presets[index]
                                                          : data.presets[index];
    for(int i = 0; i < 6; i++)
    {
        s.knobs[i] = knobValues[i];
        s.alternate_knobs[i] = alternateKnobValues[i];
    }
    expHandler.SaveState(s.expression);
    for(int i = 0; i < 3; i++)
        s.switches[i] = switchValues[i];
    for(int i = 0; i < 4; i++)
        s.dips[i] = dipValues[i];
    trigger_save = true;
    use_preset = true;
    active_preset = index;
    led2_phase = 0;
    led2_pattern_ms = System::GetNow();
    led1.Set(bypass ? 0.0f : 1.0f);
}

void DisablePreset()
{
    use_preset = false;
    active_preset = -1;
    pending_preset_clicks = 0;
    pending_save_clicks = 0;
    save_select_active = false;
    update_switches = true;
}

void UpdateSwitch1()
{
    if(switchValues[0] == 0)
        effectMode = EffectMode::CrushToFilter;
    else if(switchValues[0] == 2)
        effectMode = EffectMode::RingMod;
    else
        effectMode = EffectMode::ThruToFilter;
}

void UpdateSwitch3()
{
    if(switchValues[2] == 0)
        filterMode = FilterMode::Lowpass;
    else if(switchValues[2] == 2)
        filterMode = FilterMode::Highpass;
    else
        filterMode = FilterMode::Bandpass;
}

void UpdateSwitch2(bool restore_controls)
{
    const RouteMode new_route = RouteFromSwitch2();
    if(new_route == routeMode)
        return;

    if(restore_controls)
        SaveRouteControls(routeMode);

    routeMode = new_route;

    if(restore_controls)
        RestoreRouteControls(routeMode);
}

void UpdateLed2()
{
    if(expHandler.isExpressionSetMode())
        return;

    if(save_select_active)
    {
        led2.Set(((System::GetNow() / kSaveLedStepMs) % 2) == 0 ? 1.0f : 0.0f);
        return;
    }

    if(use_preset && active_preset >= 0)
    {
        const uint32_t now = System::GetNow();
        if(now - led2_pattern_ms >= kPresetLedStepMs)
        {
            led2_pattern_ms = now;
            led2_phase++;
        }

        const int pulses = active_preset + 1;
        const int phases = pulses * 2 + 1;
        if(led2_phase >= phases)
            led2_phase = 0;

        led2.Set((led2_phase < pulses * 2 && led2_phase % 2 == 0) ? 1.0f
                                                                  : 0.0f);
        return;
    }

    led2.Set(0.0f);
}

void UpdateButtons()
{
    if(hw.switches[Funbox::FOOTSWITCH_1].FallingEdge())
    {
        if(!expression_pressed)
        {
            if(alternateMode)
            {
                alternateMode = false;
                CaptureNormalPickup();
                led1.Set(bypass ? 0.0f : 1.0f);
            }
            else
            {
                bypass = !bypass;
                led1.Set(bypass ? 0.0f : 1.0f);
            }
        }
        expression_pressed = false;
    }

    if(hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() >= 600 && !alternateMode
       && !bypass && !hw.switches[Funbox::FOOTSWITCH_2].Pressed()
       && !expression_pressed)
    {
        alternateMode = true;
        CaptureAlternatePickup();
        led1.Set(0.5f);
    }

    if(hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() >= 500
       && hw.switches[Funbox::FOOTSWITCH_2].TimeHeldMs() >= 500
       && !expression_pressed)
    {
        expHandler.ToggleExpressionSetMode();
        if(expHandler.isExpressionSetMode())
        {
            led1.Set(expHandler.returnLed1Brightness());
            led2.Set(expHandler.returnLed2Brightness());
        }
        else
        {
            led1.Set(bypass ? 0.0f : 1.0f);
            led2.Set(use_preset ? 1.0f : 0.0f);
        }
        expression_pressed = true;
    }

    if(hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() >= 2000
       && hw.switches[Funbox::FOOTSWITCH_2].TimeHeldMs() >= 2000)
    {
        expHandler.Reset();
        led1.Set(bypass ? 0.0f : 1.0f);
        led2.Set(use_preset ? 1.0f : 0.0f);
    }

    if(hw.switches[Funbox::FOOTSWITCH_2].RisingEdge())
    {
        button2_down_ms = System::GetNow();
    }

    if(hw.switches[Funbox::FOOTSWITCH_2].FallingEdge() && !expression_pressed)
    {
        const uint32_t held_ms = System::GetNow() - button2_down_ms;
        if(held_ms >= kPresetOffHoldMs
           && hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() <= 50)
        {
            DisablePreset();
        }
        else if(held_ms >= kSaveSelectHoldMs
                && hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() <= 50)
        {
            save_select_active = true;
            pending_preset_clicks = 0;
            pending_save_clicks = 0;
            save_select_start_ms = System::GetNow();
        }
        else if(held_ms < 350)
        {
            if(save_select_active)
            {
                if(pending_save_clicks < 5)
                    pending_save_clicks++;
                save_select_start_ms = System::GetNow();
            }
            else
            {
                if(pending_preset_clicks < 5)
                    pending_preset_clicks++;
                preset_click_start_ms = System::GetNow();
            }
        }
    }

    if(save_select_active && !hw.switches[Funbox::FOOTSWITCH_2].Pressed()
       && System::GetNow() - save_select_start_ms >= kSaveSelectTimeoutMs)
    {
        if(pending_save_clicks > 0)
            StorePreset(pending_save_clicks - 1);
        else
            DisablePreset();
        pending_save_clicks = 0;
        save_select_active = false;
    }

    if(!save_select_active && pending_preset_clicks > 0
       && System::GetNow() - preset_click_start_ms >= kPresetClickTimeoutMs
       && !hw.switches[Funbox::FOOTSWITCH_2].Pressed())
    {
        const int target_preset = pending_preset_clicks - 1;
        if(use_preset && active_preset == target_preset)
            StorePreset(target_preset);
        else
            ApplyPreset(target_preset);
        pending_preset_clicks = 0;
    }

    UpdateLed2();
}

void UpdateSwitches()
{
    bool changed1 = false;
    for(int i = 0; i < 2; i++)
    {
        if(hw.switches[switch1[i]].Pressed() != pswitch1[i])
        {
            pswitch1[i] = hw.switches[switch1[i]].Pressed();
            changed1 = true;
        }
    }
    if(changed1 || update_switches)
    {
        switchValues[0] = SwitchValue(pswitch1[0], pswitch1[1]);
        UpdateSwitch1();
    }

    bool changed2 = false;
    for(int i = 0; i < 2; i++)
    {
        if(hw.switches[switch2[i]].Pressed() != pswitch2[i])
        {
            pswitch2[i] = hw.switches[switch2[i]].Pressed();
            changed2 = true;
        }
    }
    if(changed2 || update_switches)
{
    switchValues[1] = SwitchValue(pswitch2[0], pswitch2[1]);
    UpdateSwitch2();
}
    bool changed3 = false;
    for(int i = 0; i < 2; i++)
    {
        if(hw.switches[switch3[i]].Pressed() != pswitch3[i])
        {
            pswitch3[i] = hw.switches[switch3[i]].Pressed();
            changed3 = true;
        }
    }
    if(changed3 || update_switches)
{
    switchValues[2] = SwitchValue(pswitch3[0], pswitch3[1]);
    UpdateSwitch3();
    UpdateSwitch2();
}

    bool changed4 = false;
    for(int i = 0; i < 4; i++)
    {
        if(hw.switches[dip[i]].Pressed() != pdip[i])
        {
            pdip[i] = hw.switches[dip[i]].Pressed();
            changed4 = true;
        }
    }
    if(changed4 || update_switches)
    {
        for(int i = 0; i < 4; i++)
            dipValues[i] = pdip[i];
    }

    update_switches = false;
}

float ProcessFilter(float in, float cutoff, float resonance, bool left)
{
    if(filterMode == FilterMode::Lowpass)
    {
        MoogLadder& f = left ? ladderL : ladderR;
        f.SetFreq(cutoff);
        f.SetRes(resonance);
        return fclamp(f.Process(in) * 1.8f, -1.0f, 1.0f);
    }

    if(filterMode == FilterMode::Bandpass)
    {
        Svf& f = left ? svfL : svfR;
        f.SetFreq(cutoff);
        f.SetRes(resonance);
        f.SetDrive(0.5f);
        f.Process(in);
        return f.Band();
    }

    if(filterMode == FilterMode::Highpass)
    {
        Svf& f = left ? svfL : svfR;
        f.SetFreq(cutoff);
        f.SetRes(resonance);
        f.SetDrive(0.5f);
        f.Process(in);
        return f.High();
    }

    return in;
}

float PitchSemitones(float knob)
{
    return -12.0f + (Clamp01(knob) * 24.0f);
}

float RouteKnobValue(RouteMode route, int index)
{
    if(route == routeMode)
        return knobValues[index];

    RouteControls& c = routeControls[RouteIndex(route)];
    return c.valid ? c.knobs[index] : 0.5f;
}

float RouteAlternateValue(RouteMode route, int index)
{
    if(route == routeMode)
        return alternateKnobValues[index];

    RouteControls& c = routeControls[RouteIndex(route)];
    return c.valid ? c.alternate_knobs[index] : 0.0f;
}

float ProcessPitchVoice(float dry_in,
                        float pre_filter_in,
                        bool  left,
                        int   index,
                        float pitch,
                        float volume,
                        float source_select)
{
    PitchShifter* p = nullptr;
    if(index == 0)
        p = left ? &pitchL : &pitchR;
    else if(index == 1)
        p = left ? &pitch2L : &pitch2R;
    else
        p = left ? &pitch3L : &pitch3R;

    p->SetTransposition(PitchSemitones(pitch));
    const float source_mix = Clamp01(source_select);
    float       source     = (dry_in * (1.0f - source_mix)) + (pre_filter_in * source_mix);
    return p->Process(source) * Clamp01(volume);
}

float ProcessPitchLayer(float dry_in, float pre_filter_in, bool left)
{
    const float p1 = ProcessPitchVoice(dry_in,
                                       pre_filter_in,
                                       left,
                                       0,
                                       RouteKnobValue(RouteMode::TriplePitch, 0),
                                       RouteKnobValue(RouteMode::TriplePitch, 3),
                                       RouteAlternateValue(RouteMode::TriplePitch, 3));
    const float p2 = ProcessPitchVoice(dry_in,
                                       pre_filter_in,
                                       left,
                                       1,
                                       RouteKnobValue(RouteMode::TriplePitch, 1),
                                       RouteKnobValue(RouteMode::TriplePitch, 4),
                                       RouteAlternateValue(RouteMode::TriplePitch, 4));
    const float p3 = ProcessPitchVoice(dry_in,
                                       pre_filter_in,
                                       left,
                                       2,
                                       RouteKnobValue(RouteMode::TriplePitch, 2),
                                       RouteKnobValue(RouteMode::TriplePitch, 5),
                                       RouteAlternateValue(RouteMode::TriplePitch, 5));
    return fclamp((p1 + p2 + p3) * 0.6f, -1.0f, 1.0f);
}

float ProcessFlangerVoice(float in,
                          bool  left,
                          int   index,
                          float delay,
                          float env_amount,
                          float lfo_depth,
                          float lfo_freq,
                          float feedback,
                          float env)
{
    Flanger* f = nullptr;
    if(index == 0)
        f = left ? &flangerL : &flangerR;
    else if(index == 1)
        f = left ? &flanger2L : &flanger2R;
    else
        f = left ? &flanger3L : &flanger3R;

    const float delay_ms = ExpMap(ApplyEnvDeadband(delay, env_amount, env), 1.0f, 19.0f);
    f->SetDelayMs(delay_ms);
    f->SetLfoFreq(ExpMap(lfo_freq, 0.02f, 12.0f));
    f->SetLfoDepth(Clamp01(lfo_depth));
    f->SetFeedback(Clamp01(feedback) * 0.96f);
    return fclamp((f->Process(in) - in) * 1.05f, -1.0f, 1.0f);
}

float ProcessTripleFlanger(float in,
                           bool  left,
                           float k1,
                           float k2,
                           float k3,
                           float k4,
                           float k5,
                           float k6,
                           float env)
{
    const float dry_wet  = alternateKnobValues[0];
    const float lfo_freq = alternateKnobValues[1];
    const float feedback = alternateKnobValues[2];
    const float f1 = ProcessFlangerVoice(in, left, 0, k1, k4, alternateKnobValues[3], lfo_freq, feedback, env);
    const float f2 = ProcessFlangerVoice(in, left, 1, k2, k5, alternateKnobValues[4], lfo_freq, feedback, env);
    const float f3 = ProcessFlangerVoice(in, left, 2, k3, k6, alternateKnobValues[5], lfo_freq, feedback, env);
    const float wet = (f1 + f2 + f3) * 0.333333f;
    const float mix = Clamp01(dry_wet);
    return fclamp((in * (1.0f - mix)) + (wet * mix), -1.0f, 1.0f);
}

float ProcessPreFilter(float in, bool left, float bits, float sr_reduce, float ring)
{
    float x = in;

    if(effectMode == EffectMode::RingMod)
    {
        x *= ring;
        SampleRateReducer& reducer = left ? ringSrReducerL : ringSrReducerR;
        reducer.SetFreq(ExpMap(1.0f - sr_reduce, 0.005f, 1.0f));
        x = reducer.Process(x);
    }
    else if(effectMode == EffectMode::CrushToFilter)
    {
        x = QuantizeBits(x, bits);
        SampleRateReducer& reducer = left ? srReducerL : srReducerR;
        reducer.SetFreq(ExpMap(1.0f - sr_reduce, 0.005f, 1.0f));
        x = reducer.Process(x);
    }

    return x;
}

float ProcessVoice(
    float in, bool left, float bits, float sr_reduce, float cutoff, float resonance, float ring)
{
    const float x = ProcessPreFilter(in, left, bits, sr_reduce, ring);
    return ProcessFilter(x, cutoff, resonance, left);
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();
    ReadPhysicalKnobs();
    led1.Update();
    led2.Update();

    UpdateButtons();
    UpdateSwitches();

    if(alternateMode)
    {
        const int first_alt_knob = routeMode == RouteMode::Normal ? 2 : 0;
        for(int i = first_alt_knob; i < 6; i++)
        {
            if(KnobMoved(alternatePickupValues[i], physicalKnobValues[i]))
            {
                alternatePickupValues[i] = physicalKnobValues[i];
                alternateKnobValues[i] = physicalKnobValues[i];
            }
        }
    }
    else
    {
        for(int i = 0; i < 6; i++)
        {
            if(normalPickupActive[i])
            {
                if(KnobMoved(pknobValues[i], physicalKnobValues[i]))
                {
                    normalPickupActive[i] = false;
                    pknobValues[i] = knobValues[i] = physicalKnobValues[i];
                }
            }
            else
            {
                pknobValues[i] = knobValues[i] = physicalKnobValues[i];
            }
        }
    }

    float expressionValues[6];
    expHandler.Process(expression.Process(), knobValues, expressionValues);
    if(expHandler.isExpressionSetMode())
    {
        led1.Set(expHandler.returnLed1Brightness());
        led2.Set(expHandler.returnLed2Brightness());
    }

    float k1 = expressionValues[0];
    float k2 = expressionValues[1];
    float k3 = expressionValues[2];
    float k4 = expressionValues[3];
    float k5 = expressionValues[4];
    float k6 = expressionValues[5];

    if(routeMode == RouteMode::Normal)
    {
        filterResonance = alternateKnobValues[2];
        envInputGain = 1.0f + alternateKnobValues[3] * 18.0f;
        envAttack = ExpMap(alternateKnobValues[4], 0.001f, 1.0f);
        envRelease = ExpMap(alternateKnobValues[5], 0.01f, 3.0f);
    }

    envFollower.SetAttack(envAttack);
    envFollower.SetRelease(envRelease);

    float bits = k1;
    float sr_reduce = k2;
    float resonance = filterResonance;

    if(effectMode == EffectMode::RingMod)
    {
        ringBaseFreq = ExpMap(k1, 0.5f, 2000.0f);
        ringOsc.SetWaveform(Oscillator::WAVE_SIN);
    }

    for(size_t i = 0; i < size; i++)
    {
        const float inL = in[0][i];
        const float inR = dipValues[0] ? in[1][i] : in[0][i];

        if(bypass)
        {
            out[0][i] = inL;
            out[1][i] = inR;
            continue;
        }

        const float input_gain = envInputGain * (dipValues[3] ? 3.5f : 1.0f);
        const float detector = fmaxf(fabsf(inL), fabsf(inR)) * input_gain;
        const float env = Clamp01(envFollower.Process(detector));

        float modBits = bits;
        float modSr = sr_reduce;
        if(!alternateMode && routeMode == RouteMode::Normal)
            cutoffControl = k3;

        float modCutoffControl = cutoffControl;
        float modRingFreq = ringBaseFreq;

        if(!alternateMode && routeMode == RouteMode::Normal)
        {
            if(effectMode == EffectMode::RingMod)
                modRingFreq = ringBaseFreq * EnvScaleDeadband(k4, env, ringBaseFreq * 0.5f);
            else
                modBits = ApplyEnvDeadband(bits, k4, env);
            modSr = ApplyEnvDeadband(sr_reduce, k5, env);
            modCutoffControl = ApplyEnvDeadband(cutoffControl, k6, env);
        }

        const float cutoff = ExpMap(modCutoffControl, 40.0f, 16000.0f);
        ringOsc.SetFreq(fclamp(modRingFreq, 0.1f, 6000.0f));
        const float ring = ringOsc.Process() * 2.0f;
        const float preFilterL = ProcessPreFilter(inL, true, modBits, modSr, ring);
        const float preFilterR = ProcessPreFilter(inR, false, modBits, modSr, ring);
        const float normalL = ProcessFilter(preFilterL, cutoff, resonance, true);
        const float normalR = ProcessFilter(preFilterR, cutoff, resonance, false);
        const float baseL = routeMode == RouteMode::TripleFlanger
                                ? ProcessTripleFlanger(inL, true, k1, k2, k3, k4, k5, k6, env)
                                : normalL;
        const float baseR = routeMode == RouteMode::TripleFlanger
                                ? ProcessTripleFlanger(inR, false, k1, k2, k3, k4, k5, k6, env)
                                : normalR;
        const float pitchL = ProcessPitchLayer(inL, preFilterL, true);
        const float pitchR = ProcessPitchLayer(inR, preFilterR, false);
        const float outL = fclamp(baseL + pitchL, -1.0f, 1.0f);
        const float outR = fclamp(baseR + pitchR, -1.0f, 1.0f);

        out[0][i] = outL;
        out[1][i] = outR;
    }
}

int main(void)
{
    hw.Init();
    samplerate = hw.AudioSampleRate();
    hw.SetAudioBlockSize(48);

    param1.Init(hw.knob[Funbox::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    param2.Init(hw.knob[Funbox::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    param3.Init(hw.knob[Funbox::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    param4.Init(hw.knob[Funbox::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    param5.Init(hw.knob[Funbox::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    param6.Init(hw.knob[Funbox::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);
    expression.Init(hw.expression, 0.0f, 1.0f, Parameter::LINEAR);

    srReducerL.Init();
    srReducerR.Init();
    ringSrReducerL.Init();
    ringSrReducerR.Init();
    ladderL.Init(samplerate);
    ladderR.Init(samplerate);
    svfL.Init(samplerate);
    svfR.Init(samplerate);
    pitchL.Init(samplerate);
    pitchR.Init(samplerate);
    pitch2L.Init(samplerate);
    pitch2R.Init(samplerate);
    pitch3L.Init(samplerate);
    pitch3R.Init(samplerate);
    flangerL.Init(samplerate);
    flangerR.Init(samplerate);
    flanger2L.Init(samplerate);
    flanger2R.Init(samplerate);
    flanger3L.Init(samplerate);
    flanger3R.Init(samplerate);
    ringOsc.Init(samplerate);
    ringOsc.SetAmp(0.5f);
    envFollower.Init(samplerate);
    expHandler.Init(6);

    switch1[0] = Funbox::SWITCH_1_LEFT;
    switch1[1] = Funbox::SWITCH_1_RIGHT;
    switch2[0] = Funbox::SWITCH_2_LEFT;
    switch2[1] = Funbox::SWITCH_2_RIGHT;
    switch3[0] = Funbox::SWITCH_3_LEFT;
    switch3[1] = Funbox::SWITCH_3_RIGHT;
    dip[0] = Funbox::SWITCH_DIP_1;
    dip[1] = Funbox::SWITCH_DIP_2;
    dip[2] = Funbox::SWITCH_DIP_3;
    dip[3] = Funbox::SWITCH_DIP_4;

    for(int i = 0; i < 2; i++)
    {
        pswitch1[i] = false;
        pswitch2[i] = false;
        pswitch3[i] = false;
    }
    for(int i = 0; i < 4; i++)
        pdip[i] = false;

    led1.Init(hw.seed.GetPin(Funbox::LED_1), false);
    led2.Init(hw.seed.GetPin(Funbox::LED_2), false);
    led1.Set(0.0f);
    led2.Set(0.0f);
    led1.Update();
    led2.Update();

    Settings defaultPreset = {};
    const float defaultKnobs[6] = {0.75f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f};
    const float defaultAlternate[6] = {0.5f, 0.5f, 0.2f, 0.0f, 0.333f, 0.404f};
    for(int i = 0; i < 6; i++)
    {
        defaultPreset.knobs[i] = defaultKnobs[i];
        defaultPreset.alternate_knobs[i] = defaultAlternate[i];
        defaultPreset.expression.expression_up[i] = 0.0f;
        defaultPreset.expression.expression_down[i] = 0.0f;
        defaultPreset.expression.expression_knob_active[i] = false;
    }
    defaultPreset.expression.expressionEngaged = false;
    defaultPreset.switches[0] = 1;
    defaultPreset.switches[1] = 1;
    defaultPreset.switches[2] = 0;
    for(int i = 0; i < 4; i++)
        defaultPreset.dips[i] = false;

    StorageData defaults = {};
    defaults.magic = kStorageMagic;
    defaults.version = kStorageVersion;
    for(int i = 0; i < 5; i++)
    {
        defaults.presets[i] = defaultPreset;
        defaults.pitch_presets[i] = defaultPreset;
        defaults.flanger_presets[i] = defaultPreset;
    }
    SavedSettings.Init(defaults);
    if(SavedSettings.GetSettings().magic != kStorageMagic
       || SavedSettings.GetSettings().version != kStorageVersion)
    {
        SavedSettings.RestoreDefaults();
    }

    UpdateSwitch1();
    UpdateSwitch3();
    UpdateSwitch2();
    

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        if(trigger_save)
        {
            SavedSettings.Save();
            trigger_save = false;
            led2_phase = 0;
            led2_pattern_ms = System::GetNow();
        }
        System::Delay(10);
    }
}
