// Neptune Reverb/Delay Pedal

#include "daisy_petal.h"
#include "daisysp.h"
#include "funbox.h"
#include "delayline_reverse.h"
#include "delayline_oct.h"
#include "expressionHandler.h"
#include "tempo.h"  

#include "../CloudSeed/Default.h"
#include "../CloudSeed/ReverbController.h"
#include "../CloudSeed/FastSin.h"
#include "../CloudSeed/AudioLib/ValueTables.h"
#include "../CloudSeed/AudioLib/MathDefs.h"

#define NEPTUNE_TEMPO_DEBUG 1
#define NEPTUNE_RAW_MIDI_DEBUG 0

using namespace daisy;
using namespace daisysp;
using namespace funbox; 

// Declare a local daisy_petal for hardware access
DaisyPetal hw;
::daisy::Parameter rsize, modify, mix, filter, delayTime, delayFDBK, expression;
bool      bypass;
Led led1, led2;

constexpr float kNeptuneInputPad = 0.75f;
constexpr float kLooperWritePad = 0.65f;
constexpr float kLooperMonitorPad = 0.75f;

//Tone tone;       // Low Pass
Tone toneOctLP;       // Low Pass for octave delay
//ATone toneHP;    // High Pass
float pfilter;

float NeptuneSoftLimit(float value)
{
    const float x = fclamp(value, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// Alternate functions
bool alternateMode;

float delayMix;   // Mix vs. Delay Level
float reverbMix;
float dryMix;
float wetMix;

float modAlt;  // Secondary modify function
float preDelayAlt;

bool force_reset;

float  del_FDBK_Override;

float ramp;
bool mode_changed;
bool triggerMode;

bool            pswitch1[2], pswitch2[2], pswitch3[2], pdip[4];
int             switch1[2], switch2[2], switch3[2], dip[4];

// Initialize "previous" p values
float psize, pmodify, pdelayTime, pmix;


// Expression
ExpressionHandler expHandler;
bool expression_pressed;

// Midi
bool midi_control[6]; //  just knobs for now
float pknobValues[6]; // Used for Midi control logic

float knobValues[6];
int toggleValues[3];
bool dipValues[4];


CloudSeed::ReverbController* reverb = 0;


// This is used in the modified CloudSeed code for allocating
// delay line memory to SDRAM (64MB available on Daisy)
#define CUSTOM_POOL_SIZE (8*1024*1024)
DSY_SDRAM_BSS char custom_pool[CUSTOM_POOL_SIZE];
size_t pool_index = 0;
int allocation_count = 0;
void* custom_pool_allocate(size_t size)
{
        if (pool_index + size >= CUSTOM_POOL_SIZE)
        {
                return 0;
        }
        void* ptr = &custom_pool[pool_index];
        pool_index += size;
        allocation_count++;
        return ptr;
}

// Delay
#define MAX_DELAY static_cast<size_t>(48000 * 8.0f) // 8 second max delay
#define MAX_DELAY_REV static_cast<size_t>(48000 * 16.0f) // double for reverse

DelayLineReverse<float, MAX_DELAY_REV> DSY_SDRAM_BSS delayLineRevLeft;
DelayLineOct<float, MAX_DELAY> DSY_SDRAM_BSS delayLineOctLeft;
DelayLineReverse<float, MAX_DELAY_REV> DSY_SDRAM_BSS delayLineRevRight;
DelayLineOct<float, MAX_DELAY> DSY_SDRAM_BSS delayLineOctRight;


struct delay
{
    DelayLineOct<float, MAX_DELAY> *del;
    DelayLineReverse<float, MAX_DELAY_REV> *delreverse;
    float                        currentDelay;
    float                        delayTarget;
    float                        feedback = 0.0;
    float                        active = false;
    bool                         reverseMode = false;
    float                        level = 1.0;      // Level multiplier of output, added for stereo modulation
    float                        level_reverse = 1.0;      // Level multiplier of output, added for stereo modulation
    bool                         dual_delay = false;
    bool                         secondTapOn = false;
    
    float Process(float in)
    {
        //set delay times
        fonepole(currentDelay, delayTarget, .0002f);
        del->SetDelay(currentDelay);
        delreverse->SetDelay1(currentDelay);

        float del_read = del->Read();

        float read_reverse = delreverse->ReadRev(); // REVERSE

        float read = toneOctLP.Process(del_read);  // LP filter, tames harsh high frequencies on octave, has fading effect for normal/reverse

        float secondTap = 0.0;
        if (secondTapOn) {
            secondTap = del->ReadSecondTap();
        }
        //float read2 = delreverse->ReadFwd();
        if (active) {
            const float write = NeptuneSoftLimit((feedback * read) + in);
            del->Write(write);
            delreverse->Write(write);  // Writing the read from fwd/oct delay line allows for combining oct and rev for reverse octave!
            //delreverse->Write((feedback * read2) + in); 
        } else {
            const float write = NeptuneSoftLimit(feedback * read);
            del->Write(write); // if not active, don't write any new sound to buffer
            delreverse->Write(write);
            //delreverse->Write((feedback * read2));
        }

        if (dual_delay) {
            return read_reverse * level_reverse * 0.5 + (read + secondTap) * level * 0.5; // Half the volume to keep total level consistent
        } else if (reverseMode) {
            return read_reverse * level_reverse;
        } else {
            return (read + secondTap) * level;
        }
    }
};


// Left channel
delay             delayL;
delay             delayR;  


// Setting Struct containing parameters we want to save to flash
// Using the persistent storage example found on the Daisy Forum:
//   https://forum.electro-smith.com/t/saving-values-to-flash-memory-using-persistentstorage-class-on-daisy-pod/4306
struct Settings {

        float knobs[6]; // TODO Add values for alternate parameters
        int toggles[3];
        bool dips[4];

	//Overloading the != operator
	//This is necessary as this operator is used in the PersistentStorage source code
	bool operator!=(const Settings& a) const {
        return !(a.knobs[0]==knobs[0] && a.knobs[1]==knobs[1] && a.knobs[2]==knobs[2] && a.knobs[3]==knobs[3] && a.knobs[4]==knobs[4] && a.knobs[5]==knobs[5] && a.toggles[0]==toggles[0] && a.toggles[1]==toggles[1] && a.toggles[2]==toggles[2] && a.dips[0]==dips[0] && a.dips[1]==dips[1] && a.dips[2]==dips[2] && a.dips[3]==dips[3]);
        //return !(a.p1==p1 && a.p2==p2 && a.p3==p3 && a.p4==p4 && a.p5==p5 && a.p6==p6);
    }
};

//Persistent Storage Declaration. Using type Settings and passed the devices qspi handle
PersistentStorage<Settings> SavedSettings(hw.seed.qspi);
bool use_preset = false;
bool trigger_save = false;
int blink = 100;
bool save_check = false;
bool update_switches;
uint32_t footswitch2_down_ms = 0;
bool footswitch2_long_handled = false;
float tempo_division_knob = 0.5f;
int tempo_debug_blink = 0;
uint8_t midi_clock_led_count = 0;
uint8_t midi_clock_serial_count = 0;
uint16_t raw_midi_debug_count = 0;
uint32_t midi_led_pulse_until_ms = 0;

enum class LooperState
{
    Off,
    Armed,
    WaitingStart,
    Recording,
    WaitingEnd,
    Playing,
};

enum class LooperRoute
{
    OpDryOdDry,
    OpWetOdDry,
    OpDryOdWet,
    OpWetOdWet,
    LoopIntoFxResample,
};

static constexpr uint32_t kLooperLongPressMs = 1500;
static constexpr uint32_t kLooperHalfClickMs = 200;
static constexpr uint32_t kLooperLedAckTicks = 120;
static constexpr uint32_t kLooperStartBlinkTicks = 20;
static constexpr uint32_t kOverdubFadeSamples = 480;
static constexpr size_t kLooperMinRecordSamples = 4800;
static constexpr size_t kLooperMaxSamples = 48000 * 30;
DSY_SDRAM_BSS float loopBufferL[kLooperMaxSamples];
DSY_SDRAM_BSS float loopBufferR[kLooperMaxSamples];

LooperState looper_state = LooperState::Off;
LooperRoute looper_route = LooperRoute::OpDryOdDry;
uint32_t looper_start_clock = 0;
uint32_t looper_end_clock = 0;
size_t looper_write = 0;
size_t looper_read = 0;
size_t looper_length = 0;
size_t looper_repeat_source_length = 0;
float looper_route_knob = 0.0f;
float looper_feedback_knob = 0.95f;
float looper_fx_time_knob = 0.5f;
float looper_fx_feedback_knob = 0.5f;
uint32_t overdub_fade_remaining = 0;
bool previous_overdub_enabled = false;
bool hold_k3_after_looper = false;
bool hold_k6_after_looper = false;
bool hold_k3_looper_fx = false;
bool hold_k6_looper_fx = false;
bool previous_looper_fx_mode = false;
bool looper_exit_drain_active = false;
size_t looper_exit_drain_remaining = 0;
float looper_feedback_restore = 0.95f;
bool looper_length_ops_enabled = false;
bool footswitch2_looper_double_handled = false;
uint32_t looper_led_ack_ticks = 0;
bool looper_start_blink_pending = false;

enum class LooperLengthOp
{
    None,
    Halve,
    Double,
};

LooperLengthOp looper_pending_length_op = LooperLengthOp::None;

void Load() {

    //Reference to local copy of settings stored in flash
    Settings &LocalSettings = SavedSettings.GetSettings();
	
    knobValues[0] = LocalSettings.knobs[0];
    knobValues[1] = LocalSettings.knobs[1];
    knobValues[2] = LocalSettings.knobs[2];
    knobValues[3] = LocalSettings.knobs[3];
    knobValues[4] = LocalSettings.knobs[4];
    knobValues[5] = LocalSettings.knobs[5];

    toggleValues[0] = LocalSettings.toggles[0];
    toggleValues[1] = LocalSettings.toggles[1];
    toggleValues[2] = LocalSettings.toggles[2];

    dipValues[0] = LocalSettings.dips[0];
    dipValues[1] = LocalSettings.dips[1];
    dipValues[2] = LocalSettings.dips[2];
    dipValues[3] = LocalSettings.dips[3];

    use_preset = true;

}

void Save() {
    //Reference to local copy of settings stored in flash
    Settings &LocalSettings = SavedSettings.GetSettings();

    LocalSettings.knobs[0] = knobValues[0];
    LocalSettings.knobs[1] = knobValues[1];
    LocalSettings.knobs[2] = knobValues[2];
    LocalSettings.knobs[3] = knobValues[3];
    LocalSettings.knobs[4] = knobValues[4];
    LocalSettings.knobs[5] = knobValues[5];

    LocalSettings.toggles[0] = toggleValues[0];
    LocalSettings.toggles[1] = toggleValues[1];
    LocalSettings.toggles[2] = toggleValues[2];

    LocalSettings.dips[0] = dipValues[0];
    LocalSettings.dips[1] = dipValues[1];
    LocalSettings.dips[2] = dipValues[2];
    LocalSettings.dips[3] = dipValues[3];

    trigger_save = true;
}


bool knobMoved(float old_value, float new_value)
{
    float tolerance = 0.005;
    if (new_value > (old_value + tolerance) || new_value < (old_value - tolerance)) {
        return true;
    } else {
        return false;
    }
}

float FreeDelaySamples(float knob)
{
    const float min_delay = 2400.0f; // 50 ms at 48 kHz
    const float max_delay = static_cast<float>(MAX_DELAY - 1);
    return min_delay * powf(max_delay / min_delay, knob);
}

void UpdateTempoSyncMode();

void updateSwitch1()
{
    // For the different modes, write a preset in the CloudSeed code, and switch presets here
    reverb->ClearBuffers();
    int line_count = 2;

    if (toggleValues[0] == 0) { 
        reverb->initFactoryChorus();
        line_count = 1;

    } else if (toggleValues[0] == 2) {
        reverb->initFactoryRubiKaFields();
        line_count = 1;

    } else {                    
        reverb->initFactoryMediumSpace();
        line_count = 1;

    } 
    reverb->SetParameter(::Parameter2::LineCount, line_count);
    force_reset = true; // This allows the knob controlled params to be reset without moving the knobs, when a new mode is selected
}

void updateSwitch3() 
{
    // DELAY ///////////////////////
    if (toggleValues[2] == 0) {                // If switch3 is left, Normal FWD mode
        delayL.active = true;
        delayR.active = true;
        delayL.del->setOctave(false); 
        delayR.del->setOctave(false);
        delayL.reverseMode = false;
        delayR.reverseMode = false;


    } else if (toggleValues[2] == 2) {        // If switch3 is right, REV mode
        delayL.active = true;
        delayR.active = true;
        delayL.del->setOctave(false); 
        delayR.del->setOctave(false);
        delayL.reverseMode = true;
        delayR.reverseMode = true;

    } else {                           // If switch3 is middle, Octave mode
        delayL.active = true;
        delayR.active = true;
        delayL.del->setOctave(true); 
        delayR.del->setOctave(true);
        delayL.reverseMode = false;
        delayR.reverseMode = false;
    }
}

void ClearEffectBuffers()
{
    if(reverb)
        reverb->ClearBuffers();

    delayLineOctLeft.Reset();
    delayLineRevLeft.Reset();
    delayLineOctRight.Reset();
    delayLineRevRight.Reset();

    delayL.currentDelay = delayL.delayTarget;
    delayR.currentDelay = delayR.delayTarget;
    delayL.del->SetDelay(delayL.currentDelay);
    delayR.del->SetDelay(delayR.currentDelay);
    delayL.delreverse->SetDelay1(delayL.currentDelay);
    delayR.delreverse->SetDelay1(delayR.currentDelay);
}

bool LooperModeActive()
{
    return looper_state != LooperState::Off && !looper_exit_drain_active;
}

bool LooperHasLoop()
{
    if(looper_exit_drain_active)
        return false;

    return looper_state == LooperState::Playing || looper_state == LooperState::Recording
           || looper_state == LooperState::WaitingEnd;
}

LooperRoute CurrentLooperRoute()
{
    int target_index;
    if(looper_route_knob < 0.125f)
        target_index = 0;
    else if(looper_route_knob < 0.25f)
        target_index = 1;
    else if(looper_route_knob < 0.375f)
        target_index = 2;
    else if(looper_route_knob < 0.5f)
        target_index = 3;
    else
        target_index = 4;

    int current_index = 0;
    if(looper_route == LooperRoute::OpWetOdDry)
        current_index = 1;
    else if(looper_route == LooperRoute::OpDryOdWet)
        current_index = 2;
    else if(looper_route == LooperRoute::OpWetOdWet)
        current_index = 3;
    else if(looper_route == LooperRoute::LoopIntoFxResample)
        current_index = 4;

    if(target_index == current_index)
        return looper_route;

    const float boundaries[4] = {0.125f, 0.25f, 0.375f, 0.5f};
    const float hysteresis = 0.02f;
    bool should_switch = false;
    if(target_index > current_index)
        should_switch = looper_route_knob > boundaries[current_index] + hysteresis;
    else
        should_switch = looper_route_knob < boundaries[target_index] - hysteresis;

    if(should_switch)
    {
        if(target_index == 0)
            looper_route = LooperRoute::OpDryOdDry;
        else if(target_index == 1)
            looper_route = LooperRoute::OpWetOdDry;
        else if(target_index == 2)
            looper_route = LooperRoute::OpDryOdWet;
        else if(target_index == 3)
            looper_route = LooperRoute::OpWetOdWet;
        else
            looper_route = LooperRoute::LoopIntoFxResample;
    }

    return looper_route;
}

bool LooperOverplayWet(LooperRoute route)
{
    return route == LooperRoute::OpWetOdDry || route == LooperRoute::OpWetOdWet;
}

bool LooperOverdubWet(LooperRoute route)
{
    return route == LooperRoute::OpDryOdWet || route == LooperRoute::OpWetOdWet;
}

float SafeLooperFeedback(float value)
{
    return fclamp(value, 0.0f, 0.985f);
}

float SafeLooperFxFeedback(float value)
{
    return fclamp(value, 0.0f, 0.5f);
}

float LooperFxFeedbackFromRouteKnob()
{
    const float right_half = fclamp((looper_route_knob - 0.5f) * 2.0f, 0.0f, 1.0f);
    return SafeLooperFxFeedback(right_half * 0.5f);
}

void EnterLooperMode()
{
    const float physical_delay_time = delayTime.Process();
    const float physical_feedback = delayFDBK.Process();
    looper_fx_time_knob = knobValues[2];
    looper_fx_feedback_knob = knobValues[5];
    pknobValues[2] = physical_delay_time;
    pknobValues[5] = physical_feedback;
    hold_k3_looper_fx = true;
    hold_k6_looper_fx = true;
    previous_looper_fx_mode = false;

    looper_state = LooperState::Armed;
    looper_write = 0;
    looper_read = 0;
    looper_length = 0;
    looper_repeat_source_length = 0;
    looper_length_ops_enabled = false;
    looper_pending_length_op = LooperLengthOp::None;
    looper_start_blink_pending = false;
    looper_start_clock = 0;
    looper_end_clock = 0;
    use_preset = false;
}

void FinalizeLooperExit()
{
    looper_state = LooperState::Off;
    looper_exit_drain_active = false;
    looper_exit_drain_remaining = 0;
    looper_feedback_knob = looper_feedback_restore;
    looper_write = 0;
    looper_read = 0;
    looper_length = 0;
    looper_repeat_source_length = 0;
    looper_length_ops_enabled = false;
    looper_pending_length_op = LooperLengthOp::None;
    looper_start_blink_pending = false;
    looper_start_clock = 0;
    looper_end_clock = 0;
    knobValues[2] = looper_fx_time_knob;
    knobValues[5] = looper_fx_feedback_knob;
    pknobValues[2] = delayTime.Process();
    pknobValues[5] = delayFDBK.Process();
    hold_k3_after_looper = true;
    hold_k6_after_looper = true;
    hold_k3_looper_fx = false;
    hold_k6_looper_fx = false;
    previous_looper_fx_mode = false;
    previous_overdub_enabled = false;
    overdub_fade_remaining = 0;
    UpdateTempoSyncMode();
}

void ExitLooperMode()
{
    knobValues[2] = looper_fx_time_knob;
    knobValues[5] = looper_fx_feedback_knob;
    pknobValues[2] = delayTime.Process();
    pknobValues[5] = delayFDBK.Process();
    hold_k3_after_looper = true;
    hold_k6_after_looper = true;
    hold_k3_looper_fx = false;
    hold_k6_looper_fx = false;
    previous_looper_fx_mode = false;
    previous_overdub_enabled = false;
    overdub_fade_remaining = 0;
    looper_pending_length_op = LooperLengthOp::None;

    if(looper_length > 0 && looper_state == LooperState::Playing)
    {
        looper_feedback_restore = looper_feedback_knob;
        looper_feedback_knob = 0.0f;
        looper_exit_drain_active = true;
        looper_exit_drain_remaining = looper_length;
        UpdateTempoSyncMode();
    }
    else
    {
        FinalizeLooperExit();
    }
}

void DrainLooperExitSample()
{
    if(!looper_exit_drain_active)
        return;

    if(looper_length == 0 || looper_exit_drain_remaining == 0)
    {
        FinalizeLooperExit();
        return;
    }

    loopBufferL[looper_read] = 0.0f;
    loopBufferR[looper_read] = 0.0f;
    looper_read += 1;
    if(looper_read >= looper_length)
        looper_read = 0;

    looper_exit_drain_remaining -= 1;
    if(looper_exit_drain_remaining == 0)
        FinalizeLooperExit();
}

void StartLoopRecording()
{
    looper_state = LooperState::Recording;
    looper_write = 0;
    looper_read = 0;
    looper_length = 0;
    looper_repeat_source_length = 0;
    looper_length_ops_enabled = false;
    looper_pending_length_op = LooperLengthOp::None;
    looper_start_blink_pending = false;
}

void FinishLoopRecording()
{
    if(looper_write < kLooperMinRecordSamples)
    {
        return;
    }

    looper_length = looper_write;
    if(looper_length > kLooperMaxSamples)
        looper_length = kLooperMaxSamples;
    looper_read = 0;
    looper_state = LooperState::Playing;
    looper_repeat_source_length = 0;
    looper_length_ops_enabled = false;
    looper_pending_length_op = LooperLengthOp::None;
}

void HalveLoopLength()
{
    if(looper_state != LooperState::Playing || looper_length < 64)
        return;

    looper_length /= 2;
    looper_repeat_source_length = 0;
    if(looper_length < 32)
        looper_length = 32;
    if(looper_read >= looper_length)
        looper_read %= looper_length;
}

void DoubleLoopLength()
{
    if(looper_state != LooperState::Playing || looper_length == 0)
        return;

    const size_t old_length = looper_length;
    size_t new_length = old_length * 2;
    if(new_length > kLooperMaxSamples)
        new_length = kLooperMaxSamples;
    if(new_length == old_length)
        return;

    looper_repeat_source_length = old_length;
    looper_length = new_length;
}

void QueueLoopLengthOp(LooperLengthOp op)
{
    if(looper_state != LooperState::Playing || looper_length == 0 || !looper_length_ops_enabled)
        return;

    looper_pending_length_op = op;
    if(op == LooperLengthOp::Double)
        looper_led_ack_ticks = kLooperLedAckTicks;
}

void ApplyPendingLoopLengthOp()
{
    if(looper_pending_length_op == LooperLengthOp::None)
        return;

    const LooperLengthOp op = looper_pending_length_op;
    looper_pending_length_op = LooperLengthOp::None;

    if(op == LooperLengthOp::Halve)
        HalveLoopLength();
    else if(op == LooperLengthOp::Double)
        DoubleLoopLength();
}

size_t LooperPlaybackIndex()
{
    if(looper_repeat_source_length > 0 && looper_read >= looper_repeat_source_length)
        return looper_read % looper_repeat_source_length;
    return looper_read;
}

void AdvanceLooperRead()
{
    looper_read += 1;
    if(looper_read >= looper_length)
    {
        looper_read = 0;
        looper_length_ops_enabled = true;
        looper_repeat_source_length = 0;
        looper_start_blink_pending = true;
        ApplyPendingLoopLengthOp();
    }
}

void HandleLooperTapRelease(uint32_t held_ms)
{
    if(looper_state == LooperState::Armed)
    {
        if(tempo::MidiClockPresent())
        {
            looper_start_clock = tempo::MidiClockCount();
            looper_state = LooperState::WaitingStart;
        }
        else
        {
            StartLoopRecording();
        }
    }
    else if(looper_state == LooperState::Recording)
    {
        if(tempo::MidiClockPresent())
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
        // Fallback: allow manual start if quantized start is waiting too long.
        StartLoopRecording();
    }
    else if(looper_state == LooperState::WaitingEnd)
    {
        // Fallback: allow manual end if quantized end is waiting too long.
        FinishLoopRecording();
    }
    else if(looper_state == LooperState::Playing && looper_length > 0)
    {
        if(held_ms < kLooperHalfClickMs && !footswitch2_looper_double_handled)
            QueueLoopLengthOp(LooperLengthOp::Halve);
    }
}

void HandleLooperTapHeld(uint32_t held_ms)
{
    if(looper_state == LooperState::Playing && looper_length > 0
       && !footswitch2_looper_double_handled
       && held_ms >= kLooperHalfClickMs
       && held_ms < kLooperLongPressMs)
    {
        QueueLoopLengthOp(LooperLengthOp::Double);
        footswitch2_looper_double_handled = true;
    }
}

void ProcessLooperQuantize()
{
    if(looper_state == LooperState::WaitingStart)
    {
        if(!tempo::MidiClockPresent())
            StartLoopRecording();
        else if(tempo::MidiClockCount() != looper_start_clock && tempo::MidiAtSixteenth())
            StartLoopRecording();
    }
    else if(looper_state == LooperState::WaitingEnd)
    {
        if(!tempo::MidiClockPresent())
            FinishLoopRecording();
        else if(tempo::MidiClockCount() != looper_end_clock && tempo::MidiAtBar())
            FinishLoopRecording();
    }
}

void ProcessLooper(float inputL,
                   float inputR,
                   bool overdub,
                   float overdub_gain,
                   float feedback,
                   float& loopL,
                   float& loopR)
{
    loopL = 0.0f;
    loopR = 0.0f;

    ProcessLooperQuantize();

    if(looper_state == LooperState::Recording || looper_state == LooperState::WaitingEnd)
    {
        if(looper_write >= kLooperMaxSamples)
        {
            FinishLoopRecording();
            return;
        }

        loopBufferL[looper_write] = NeptuneSoftLimit(inputL * kLooperWritePad);
        loopBufferR[looper_write] = NeptuneSoftLimit(inputR * kLooperWritePad);
        looper_write += 1;

        if(looper_write >= kLooperMaxSamples)
            FinishLoopRecording();
    }
    else if(looper_state == LooperState::Playing && looper_length > 0)
    {
        const size_t playback_index = LooperPlaybackIndex();
        loopL = loopBufferL[playback_index];
        loopR = loopBufferR[playback_index];

        const float fb = SafeLooperFeedback(feedback);
        const float od_gain = overdub ? fclamp(overdub_gain, 0.0f, 1.0f) : 0.0f;
        loopBufferL[looper_read] = NeptuneSoftLimit((loopL * fb) + inputL * od_gain * kLooperWritePad);
        loopBufferR[looper_read] = NeptuneSoftLimit((loopR * fb) + inputR * od_gain * kLooperWritePad);

        AdvanceLooperRead();
    }
}

bool ReadLooperForFxResample(float liveL, float liveR, float& loopL, float& loopR)
{
    loopL = 0.0f;
    loopR = 0.0f;

    ProcessLooperQuantize();

    if(looper_state == LooperState::Recording || looper_state == LooperState::WaitingEnd)
    {
        if(looper_write >= kLooperMaxSamples)
        {
            FinishLoopRecording();
            return false;
        }

        loopBufferL[looper_write] = NeptuneSoftLimit(liveL * kLooperWritePad);
        loopBufferR[looper_write] = NeptuneSoftLimit(liveR * kLooperWritePad);
        looper_write += 1;

        if(looper_write >= kLooperMaxSamples)
            FinishLoopRecording();
        return false;
    }

    if(looper_state == LooperState::Playing && looper_length > 0)
    {
        const size_t playback_index = LooperPlaybackIndex();
        loopL = loopBufferL[playback_index];
        loopR = loopBufferR[playback_index];
        return true;
    }

    return false;
}

void WriteLooperFxResample(float fxL,
                           float fxR,
                           bool overdub,
                           float overdub_gain,
                           float feedback)
{
    if(looper_state != LooperState::Playing || looper_length == 0)
        return;

    const float fb = SafeLooperFxFeedback(feedback);
    const float od_gain = overdub ? fclamp(overdub_gain, 0.0f, 1.0f) * 0.45f : 0.0f;
    const size_t playback_index = LooperPlaybackIndex();
    const float oldL = loopBufferL[playback_index];
    const float oldR = loopBufferR[playback_index];
    loopBufferL[looper_read] = NeptuneSoftLimit((oldL * fb) + fxL * od_gain * kLooperWritePad);
    loopBufferR[looper_read] = NeptuneSoftLimit((oldR * fb) + fxR * od_gain * kLooperWritePad);

    AdvanceLooperRead();
}

void UpdateLed2Feedback()
{
    if(expHandler.isExpressionSetMode())
        return;

    if(looper_led_ack_ticks > 0)
    {
        looper_led_ack_ticks -= 1;
        led2.Set(0.0f);
        return;
    }

    if(LooperModeActive())
    {
        if(looper_start_blink_pending)
        {
            looper_start_blink_pending = false;
            looper_led_ack_ticks = kLooperStartBlinkTicks;
            led2.Set(0.0f);
            return;
        }
        led2.Set(LooperHasLoop() ? 1.0f : 0.35f);
        return;
    }

    if(blink < 100)
    {
        blink += 1;
        led2.Set(1.0f);
        return;
    }

    tempo::Process(tempo_division_knob);
    const bool tempo_pulse = tempo::Active() && tempo::LedPulseActive();
    const float base = use_preset ? 1.0f : 0.0f;

    if(tempo_pulse)
        led2.Set(use_preset ? 0.0f : 1.0f);
    else
        led2.Set(base);
}

void TriggerTempoDebugBlink()
{
#if NEPTUNE_TEMPO_DEBUG
    tempo_debug_blink = 20;
#endif
}

float Led1BaseBrightness()
{
    return bypass ? 0.0f : (alternateMode ? 0.5f : 1.0f);
}

void UpdateTempoSyncMode()
{
    tempo::SetSyncMode(tempo::SyncMode::Tap);
    midi_clock_led_count = 0;
    midi_clock_serial_count = 0;
    DaisySeed::PrintLine("sync mode: auto tap/midi");
}


void UpdateButtons()
{
    // (De-)Activate bypass and toggle LED when left footswitch is let go
    if(hw.switches[Funbox::FOOTSWITCH_1].FallingEdge())
    {
        if(!expression_pressed) {
            if (alternateMode) {
                alternateMode = false;
                force_reset = true; // force parameter reset to enforce current knob settings when leaving alternate mode
                led1.Set(Led1BaseBrightness());
            } else {
                bypass = !bypass;
                led1.Set(bypass ? 0.0f : 1.0f);
            }
        }
        expression_pressed = false; // TODO Verify if the switch holding for expression interferes with Bypass/Alternate functionality
    } 

    // Toggle Alternate mode by holding down the left footswitch, if not already in alternate mode and not in bypass
    if(hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() >= 600 && !alternateMode && (!bypass || LooperModeActive()) && !hw.switches[Funbox::FOOTSWITCH_2].Pressed() && !expression_pressed) { // TODO Check logic here
        alternateMode = true;
        led1.Set(0.5f);  // Dim LED in alternate mode
    }


    // Toggle Expression mode by holding down both footswitches for half a second
    if(hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() >= 500 && hw.switches[Funbox::FOOTSWITCH_2].TimeHeldMs() >= 500 && !expression_pressed ) {
        expHandler.ToggleExpressionSetMode();

        if (expHandler.isExpressionSetMode()) {
            led1.Set(expHandler.returnLed1Brightness());  // Dim LEDs in expression set mode
            led2.Set(expHandler.returnLed2Brightness());  // Dim LEDs in expression set mode

        } else {
            led1.Set(bypass ? 0.0f : 1.0f); 
            led2.Set(0.0f);  
        }
        expression_pressed = true; // Keeps it from switching over and over while held

    }

    // Clear Expression settings by holding down both footswitches for 2 seconds
    if(hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() >= 2000 && hw.switches[Funbox::FOOTSWITCH_2].TimeHeldMs() >= 2000) {
        expHandler.Reset();
        led1.Set(bypass ? 0.0f : 1.0f); 
        led2.Set(0.0f); 

    }

    if(hw.switches[Funbox::FOOTSWITCH_2].TimeHeldMs() >= kLooperLongPressMs
       && !footswitch2_long_handled
       && !expression_pressed
       && hw.switches[Funbox::FOOTSWITCH_1].TimeHeldMs() <= 50)
    {
        looper_pending_length_op = LooperLengthOp::None;
        if(LooperModeActive())
            ExitLooperMode();
        else
            EnterLooperMode();
        footswitch2_long_handled = true;
    }

    if(hw.switches[Funbox::FOOTSWITCH_2].RisingEdge())
    {
        footswitch2_down_ms = System::GetNow();
        footswitch2_long_handled = false;
        footswitch2_looper_double_handled = false;
    }

    if(LooperModeActive()
       && hw.switches[Funbox::FOOTSWITCH_2].Pressed()
       && !footswitch2_long_handled
       && !expression_pressed)
    {
        HandleLooperTapHeld(System::GetNow() - footswitch2_down_ms);
    }

    // Short right footswitch clicks tap tempo, or controls the looper when loop mode is active.
    if(hw.switches[Funbox::FOOTSWITCH_2].FallingEdge() && !expression_pressed)
    {
        const uint32_t held_ms = System::GetNow() - footswitch2_down_ms;
        if(footswitch2_long_handled)
        {
            footswitch2_long_handled = false;
        }
        else if(LooperModeActive())
        {
            HandleLooperTapRelease(held_ms);
        }
        else if(held_ms < 350)
        {
            tempo::RegisterTap();
        }
    }

    UpdateLed2Feedback();

    if(!expHandler.isExpressionSetMode() && tempo::MidiClockPresent())
    {
        const float base = Led1BaseBrightness();
        const bool midi_pulse = System::GetNow() < midi_led_pulse_until_ms;
        led1.Set(midi_pulse ? (base > 0.0f ? 0.0f : 1.0f) : base);
    }
}


void UpdateSwitches()
{

    // Detect any changes in switch positions

    // 3-way Switch 1
    bool changed1 = false;
    for(int i=0; i<2; i++) {
        if (hw.switches[switch1[i]].Pressed() != pswitch1[i]) {
            pswitch1[i] = hw.switches[switch1[i]].Pressed();
            changed1 = true;
            
        }
    }
    if (changed1 || update_switches) { // update_switches is for turning off preset
        if (pswitch1[0] == true) {
            toggleValues[0] = 0;
        } else if (pswitch1[1] == true) {
            toggleValues[0] = 2;
        } else {
            toggleValues[0] = 1;
        }
        mode_changed = true; // This will trigger calling updateSwitch1() after a waiting time
    }


    // 3-way Switch 2
    bool changed2 = false;
    for(int i=0; i<2; i++) {
        if (hw.switches[switch2[i]].Pressed() != pswitch2[i]) {
            pswitch2[i] = hw.switches[switch2[i]].Pressed();
            changed2 = true;
        }
    }
    if (changed2 || update_switches) {
        if (pswitch2[0] == true) {
            toggleValues[1] = 0;
        } else if (pswitch2[1] == true) {
            toggleValues[1] = 2;
        } else {
            toggleValues[1] = 1;
        }
        //updateSwitch2(); // action for routing toggle handled in the audio callback
    }

    // 3-way Switch 3
    bool changed3 = false;
    for(int i=0; i<2; i++) {
        if (hw.switches[switch3[i]].Pressed() != pswitch3[i]) {
            pswitch3[i] = hw.switches[switch3[i]].Pressed();
            changed3 = true;
        }
    }
    if (changed3 || update_switches) {
        if (pswitch3[0] == true) {
            toggleValues[2] = 0;
        } else if (pswitch3[1] == true) {
            toggleValues[2] = 2;
        } else {
            toggleValues[2] = 1;
        }
        updateSwitch3();
    }

    // Dip switches
    bool changed4 = false;
    for(int i=0; i<4; i++) {
        if (hw.switches[dip[i]].Pressed() != pdip[i]) {
            pdip[i] = hw.switches[dip[i]].Pressed();
            changed4 = true;
            // Action for dipswitches handled in audio callback
        }
    }
    // Update if preset turned off
    if (changed4 || update_switches) {
        for (int i=0; i<4; i++) {
           dipValues[i] = pdip[i];    // TODO Check logic here
        }
        UpdateTempoSyncMode();
    }

    update_switches = false; // only update once after turning off preset

    if (mode_changed) {
        if (ramp > 0.95) { // Helps prevent mode from changing too quickly and overloading
            ramp = 0.0;
            triggerMode = true;

        }
    }

}


// This runs at a fixed rate, to prepare audio samples
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{

    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();
    led1.Update();
    led2.Update();

    UpdateButtons();
    UpdateSwitches();


    // Knob and Expression Processing ////////////////////

    //float knobValues[6];
    float newExpressionValues[6];


    if (!use_preset) {  // TODO Do I want to lock out the knobs when using a preset?
        const bool looper_fx_mode = LooperModeActive() && alternateMode;
        if(looper_fx_mode && !previous_looper_fx_mode)
        {
            pknobValues[2] = delayTime.Process();
            pknobValues[5] = delayFDBK.Process();
            hold_k3_looper_fx = true;
            hold_k6_looper_fx = true;
        }
        previous_looper_fx_mode = looper_fx_mode;

        // Knob 1
        if (!midi_control[0])   // If not under midi control, use knob ADC
            pknobValues[0] = knobValues[0] = rsize.Process();
        else if (knobMoved(pknobValues[0], rsize.Process()))  // If midi controlled, watch for knob movement to end Midi control
            midi_control[0] = false;

        // Knob 2
        if (!midi_control[1])   // If not under midi control, use knob ADC
            pknobValues[1] = knobValues[1] = mix.Process();
        else if (knobMoved(pknobValues[1], mix.Process()))  // If midi controlled, watch for knob movement to end Midi control
            midi_control[1] = false;

        // Knob 3
        if(LooperModeActive())
        {
            const float physical_delay_time = delayTime.Process();
            if(alternateMode)
            {
                if(!hold_k3_looper_fx || knobMoved(pknobValues[2], physical_delay_time))
                {
                    hold_k3_looper_fx = false;
                    pknobValues[2] = physical_delay_time;
                    looper_fx_time_knob = physical_delay_time;
                }
            }
            else
                looper_route_knob = physical_delay_time;
            knobValues[2] = looper_fx_time_knob;
        }
        else if(hold_k3_after_looper)
        {
            const float physical_delay_time = delayTime.Process();
            if(knobMoved(pknobValues[2], physical_delay_time))
            {
                hold_k3_after_looper = false;
                pknobValues[2] = knobValues[2] = physical_delay_time;
            }
            else
            {
                knobValues[2] = looper_fx_time_knob;
            }
        }
        else if (!midi_control[2])   // If not under midi control, use knob ADC
            pknobValues[2] = knobValues[2] = delayTime.Process();
        else if (knobMoved(pknobValues[2], delayTime.Process()))  // If midi controlled, watch for knob movement to end Midi control
            midi_control[2] = false;

        // Knob 4
        if (!midi_control[3])   // If not under midi control, use knob ADC
            pknobValues[3] = knobValues[3] = modify.Process();
        else if (knobMoved(pknobValues[3], modify.Process()))  // If midi controlled, watch for knob movement to end Midi control
            midi_control[3] = false;
    

        // Knob 5
        if (!midi_control[4])   // If not under midi control, use knob ADC
            pknobValues[4] = knobValues[4] = filter.Process();
        else if (knobMoved(pknobValues[4], filter.Process()))  // If midi controlled, watch for knob movement to end Midi control
            midi_control[4] = false;
    

        // Knob 6
        if(LooperModeActive())
        {
            const float physical_feedback = delayFDBK.Process();
            if(alternateMode)
            {
                if(!hold_k6_looper_fx || knobMoved(pknobValues[5], physical_feedback))
                {
                    hold_k6_looper_fx = false;
                    pknobValues[5] = physical_feedback;
                    looper_fx_feedback_knob = physical_feedback;
                }
            }
            else
            {
                if(CurrentLooperRoute() != LooperRoute::LoopIntoFxResample)
                    looper_feedback_knob = SafeLooperFeedback(physical_feedback);
            }
            knobValues[5] = looper_fx_feedback_knob;
        }
        else if(hold_k6_after_looper)
        {
            const float physical_feedback = delayFDBK.Process();
            if(knobMoved(pknobValues[5], physical_feedback))
            {
                hold_k6_after_looper = false;
                pknobValues[5] = knobValues[5] = physical_feedback;
            }
            else
            {
                knobValues[5] = looper_fx_feedback_knob;
            }
        }
        else if (!midi_control[5])   // If not under midi control, use knob ADC
            pknobValues[5] = knobValues[5] = delayFDBK.Process();
        else if (knobMoved(pknobValues[5], delayFDBK.Process()))  // If midi controlled, watch for knob movement to end Midi control
            midi_control[5] = false;

    }


    float vexpression = expression.Process(); // 0 is heel (up), 1 is toe (down)
    expHandler.Process(vexpression, knobValues, newExpressionValues);


    // If in expression set mode, set LEDS accordingly
    if (expHandler.isExpressionSetMode()) {
        led1.Set(expHandler.returnLed1Brightness());
        led2.Set(expHandler.returnLed2Brightness());
    }
    else if (tempo_debug_blink > 0) {
        tempo_debug_blink -= 1;
        const float base = Led1BaseBrightness();
        led1.Set(base > 0.0f ? 0.0f : 1.0f);
        if (tempo_debug_blink == 0)
            led1.Set(base);
    }
  

    float vsize = newExpressionValues[0];
    float vmix = newExpressionValues[1];
    float vdelayTime = newExpressionValues[2];
    float vmodify = newExpressionValues[3];
    float vfilter = newExpressionValues[4];
    float vdelayFDBK = newExpressionValues[5];
    tempo_division_knob = vdelayTime;


    if (knobMoved(pmix, vmix) || force_reset == true) {
        // Handle Normal or Alternate Mode Mix Controls
        //    A cheap mostly energy constant crossfade from SignalSmith Blog
        float x2 = 1.0 - vmix;
        float A = vmix*x2;
        float B = A * (1.0 + 1.4186 * A);
        float C = B + vmix;
        float D = B + x2;

        if (!alternateMode) {
            wetMix = C * C;
            dryMix = D * D;

        } else {    //// UPDATE   //// UPDATE   //// UPDATE
            delayMix = C * C;
            reverbMix = D * D;

        }
        pmix = vmix;
    }

    if (knobMoved(pfilter, vfilter)) {
        reverb->SetParameter(::Parameter2::PostCutoffFrequency, vfilter); // TODO maybe do something with the shelf filters as well
        pfilter = vfilter;
    }


    //float fvdelayTime = 1.0 - vdelayTime; //Invert parameter for correct knob operation

    const bool tempo_sync_active = tempo::Active();

    if (!tempo_sync_active && vdelayTime <= 0.01) {   // if knob < 1%, set delay to inactive in free mode
        delayL.active = false;
        delayR.active = false;

    } else {
        delayL.active = true;
        delayR.active = true;
    }


    if (tempo_sync_active) {
        float syncedDelay = tempo::DelaySamples(vdelayTime);
        if (syncedDelay > static_cast<float>(MAX_DELAY - 1))
            syncedDelay = static_cast<float>(MAX_DELAY - 1);
        delayL.delayTarget = syncedDelay;
        delayR.delayTarget = syncedDelay;
    } else {
        const float freeDelay = FreeDelaySamples(vdelayTime);
        delayL.delayTarget = freeDelay;
        delayR.delayTarget = freeDelay;   
    }

    delayL.feedback = vdelayFDBK;
    delayR.feedback = vdelayFDBK;


    // Set Reverb Parameters ///////////////
    if (knobMoved(psize, vsize) || force_reset == true)
    {
        if (!alternateMode) {

            reverb->SetParameter(::Parameter2::LineDecay, (vsize));      

        } else {  
            preDelayAlt = vsize;
            reverb->SetParameter(::Parameter2::PreDelay, preDelayAlt);
        }
        psize = vsize;
    }

    if ((knobMoved(pmodify, vmodify) || force_reset == true)) // Force reset to apply knob params when switching reverb modes, or when alternate mode released
    {
        if (!alternateMode) {
            reverb->SetParameter(::Parameter2::LineModAmount, vmodify); // Range 0 to 1

        } else {
            modAlt = vmodify;
            reverb->SetParameter(::Parameter2::LineModRate, modAlt); // Range 0 to ~1

        }
        pmodify = vmodify;
    }

    force_reset = false;

    float inL[1];
    float outL[1];
    float inR[1];
    float outR[1];

    float delay_outL;
    float delay_inL;
    float delay_outR;
    float delay_inR;

    float inputL;
    float inputR;
    float loop_outL;
    float loop_outR;
    const bool looper_mode = LooperModeActive();
    const bool looper_has_audio = LooperHasLoop();
    const bool process_fx = !bypass || looper_mode || looper_has_audio;
    const bool overdub_enabled = looper_mode && !bypass;
    if(overdub_enabled && !previous_overdub_enabled)
        overdub_fade_remaining = kOverdubFadeSamples;
    previous_overdub_enabled = overdub_enabled;

    if(process_fx) {
        for (size_t i = 0; i < size; i++)
        {
            DrainLooperExitSample();

            // Stereo or MISO 
            if (dipValues[0]) {  // Stereo
                inputL = in[0][i] * kNeptuneInputPad;
                inputR = in[1][i] * kNeptuneInputPad;
            } else {     //MISO
                inputL = in[0][i] * kNeptuneInputPad;
                inputR = in[0][i] * kNeptuneInputPad;
            }
            loop_outL = 0.0f;
            loop_outR = 0.0f;
 
            const LooperRoute looper_route = CurrentLooperRoute();
            const bool resample_loop_fx = looper_mode
                                          && looper_has_audio
                                          && looper_route == LooperRoute::LoopIntoFxResample;
            bool resample_active = false;
            if(resample_loop_fx)
            {
                resample_active = ReadLooperForFxResample(inputL, inputR, loop_outL, loop_outR);
                delay_inL = NeptuneSoftLimit(loop_outL + (overdub_enabled ? inputL * 0.85f : 0.0f));
                delay_inR = NeptuneSoftLimit(loop_outR + (overdub_enabled ? inputR * 0.85f : 0.0f));
            }
            else
            {
                delay_inL = inputL;
                delay_inR = inputR;
            }

            delay_outL = (delayL.Process(delay_inL)  * delayMix);
            delay_outR = (delayR.Process(delay_inR) * delayMix);


            // Delay and Reverb Routing based on Switch3 Position
            //        Up=Delay + and into Reverb     Middle= Delay and Reverb in Parallel  Down= Delay into Reverb

            if ((toggleValues[1] == 0) || (toggleValues[1] == 2)) {  // Up or Down Delay into reverb
                inL[0] = delay_inL + delay_outL;
                inR[0] = delay_inR + delay_outR;

            } else { // Middle position process R and D in parallel
                inL[0] = delay_inL;
                inR[0] = delay_inR;
            }


            reverb->Process(inL, inR, outL, outR, 1); // Try doing the 1 sample like in effects suite, then just have one sample loop


            float balanced_outL;
            float balanced_outR;

            if (!(toggleValues[1] == 2)) {  // If switch3 not down (if either up or middle position) add delay
                balanced_outL =  (outL[0] * (reverbMix * 1.2) + delay_outL) * wetMix; // Apply mix and level controls, slight reverb boost
                balanced_outR =  (outR[0] * (reverbMix * 1.2) + delay_outR) * wetMix;
            } else {
                balanced_outL =  (outL[0] * (reverbMix * 1.2))  * wetMix; // Apply mix and level controls, slight reverb boost
                balanced_outR =  (outR[0] * (reverbMix * 1.2))  * wetMix;
            }


            const float effect_outL = NeptuneSoftLimit(delay_inL * dryMix + balanced_outL * wetMix);
            const float effect_outR = NeptuneSoftLimit(delay_inR * dryMix + balanced_outR * wetMix);

            if(looper_mode && !looper_has_audio)
            {
                out[0][i] = effect_outL;
                out[1][i] = effect_outR;
            }
            else if(resample_loop_fx)
            {
                float overdub_gain = 1.0f;
                if(overdub_fade_remaining > 0)
                {
                    overdub_gain = 1.0f
                                   - (static_cast<float>(overdub_fade_remaining)
                                      / static_cast<float>(kOverdubFadeSamples));
                    overdub_fade_remaining -= 1;
                }
                if(resample_active)
                    WriteLooperFxResample(effect_outL,
                                          effect_outR,
                                          overdub_enabled,
                                          overdub_gain,
                                          LooperFxFeedbackFromRouteKnob());
                out[0][i] = effect_outL;
                out[1][i] = effect_outR;
            }
            else if(looper_mode)
            {
                const bool op_wet = LooperOverplayWet(looper_route);
                const bool od_wet = LooperOverdubWet(looper_route);
                const float overplayL = op_wet ? effect_outL : inputL;
                const float overplayR = op_wet ? effect_outR : inputR;
                const float overdubL = od_wet ? effect_outL : inputL;
                const float overdubR = od_wet ? effect_outR : inputR;
                float overdub_gain = 1.0f;
                if(overdub_fade_remaining > 0)
                {
                    overdub_gain = 1.0f
                                   - (static_cast<float>(overdub_fade_remaining)
                                      / static_cast<float>(kOverdubFadeSamples));
                    overdub_fade_remaining -= 1;
                }
                ProcessLooper(overdubL,
                              overdubR,
                              overdub_enabled,
                              overdub_gain,
                              looper_feedback_knob,
                              loop_outL,
                              loop_outR);
                out[0][i] = NeptuneSoftLimit(overplayL * kLooperMonitorPad + loop_outL * kLooperMonitorPad);
                out[1][i] = NeptuneSoftLimit(overplayR * kLooperMonitorPad + loop_outR * kLooperMonitorPad);
            }
            else
            {
                out[0][i] = effect_outL;
                out[1][i] = effect_outR;
            }

            if (ramp < 1.0) {  // Volume ramp when changing reverb modes (to prevent audible click)
                ramp += 0.001;
                out[0][i] = 0.0;
                if (ramp > 0.1 && triggerMode == true) {
                    triggerMode = false;
                    updateSwitch1();
                    mode_changed = false;
                }

            }
        }

    } else {
        for (size_t i = 0; i < size; i++)
        {
            DrainLooperExitSample();

            // Stereo or MISO 
            if (dipValues[0]) {  // Stereo
                inputL = in[0][i] * kNeptuneInputPad;
                inputR = in[1][i] * kNeptuneInputPad;
            } else {     //MISO
                inputL = in[0][i] * kNeptuneInputPad;
                inputR = in[0][i] * kNeptuneInputPad;
            }
            loop_outL = 0.0f;
            loop_outR = 0.0f;

            const float fx_inputL = 0.0f;
            const float fx_inputR = 0.0f;

            delay_outL = delayL.Process(fx_inputL) * delayMix;
            delay_outR = delayR.Process(fx_inputR) * delayMix;
            if ((toggleValues[1] == 0) || (toggleValues[1] == 2)) {
                inL[0] = fx_inputL + delay_outL;
                inR[0] = fx_inputR + delay_outR;
            } else {
                inL[0] = fx_inputL;
                inR[0] = fx_inputR;
            }
            reverb->Process(inL, inR, outL, outR, 1);

            float tail_outL;
            float tail_outR;
            if (!(toggleValues[1] == 2)) {
                tail_outL = (outL[0] * (reverbMix * 1.2) + delay_outL) * wetMix;
                tail_outR = (outR[0] * (reverbMix * 1.2) + delay_outR) * wetMix;
            } else {
                tail_outL = (outL[0] * (reverbMix * 1.2)) * wetMix;
                tail_outR = (outR[0] * (reverbMix * 1.2)) * wetMix;
            }

            out[0][i] = NeptuneSoftLimit(inputL + tail_outL * wetMix * 0.7f);
            out[1][i] = NeptuneSoftLimit(inputR + tail_outR * wetMix * 0.7f);
        }
    }
}


// Typical Switch case for Message Type.
void HandleMidiMessage(MidiEvent m)
{
#if NEPTUNE_RAW_MIDI_DEBUG
    raw_midi_debug_count += 1;
    const bool print_raw = raw_midi_debug_count <= 16
                           || m.type != SystemRealTime
                           || (raw_midi_debug_count % 96) == 0;
    if(print_raw)
    {
        DaisySeed::PrintLine("raw midi type=%d ch=%d d0=%d d1=%d srt=%d",
                             static_cast<int>(m.type),
                             m.channel,
                             m.data[0],
                             m.data[1],
                             static_cast<int>(m.srt_type));
    }
#endif

    switch(m.type)
    {
        case NoteOn:
        {

            // Real MIDI notes must not become tap clock when a MIDI cable is connected.
            // Tap is handled by footswitch 2; MIDI timing is handled by TimingClock below.
        }
        break;
        case SystemRealTime:
        {
            if(m.srt_type == TimingClock)
            {
                tempo::HandleMidiClock();
                midi_clock_led_count += 1;
                if(midi_clock_led_count >= 24)
                {
                    midi_clock_led_count = 0;
                    midi_led_pulse_until_ms = System::GetNow() + 35;
                    TriggerTempoDebugBlink();
                }

                midi_clock_serial_count += 1;
                if(midi_clock_serial_count >= 24)
                {
                    midi_clock_serial_count = 0;
                    DaisySeed::PrintLine(tempo::MidiSyncEnabled()
                                             ? "midi clock quarter"
                                             : "midi clock seen");
                }
            }
        }
        break;
        case ControlChange:
        {

            ControlChangeEvent p = m.AsControlChange();
            switch(p.control_number)
            {
                case 14:
                    midi_control[0] = true;
                    knobValues[0] = ((float)p.value / 127.0f);
                    break;
                case 15:
                    midi_control[1] = true;
                    knobValues[1] = ((float)p.value / 127.0f);
                    break;
                case 16:
                    midi_control[2] = true;
                    knobValues[2] = ((float)p.value / 127.0f);
                    break;
                case 17:
                    midi_control[3] = true;
                    knobValues[3] = ((float)p.value / 127.0f);
                    break;
                case 18:
                    midi_control[4] = true;
                    knobValues[4] = ((float)p.value / 127.0f);
                    break;
                case 19:
                    midi_control[5] = true;
                    knobValues[5] = ((float)p.value / 127.0f);
                    break;


                default: break;
            }
            break;
        }
        default: break;
    }
}



int main(void)
{
    float samplerate;

    hw.Init();
    DaisySeed::StartLog(false);
    DaisySeed::PrintLine("Neptune serial ready");
    samplerate = hw.AudioSampleRate();

    tempo::Init(samplerate);

    AudioLib::ValueTables::Init();
    CloudSeed::FastSin::Init();

    reverb = new CloudSeed::ReverbController(samplerate);
    updateSwitch1();
    DaisySeed::PrintLine("CloudSeed pool used: %u KB / %u KB, allocs=%d",
                         static_cast<unsigned int>(pool_index / 1024),
                         static_cast<unsigned int>(CUSTOM_POOL_SIZE / 1024),
                         allocation_count);
    using namespace CloudSeed;
    reverb->SetParameter(::Parameter2::PreDelay, 0.0);  // Set initial predelay to 0.0 (controlled by Alt param)

    hw.SetAudioBlockSize(48);

    rsize.Init(hw.knob[Funbox::KNOB_1], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    modify.Init(hw.knob[Funbox::KNOB_4], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);

    delayTime.Init(hw.knob[Funbox::KNOB_3], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    delayFDBK.Init(hw.knob[Funbox::KNOB_6], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);

    mix.Init(hw.knob[Funbox::KNOB_2], 0.0f, 1.0f, ::daisy::Parameter::LINEAR);
    filter.Init(hw.knob[Funbox::KNOB_5], 0.0f, 1.0f, ::daisy::Parameter::CUBE);
    expression.Init(hw.expression, 0.0f, 1.0f, Parameter::LINEAR); 

    // Alternate Parameters
    alternateMode = false;

    delayMix = 0.703;   // Reverb vs. Delay Level
    reverbMix = 0.703;

    pmix = 0.5;
    modAlt = 0.0;
    preDelayAlt = 0.0;

    psize = 0.0;
    pmodify = 0.0;
    pdelayTime = 0.0;


    delayLineRevLeft.Init();
    delayLineOctLeft.Init();
    delayL.del = &delayLineOctLeft;
    delayL.delreverse = &delayLineRevLeft;
    delayL.delayTarget = 2400; // in samples
    delayL.currentDelay = delayL.delayTarget;
    delayL.feedback = 0.0;
    delayL.active = true;     // Default to no delay

    delayLineRevRight.Init();
    delayLineOctRight.Init();
    delayR.del = &delayLineOctRight;
    delayR.delreverse = &delayLineRevRight;
    delayR.delayTarget = 2400; // in samples
    delayR.currentDelay = delayR.delayTarget;
    delayR.feedback = 0.0;
    delayR.active = true;     // Default to no delay

    toneOctLP.Init(samplerate);
    toneOctLP.SetFreq(3000.0);

    pfilter = 0.5;

    force_reset = false;

    ramp = 0.0;
    triggerMode = false;

    updateSwitch3();

    switch1[0]= Funbox::SWITCH_1_LEFT;
    switch1[1]= Funbox::SWITCH_1_RIGHT;
    switch2[0]= Funbox::SWITCH_2_LEFT;
    switch2[1]= Funbox::SWITCH_2_RIGHT;
    switch3[0]= Funbox::SWITCH_3_LEFT;
    switch3[1]= Funbox::SWITCH_3_RIGHT;
    dip[0]= Funbox::SWITCH_DIP_1;
    dip[1]= Funbox::SWITCH_DIP_2;
    dip[2]= Funbox::SWITCH_DIP_3;
    dip[3]= Funbox::SWITCH_DIP_4;

    pswitch1[0]= false;
    pswitch1[1]= false;
    pswitch2[0]= false;
    pswitch2[1]= false;
    pswitch3[0]= false;
    pswitch3[1]= false;
    pdip[0]= false;
    pdip[1]= false;
    pdip[2]= false;
    pdip[3]= false;

    update_switches = true;

    // Midi
    for( int i = 0; i < 6; ++i ) 
        midi_control[i] = false;  

    // Expression
    expHandler.Init(6);
    expression_pressed = false;

    // Init the LEDs and set activate bypass
    led1.Init(hw.seed.GetPin(Funbox::LED_1),false);
    led1.Update();
    bypass = true;

    led2.Init(hw.seed.GetPin(Funbox::LED_2),false);
    led2.Update();


    //Initilize the PersistentStorage Object with default values.
    //Defaults will be the first values stored in flash when the device is first turned on. They can also be restored at a later date using the RestoreDefaults method
    Settings DefaultSettings = {0.0f, 0.0f};
    SavedSettings.Init(DefaultSettings);

    hw.InitMidi();
    hw.midi.StartReceive();

    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    while(1)
    {
        hw.midi.Listen();
        // Handle MIDI Events
        while(hw.midi.HasEvents())
        {
            HandleMidiMessage(hw.midi.PopEvent());
        }

        if(trigger_save) {
			
	    SavedSettings.Save(); // Writing locally stored settings to the external flash
	    trigger_save = false;
        blink = 0;
	    }
	System::Delay(1);
    }
}
