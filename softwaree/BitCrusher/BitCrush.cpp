#include "daisy_petal.h"
#include "daisysp.h"
#include "funbox.h"
#include "expressionHandler.h"

#include "comb.h"
#include "envelopeFollower.h"
#include "moogladder.h"
#include "oscillator.h"
#include "sampleratereducer.h"
#include "soap.h"
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
    Ladder,
    Bandpass,
    Comb,
};

enum class CombSubMode
{
    Comb,
    Hipass,
    Notch
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
    Settings presets[4];

    bool operator!=(const StorageData& a) const
    {
        if(a.magic != magic || a.version != version)
            return true;
        for(int i = 0; i < 4; i++)
        {
            if(a.presets[i] != presets[i])
                return true;
        }
        return false;
    }
};

static constexpr uint32_t kStorageMagic = 0x42435434; // "BCT4"
static constexpr uint32_t kStorageVersion = 2;
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
FilterMode filterMode = FilterMode::Ladder;

CombSubMode combSubMode = CombSubMode::Comb;

ExpressionHandler expHandler;
PersistentStorage<StorageData> SavedSettings(hw.seed.qspi);

SampleRateReducer srReducerL, srReducerR;
MoogLadder        ladderL, ladderR;
Svf               svfL, svfR;
Comb              combL, combR;
::Soap            soapL, soapR;
Oscillator        ringOsc;
EnvelopeFollower  envFollower;

static constexpr size_t kCombSize = 48000;
DSY_SDRAM_BSS float combBufferL[kCombSize];
DSY_SDRAM_BSS float combBufferR[kCombSize];

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

float EnvScale(float amount, float env, float depth)
{
    return powf(2.0f, Bipolar(amount) * env * depth);
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

void ApplyPreset(int index)
{
    StorageData& data = SavedSettings.GetSettings();
    if(data.magic != kStorageMagic || data.version != kStorageVersion)
        return;

    Settings& s = data.presets[index];
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
    bypass = false;
    active_preset = index;
    led2_phase = 0;
    led2_pattern_ms = System::GetNow();
    CaptureNormalPickup();
    CaptureAlternatePickup();
    led1.Set(1.0f);
    UpdateSwitch1();
    UpdateSwitch3();
    UpdateSwitch2();
}

void StorePreset(int index)
{
    StorageData& data = SavedSettings.GetSettings();
    data.magic = kStorageMagic;
    data.version = kStorageVersion;
    Settings& s = data.presets[index];
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
    bypass = false;
    active_preset = index;
    led2_phase = 0;
    led2_pattern_ms = System::GetNow();
    led1.Set(1.0f);
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

// Ersetzen Sie die UpdateSwitch3()-Funktion durch diese:
void UpdateSwitch3()
{
    if(switchValues[2] == 0)
        filterMode = FilterMode::Ladder;
    else if(switchValues[2] == 2)
        filterMode = FilterMode::Comb;
    else
        filterMode = FilterMode::Bandpass;
}

// Fügen Sie DIESE Funktion hinzu:
void UpdateSwitch2()
{
    if(filterMode != FilterMode::Comb)
    {
        combSubMode = CombSubMode::Comb;
        return;
    }
    
    if(switchValues[1] == 0)
        combSubMode = CombSubMode::Comb;
    else if(switchValues[1] == 2)
        combSubMode = CombSubMode::Notch;
    else
        combSubMode = CombSubMode::Hipass;
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
                if(pending_save_clicks < 4)
                    pending_save_clicks++;
                save_select_start_ms = System::GetNow();
            }
            else
            {
                if(pending_preset_clicks < 4)
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
        ApplyPreset(pending_preset_clicks - 1);
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
    UpdateSwitch2();  // <-- HIER fehlte der Aufruf!
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
    UpdateSwitch2();  // <-- Damit Switch 2 aktualisiert wird wenn FilterMode wechselt
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

// Ersetzen Sie ProcessFilter() mit dieser Version:
float ProcessFilter(float in, float cutoff, float resonance, bool left)
{
    if(filterMode == FilterMode::Ladder)
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

    if(combSubMode == CombSubMode::Hipass)
    {
        Svf& f = left ? svfL : svfR;
        f.SetFreq(cutoff);
        f.SetRes(resonance);
        f.SetDrive(0.5f);
        f.Process(in);
        return f.High();
    }

    if(combSubMode == CombSubMode::Notch)
    {
        ::Soap& f = left ? soapL : soapR;
        f.SetCenterFreq(cutoff);
        f.SetFilterBandwidth(20.0f + (1.0f - resonance) * 4000.0f);
        f.Process(in);
        return f.Bandreject();
    }

    // Comb-Modus mit Submodes
    Comb& f = left ? combL : combR;
    f.SetFreq(cutoff);
    f.SetRevTime(0.15f + resonance * 0.85f);
    const float wet = f.Process(in) * 0.30f;
    const float dry = in * 0.5f;
    
    switch(combSubMode)
    {
        case CombSubMode::Comb:
            return fclamp(dry + wet, -1.0f, 1.0f);
        case CombSubMode::Hipass:
            return fclamp(dry - wet, -1.0f, 1.0f);
        case CombSubMode::Notch:
            // Notch: Trocken, aber mit Frequenz-abhängiger Auslöschung
            // Einfache Implementierung: Mischung aus HP und LP
            return fclamp(dry * 0.7f + wet * 0.3f, -1.0f, 1.0f);
        default:
            return fclamp(dry + wet, -1.0f, 1.0f);
    }
}

float ProcessVoice(
    float in, bool left, float bits, float sr_reduce, float cutoff, float resonance, float ring)
{
    float x = in;

    if(effectMode == EffectMode::RingMod)
    {
        x *= ring;
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
        for(int i = 2; i < 6; i++)
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

    filterResonance = alternateKnobValues[2];
    envInputGain = 1.0f + alternateKnobValues[3] * 18.0f;
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
        const int wf = static_cast<int>(fclamp(k2, 0.0f, 0.999f) * 5.0f);
        const uint8_t waveforms[5] = {Oscillator::WAVE_SIN,
                                      Oscillator::WAVE_TRI,
                                      Oscillator::WAVE_POLYBLEP_SAW,
                                      Oscillator::WAVE_POLYBLEP_SQUARE,
                                      Oscillator::WAVE_RAMP};
        ringOsc.SetWaveform(waveforms[wf]);
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

        if(!alternateMode)
        {
            if(effectMode == EffectMode::RingMod)
                modRingFreq = ringBaseFreq * EnvScale(k4, env, ringBaseFreq * 0.5f);
            else
                modBits = ApplyEnv(bits, k4, env);
            modSr = ApplyEnv(sr_reduce, k5, env);
            modCutoffControl = ApplyEnv(cutoffControl, k6, env);
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
    ladderL.Init(samplerate);
    ladderR.Init(samplerate);
    svfL.Init(samplerate);
    svfR.Init(samplerate);
    soapL.Init(samplerate);
    soapR.Init(samplerate);
    combL.Init(samplerate, combBufferL, kCombSize);
    combR.Init(samplerate, combBufferR, kCombSize);
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
    for(int i = 0; i < 4; i++)
        defaults.presets[i] = defaultPreset;
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
