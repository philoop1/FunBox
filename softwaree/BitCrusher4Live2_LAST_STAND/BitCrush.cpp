#include "daisy_petal.h"
#include "daisysp.h"
#include "funbox.h"
#include "expressionHandler.h"

#include "envelopeFollower.h"
#include "funbox_flanger.h"
#include "funbox_phaser.h"
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

enum class ExpressionSlotMode
{
    Slot1,
    Off,
    Slot2,
};

enum class RouteMode
{
    Normal,
    Phaser,
    Flanger,
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
    float live_alternate_knobs[6];
    Settings presets[3][5];
    Settings phaser_presets[5];
    Settings flanger_presets[5];
    ExpressionHandler::State expression_assignments[2];

    bool operator!=(const StorageData& a) const
    {
        if(a.magic != magic || a.version != version)
            return true;
        for(int i = 0; i < 6; i++)
        {
            if(a.live_alternate_knobs[i] != live_alternate_knobs[i])
                return true;
        }
        for(int i = 0; i < 5; i++)
        {
            for(int bank = 0; bank < 3; bank++)
            {
                if(a.presets[bank][i] != presets[bank][i])
                    return true;
            }
            if(a.phaser_presets[i] != phaser_presets[i])
                return true;
            if(a.flanger_presets[i] != flanger_presets[i])
                return true;
        }
        for(int slot = 0; slot < 2; slot++)
        {
            for(int i = 0; i < 6; i++)
            {
                if(a.expression_assignments[slot].expression_up[i]
                       != expression_assignments[slot].expression_up[i]
                   || a.expression_assignments[slot].expression_down[i]
                          != expression_assignments[slot].expression_down[i]
                   || a.expression_assignments[slot].expression_knob_active[i]
                          != expression_assignments[slot].expression_knob_active[i])
                    return true;
            }
            if(a.expression_assignments[slot].expressionEngaged
               != expression_assignments[slot].expressionEngaged)
                return true;
        }
        return false;
    }
};

static constexpr uint32_t kStorageMagic = 0x42434C34; // "BCL4"
static constexpr uint32_t kStorageVersion = 5;
static constexpr uint32_t kPresetClickTimeoutMs = 1000;
static constexpr uint32_t kSaveSelectTimeoutMs = 3000;
static constexpr uint32_t kPresetLedStepMs = 200;
static constexpr uint32_t kSaveLedStepMs = 80;
static constexpr uint32_t kSaveSelectHoldMs = 700;
static constexpr uint32_t kPresetOffHoldMs = 1500;

DaisyPetal hw;
Parameter  param1, param2, param3, param4, param5, param6, expression;
Led        led1, led2;

bool bypass = true;
bool alternateMode = false;
bool expression_pressed = false;
bool use_preset = false;
bool trigger_save = false;
bool update_switches = true;
bool suppress_button2_release = false;
bool suppress_button2_preset_release = false;
bool live_alternate_save_pending = false;
bool special_switch1_latched = false;
int  special_return_switch1 = 1;
uint32_t button2_down_ms = 0;
uint32_t preset_click_start_ms = 0;
uint32_t save_select_start_ms = 0;
uint32_t led2_pattern_ms = 0;
uint32_t live_alternate_save_ms = 0;
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
float phaserAlternateKnobValues[6] = {0.0f, 0.45f, 0.35f, 0.5f, 0.65f, 0.5f};
float flangerAlternateKnobValues[6] = {0.0f, 0.45f, 0.35f, 0.5f, 0.4f, 0.5f};
float physicalKnobValues[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
float alternatePickupValues[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
bool  normalPickupActive[6] = {false, false, false, false, false, false};

EffectMode effectMode = EffectMode::ThruToFilter;
FilterMode filterMode = FilterMode::Lowpass;
ExpressionSlotMode expressionSlotMode = ExpressionSlotMode::Off;
RouteMode routeMode = RouteMode::Normal;

ExpressionHandler expHandler;
PersistentStorage<StorageData> SavedSettings(hw.seed.qspi);

SampleRateReducer srReducerL, srReducerR;
SampleRateReducer ringSrReducerL, ringSrReducerR;
MoogLadder        ladderL, ladderR;
Svf               svfL, svfR;
DSY_SDRAM_BSS FunboxPhaser phaserL;
DSY_SDRAM_BSS FunboxPhaser phaserR;
DSY_SDRAM_BSS FunboxFlanger flangerL;
DSY_SDRAM_BSS FunboxFlanger flangerR;
Oscillator        ringOsc;
EnvelopeFollower  envFollower;

float samplerate = 48000.0f;
float envInputGain = 1.0f;
float envAttack = 0.01f;
float envRelease = 0.1f;
float filterResonance = 0.2f;
float cutoffControl = 0.5f;
float ringBaseFreq = 100.0f;
float phaserLfoPhase = 0.0f;
float phaserLfoSlewL = 0.0f;
float phaserLfoSlewR = 0.0f;
float phaserLfoPrevPhaseL = 0.0f;
float phaserLfoPrevPhaseR = 0.0f;
float phaserLfoRandomL = 0.0f;
float phaserLfoRandomR = 0.0f;
uint32_t phaserRandomState = 0x12345678;
float flangerLfoPhase = 0.0f;
float flangerLfoSlewL = 0.0f;
float flangerLfoSlewR = 0.0f;
float flangerLfoPrevPhaseL = 0.0f;
float flangerLfoPrevPhaseR = 0.0f;
float flangerLfoRandomL = 0.0f;
float flangerLfoRandomR = 0.0f;
uint32_t flangerRandomState = 0x87654321;

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

float ApplyEnvDeadbandDepth(float base, float amount, float env, float depth)
{
    return Clamp01(base + BipolarDeadband(amount) * env * depth);
}

float EnvScale(float amount, float env, float depth)
{
    return powf(2.0f, Bipolar(amount) * env * depth);
}

float EnvScaleDeadband(float amount, float env, float depth)
{
    return powf(2.0f, BipolarDeadband(amount) * env * depth);
}

float PhaserLfo(float phase)
{
    static constexpr float kTwoPi = 6.28318530718f;
    phase -= floorf(phase);
    return sinf(phase * kTwoPi);
}

int PhaserLfoMode(float character)
{
    int mode = static_cast<int>(Clamp01(character) * 5.0f);
    return mode > 4 ? 4 : mode;
}

float NextRandom(uint32_t& state)
{
    state = state * 1664525UL + 1013904223UL;
    const uint32_t bits = (state >> 8) & 0x00FFFFFFUL;
    return (static_cast<float>(bits) / 8388607.5f) - 1.0f;
}

float PhaserLfoTarget(float phase, int mode, float random_value)
{
    phase -= floorf(phase);
    switch(mode)
    {
        case 1: return phase * 2.0f - 1.0f; // ramp
        case 2: return 1.0f - phase * 2.0f; // saw
        case 3: return phase < 0.5f ? 1.0f : -1.0f; // square
        case 4: return random_value; // sample and hold
        default:
        {
            const float t = -1.0f + 2.0f * phase;
            return 2.0f * (fabsf(t) - 0.5f); // triangle
        }
    }
}

float ProcessPhaserLfo(float phase,
                       int mode,
                       float& prev_phase,
                       float& random_value,
                       float& slew_state,
                       float slew_amount,
                       uint32_t& random_state)
{
    phase -= floorf(phase);
    if(phase < prev_phase && mode == 4)
        random_value = NextRandom(random_state);
    prev_phase = phase;

    const float target = PhaserLfoTarget(phase, mode, random_value);
    if(mode == 0)
    {
        slew_state = target;
        return target;
    }

    const float slew = ExpMap(Clamp01(slew_amount), 0.0004f, 0.08f);
    slew_state += (target - slew_state) * slew;
    return slew_state;
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
void UpdateSwitch2();
void UpdateSwitch3();

int ActiveExpressionSlot()
{
    if(switchValues[1] == 0)
        return 0;
    if(switchValues[1] == 2)
        return 1;
    return -1;
}

void LoadExpressionAssignment(int slot)
{
    StorageData& data = SavedSettings.GetSettings();
    if(slot < 0 || slot >= 2 || data.magic != kStorageMagic
       || data.version != kStorageVersion)
        return;

    expHandler.LoadState(data.expression_assignments[slot]);
}

void StoreExpressionAssignment(int slot)
{
    if(slot < 0 || slot >= 2)
        return;

    StorageData& data = SavedSettings.GetSettings();
    data.magic = kStorageMagic;
    data.version = kStorageVersion;
    expHandler.SaveState(data.expression_assignments[slot]);
    trigger_save = true;
}

void RequestLiveAlternateSave()
{
    live_alternate_save_pending = true;
    live_alternate_save_ms = System::GetNow();
}

void StoreLiveAlternateValues()
{
    StorageData& data = SavedSettings.GetSettings();
    data.magic = kStorageMagic;
    data.version = kStorageVersion;
    for(int i = 0; i < 6; i++)
        data.live_alternate_knobs[i] = alternateKnobValues[i];
    trigger_save = true;
    live_alternate_save_pending = false;
}

bool IsSpecialRoute()
{
    return routeMode == RouteMode::Phaser || routeMode == RouteMode::Flanger;
}

int NormalPresetBank()
{
    return switchValues[0] == 0 ? 0 : (switchValues[0] == 2 ? 2 : 1);
}

Settings& ActivePresetBank(StorageData& data, int index)
{
    if(routeMode == RouteMode::Phaser)
        return data.phaser_presets[index];
    if(routeMode == RouteMode::Flanger)
        return data.flanger_presets[index];
    return data.presets[NormalPresetBank()][index];
}

float* ActiveSpecialAlternateValues()
{
    if(routeMode == RouteMode::Phaser)
        return phaserAlternateKnobValues;
    if(routeMode == RouteMode::Flanger)
        return flangerAlternateKnobValues;
    return alternateKnobValues;
}

void ApplyPreset(int index)
{
    StorageData& data = SavedSettings.GetSettings();
    if(data.magic != kStorageMagic || data.version != kStorageVersion)
        return;

    Settings& s = ActivePresetBank(data, index);
    for(int i = 0; i < 6; i++)
    {
        knobValues[i] = s.knobs[i];
        if(routeMode == RouteMode::Phaser)
            phaserAlternateKnobValues[i] = s.alternate_knobs[i];
        else if(routeMode == RouteMode::Flanger)
            flangerAlternateKnobValues[i] = s.alternate_knobs[i];
        else
        {
            // Normal Shift K4-K6 are global env follower settings, not preset data.
            if(i < 3)
                alternateKnobValues[i] = s.alternate_knobs[i];
        }
    }
    switchValues[2] = s.switches[2];
    for(int i = 0; i < 4; i++)
        dipValues[i] = s.dips[i];
    use_preset = true;
    active_preset = index;
    led2_phase = 0;
    led2_pattern_ms = System::GetNow();
    CaptureNormalPickup();
    CaptureAlternatePickup();
    led1.Set(bypass ? 0.0f : 1.0f);
    UpdateSwitch1();
    UpdateSwitch3();
    UpdateSwitch2();
}

void StorePreset(int index)
{
    StorageData& data = SavedSettings.GetSettings();
    data.magic = kStorageMagic;
    data.version = kStorageVersion;
    Settings& s = ActivePresetBank(data, index);
    for(int i = 0; i < 6; i++)
    {
        s.knobs[i] = knobValues[i];
        if(IsSpecialRoute() || i < 3)
            s.alternate_knobs[i] = IsSpecialRoute() ? ActiveSpecialAlternateValues()[i]
                                                    : alternateKnobValues[i];
    }
    s.switches[2] = switchValues[2];
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

void UpdateSwitch2()
{
    if(switchValues[1] == 0)
    {
        expressionSlotMode = ExpressionSlotMode::Slot1;
        LoadExpressionAssignment(0);
    }
    else if(switchValues[1] == 2)
    {
        expressionSlotMode = ExpressionSlotMode::Slot2;
        LoadExpressionAssignment(1);
    }
    else
    {
        expressionSlotMode = ExpressionSlotMode::Off;
        expHandler.Reset();
    }

    if(!special_switch1_latched)
        routeMode = RouteMode::Normal;
}

void UpdateLed2()
{
    if(expHandler.isExpressionSetMode())
        return;

    if(save_select_active)
    {
        led2.Set(0.75f);
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
        const int phases = pulses * 2 + 2;
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
       && !hw.switches[Funbox::FOOTSWITCH_2].Pressed() && !expression_pressed)
    {
        alternateMode = true;
        CaptureAlternatePickup();
        led1.Set(0.5f);
    }

    if(hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() >= 500
       && hw.switches[Funbox::FOOTSWITCH_2].TimeHeldMs() >= 500
       && !expression_pressed)
    {
        const int expression_slot = ActiveExpressionSlot();
        if(expression_slot >= 0)
        {
            const bool was_set_mode = expHandler.isExpressionSetMode();
            expHandler.ToggleExpressionSetMode();
            if(was_set_mode && !expHandler.isExpressionSetMode())
                StoreExpressionAssignment(expression_slot);

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
        }
        else
        {
            expHandler.Reset();
            led1.Set(bypass ? 0.0f : 1.0f);
            led2.Set(use_preset ? 1.0f : 0.0f);
        }
        expression_pressed = true;
        suppress_button2_release = true;
        pending_preset_clicks = 0;
    }

    if(hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() >= 2000
       && hw.switches[Funbox::FOOTSWITCH_2].TimeHeldMs() >= 2000)
    {
        expHandler.Reset();
        StoreExpressionAssignment(ActiveExpressionSlot());
        led1.Set(bypass ? 0.0f : 1.0f);
        led2.Set(use_preset ? 1.0f : 0.0f);
        suppress_button2_release = true;
        pending_preset_clicks = 0;
    }

    if(hw.switches[Funbox::FOOTSWITCH_2].RisingEdge())
    {
        button2_down_ms = System::GetNow();
    }

    if(hw.switches[Funbox::FOOTSWITCH_2].Pressed()
       && hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() <= 50
       && !expression_pressed)
    {
        const uint32_t held_ms = System::GetNow() - button2_down_ms;
        if(held_ms >= kPresetOffHoldMs)
        {
            if(save_select_active)
            {
                save_select_active = false;
                pending_save_clicks = 0;
            }
            DisablePreset();
            led2.Set(0.0f);
            suppress_button2_preset_release = true;
        }
        else if(held_ms >= kSaveSelectHoldMs && !save_select_active
                && !suppress_button2_preset_release)
        {
            save_select_active = true;
            pending_preset_clicks = 0;
            pending_save_clicks = 0;
            save_select_start_ms = System::GetNow();
            led2.Set(0.75f);
        }
    }

    if(hw.switches[Funbox::FOOTSWITCH_2].FallingEdge() && suppress_button2_release)
    {
        suppress_button2_release = false;
        return;
    }

    if(hw.switches[Funbox::FOOTSWITCH_2].FallingEdge() && suppress_button2_preset_release)
    {
        suppress_button2_preset_release = false;
        return;
    }

    if(hw.switches[Funbox::FOOTSWITCH_2].FallingEdge() && !expression_pressed)
    {
        const uint32_t held_ms = System::GetNow() - button2_down_ms;
        if(held_ms >= kSaveSelectHoldMs
                && hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() <= 50)
        {
            return;
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
        const int previous_switch1 = switchValues[0];
        switchValues[0] = SwitchValue(pswitch1[0], pswitch1[1]);
        UpdateSwitch1();
        if(changed1)
        {
            if(special_switch1_latched)
            {
                special_switch1_latched = false;
                switchValues[0] = special_return_switch1;
                UpdateSwitch1();
                UpdateSwitch2();
                ApplyPreset(active_preset >= 0 ? active_preset : 0);
            }
            else if(alternateMode && (switchValues[0] == 0 || switchValues[0] == 2))
            {
                special_switch1_latched = true;
                special_return_switch1 = previous_switch1;
                routeMode = switchValues[0] == 0 ? RouteMode::Phaser : RouteMode::Flanger;
                UpdateSwitch2();
                ApplyPreset(0);
            }
            else if(!alternateMode && routeMode == RouteMode::Normal)
            {
                ApplyPreset(active_preset >= 0 ? active_preset : 0);
            }
        }
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

float ProcessVoice(
    float in, bool left, float bits, float sr_reduce, float cutoff, float resonance, float ring)
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

    return ProcessFilter(x, cutoff, resonance, left);
}

float ProcessPhaserVoice(float in,
                         bool left,
                         float freq_control,
                         float feedback_control,
                         float env_to_freq,
                         float env_to_feedback,
                         float depth_control,
                         float env_to_depth,
                         float stages_control,
                         float lfo_signal,
                         float env)
{
    float freq = ExpMap(freq_control, 40.0f, 8000.0f);
    freq *= EnvScaleDeadband(env_to_freq, env, 7.0f);

    float feedback = Clamp01(feedback_control + BipolarDeadband(env_to_feedback) * env);
    float depth = Clamp01(depth_control + BipolarDeadband(env_to_depth) * env);
    freq *= powf(2.0f, lfo_signal * depth * 1.5f);

    FunboxPhaser& p = left ? phaserL : phaserR;
    const int stages = 2 + static_cast<int>(Clamp01(stages_control) * 3.999f) * 2;
    p.SetPoles(stages);
    p.SetFreq(fclamp(freq, 20.0f, 16000.0f));
    p.SetFeedback(feedback);

    const float wet = p.Process(in) * 0.25f;
    return fclamp((in * 0.35f) + wet, -1.0f, 1.0f);
}

float ProcessFlangerVoice(float in,
                          bool left,
                          float delay_control,
                          float feedback_control,
                          float env_to_delay,
                          float env_to_feedback,
                          float depth_control,
                          float env_to_depth,
                          float lfo_signal,
                          float env)
{
    float center_ms = ExpMap(delay_control, 0.35f, 45.0f);
    center_ms *= EnvScaleDeadband(env_to_delay, env, 3.0f);

    const float feedback = BipolarDeadband(feedback_control)
                           + BipolarDeadband(env_to_feedback) * env;
    const float depth = Clamp01(depth_control + BipolarDeadband(env_to_depth) * env);
    const float sweep = powf(2.0f, lfo_signal * depth * 2.0f);
    const float delay_ms = fclamp(center_ms * sweep, 0.25f, 48.0f);

    FunboxFlanger& f = left ? flangerL : flangerR;
    f.SetDelayMs(delay_ms);
    f.SetFeedback(feedback);
    f.SetWet(0.62f);
    return f.Process(in);
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
        float* activeAlternateValues = ActiveSpecialAlternateValues();
        for(int i = 0; i < 6; i++)
        {
            if(KnobMoved(alternatePickupValues[i], physicalKnobValues[i]))
            {
                alternatePickupValues[i] = physicalKnobValues[i];
                activeAlternateValues[i] = physicalKnobValues[i];
                if(!IsSpecialRoute())
                    RequestLiveAlternateSave();
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

    filterResonance = alternateKnobValues[2];
    envInputGain = 1.0f + alternateKnobValues[3] * 18.0f;
    if(IsSpecialRoute())
        envInputGain *= 3.0f;
    envAttack = ExpMap(alternateKnobValues[4], 0.001f, 1.0f);
    envRelease = ExpMap(alternateKnobValues[5], 0.01f, 3.0f);

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
        if(!alternateMode)
            cutoffControl = k3;

        float modCutoffControl = cutoffControl;
        float modRingFreq = ringBaseFreq;

        if(routeMode == RouteMode::Phaser)
        {
            const float* phaserAlt = phaserAlternateKnobValues;
            const float phaserEnv = env;
            const float lfoSpeedControl = ApplyEnvDeadband(k2, k5, phaserEnv);
            float       lfoSpeed = ExpMap(lfoSpeedControl, 0.02f, 8.0f);
            lfoSpeed = fclamp(lfoSpeed, 0.005f, 20.0f);
            phaserLfoPhase += lfoSpeed / samplerate;
            if(phaserLfoPhase >= 1.0f)
                phaserLfoPhase -= floorf(phaserLfoPhase);

            const float spread = Clamp01(phaserAlt[0]
                                         + BipolarDeadband(phaserAlt[3]) * phaserEnv);
            const int   lfoMode = PhaserLfoMode(phaserAlt[1]);
            const float lfoL = ProcessPhaserLfo(phaserLfoPhase,
                                                lfoMode,
                                                phaserLfoPrevPhaseL,
                                                phaserLfoRandomL,
                                                phaserLfoSlewL,
                                                0.5f,
                                                phaserRandomState);
            const float lfoR = ProcessPhaserLfo(phaserLfoPhase + (spread * 0.5f),
                                                lfoMode,
                                                phaserLfoPrevPhaseR,
                                                phaserLfoRandomR,
                                                phaserLfoSlewR,
                                                0.5f,
                                                phaserRandomState);
            const float outL = ProcessPhaserVoice(inL,
                                                  true,
                                                  k1,
                                                  phaserAlt[2],
                                                  k4,
                                                  phaserAlt[5],
                                                  k3,
                                                  k6,
                                                  phaserAlt[4],
                                                  lfoL,
                                                  phaserEnv);
            const float outR = ProcessPhaserVoice(inR,
                                                  false,
                                                  k1,
                                                  phaserAlt[2],
                                                  k4,
                                                  phaserAlt[5],
                                                  k3,
                                                  k6,
                                                  phaserAlt[4],
                                                  lfoR,
                                                  phaserEnv);
            out[0][i] = outL;
            out[1][i] = outR;
            continue;
        }

        if(routeMode == RouteMode::Flanger)
        {
            const float* flangerAlt = flangerAlternateKnobValues;
            const float flangerEnv = env;
            const float lfoSpeedControl = ApplyEnvDeadband(k2, k5, flangerEnv);
            float       lfoSpeed = ExpMap(lfoSpeedControl, 0.02f, 8.0f);
            lfoSpeed = fclamp(lfoSpeed, 0.005f, 20.0f);
            flangerLfoPhase += lfoSpeed / samplerate;
            if(flangerLfoPhase >= 1.0f)
                flangerLfoPhase -= floorf(flangerLfoPhase);

            const float spread = Clamp01(flangerAlt[0]
                                         + BipolarDeadband(flangerAlt[3]) * flangerEnv);
            const int   lfoMode = PhaserLfoMode(flangerAlt[1]);
            const float slew = flangerAlt[4];
            const float lfoL = ProcessPhaserLfo(flangerLfoPhase,
                                                lfoMode,
                                                flangerLfoPrevPhaseL,
                                                flangerLfoRandomL,
                                                flangerLfoSlewL,
                                                slew,
                                                flangerRandomState);
            const float lfoR = ProcessPhaserLfo(flangerLfoPhase + (spread * 0.5f),
                                                lfoMode,
                                                flangerLfoPrevPhaseR,
                                                flangerLfoRandomR,
                                                flangerLfoSlewR,
                                                slew,
                                                flangerRandomState);
            const float outL = ProcessFlangerVoice(inL,
                                                   true,
                                                   k1,
                                                   flangerAlt[2],
                                                   k4,
                                                   flangerAlt[5],
                                                   k3,
                                                   k6,
                                                   lfoL,
                                                   flangerEnv);
            const float outR = ProcessFlangerVoice(inR,
                                                   false,
                                                   k1,
                                                   flangerAlt[2],
                                                   k4,
                                                   flangerAlt[5],
                                                   k3,
                                                   k6,
                                                   lfoR,
                                                   flangerEnv);
            out[0][i] = outL;
            out[1][i] = outR;
            continue;
        }

        if(!alternateMode)
        {
            if(effectMode == EffectMode::RingMod)
                modRingFreq = ringBaseFreq * EnvScaleDeadband(k4, env, ringBaseFreq * 0.5f);
            else
                modBits = ApplyEnvDeadband(bits, k4, env);
            modSr = ApplyEnvDeadband(sr_reduce, k5, env);
            modCutoffControl = ApplyEnvDeadbandDepth(cutoffControl, k6, env, 2.5f);
        }

        const float cutoff = ExpMap(modCutoffControl, 40.0f, 16000.0f);
        ringOsc.SetFreq(fclamp(modRingFreq, 0.1f, 6000.0f));
        const float ring = ringOsc.Process() * 2.0f;
        const float outL = ProcessVoice(inL, true, modBits, modSr, cutoff, resonance, ring);
        const float outR = ProcessVoice(inR, false, modBits, modSr, cutoff, resonance, ring);

        out[0][i] = outL;
        out[1][i] = outR;
    }
}

int main(void)
{
    hw.Init();
    samplerate = hw.AudioSampleRate();
    hw.SetAudioBlockSize(16);

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
    phaserL.Init(samplerate);
    phaserR.Init(samplerate);
    flangerL.Init(samplerate);
    flangerR.Init(samplerate);
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
    for(int i = 0; i < 6; i++)
        defaults.live_alternate_knobs[i] = defaultAlternate[i];
    for(int i = 0; i < 5; i++)
    {
        for(int bank = 0; bank < 3; bank++)
        {
            Settings normalPreset = defaultPreset;
            normalPreset.switches[0] = bank == 0 ? 0 : (bank == 2 ? 2 : 1);
            defaults.presets[bank][i] = normalPreset;
        }
        Settings phaserPreset = defaultPreset;
        phaserPreset.switches[1] = 0;
        phaserPreset.knobs[0] = 0.45f;
        phaserPreset.knobs[1] = 0.25f;
        phaserPreset.knobs[2] = 0.65f;
        phaserPreset.knobs[5] = 0.5f;
        phaserPreset.alternate_knobs[0] = 0.0f;
        phaserPreset.alternate_knobs[1] = 0.45f;
        phaserPreset.alternate_knobs[2] = 0.35f;
        phaserPreset.alternate_knobs[3] = 0.5f;
        phaserPreset.alternate_knobs[4] = 0.65f;
        phaserPreset.alternate_knobs[5] = 0.5f;
        defaults.phaser_presets[i] = phaserPreset;

        Settings flangerPreset = phaserPreset;
        flangerPreset.knobs[0] = 0.35f;
        flangerPreset.knobs[1] = 0.2f;
        flangerPreset.knobs[2] = 0.6f;
        flangerPreset.alternate_knobs[2] = 0.2f;
        flangerPreset.alternate_knobs[4] = 0.45f;
        defaults.flanger_presets[i] = flangerPreset;
    }
    for(int slot = 0; slot < 2; slot++)
    {
        for(int i = 0; i < 6; i++)
        {
            defaults.expression_assignments[slot].expression_up[i] = 0.0f;
            defaults.expression_assignments[slot].expression_down[i] = 0.0f;
            defaults.expression_assignments[slot].expression_knob_active[i] = false;
        }
        defaults.expression_assignments[slot].expressionEngaged = false;
    }
    SavedSettings.Init(defaults);
    if(SavedSettings.GetSettings().magic != kStorageMagic
       || SavedSettings.GetSettings().version != kStorageVersion)
    {
        SavedSettings.RestoreDefaults();
    }
    for(int i = 0; i < 6; i++)
    {
        alternateKnobValues[i] = SavedSettings.GetSettings().live_alternate_knobs[i];
        alternatePickupValues[i] = alternateKnobValues[i];
    }

    UpdateSwitch1();
    UpdateSwitch3();
    UpdateSwitch2();
    

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        if(live_alternate_save_pending
           && System::GetNow() - live_alternate_save_ms >= 750)
        {
            StoreLiveAlternateValues();
        }

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
