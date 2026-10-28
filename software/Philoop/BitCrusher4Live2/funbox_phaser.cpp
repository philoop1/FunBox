#include "dsp.h"
#include "funbox_phaser.h"

using namespace daisysp;

void FunboxPhaserEngine::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    del_.Init();
    feedback_ = 0.2f;
    ap_freq_ = 200.0f;
    os_ = 30.0f;
    deltime_ = 0.0f;
    last_sample_ = 0.0f;
    del_.SetDelay(0.0f);
}

float FunboxPhaserEngine::Process(float in)
{
    fonepole(deltime_, sample_rate_ / (ap_freq_ + os_), 0.0001f);
    last_sample_ = del_.Allpass(in + feedback_ * last_sample_, deltime_, 0.3f);
    return (in + last_sample_) * 0.5f;
}

void FunboxPhaserEngine::SetFreq(float ap_freq)
{
    ap_freq_ = fclamp(ap_freq, 0.0f, 20000.0f);
}

void FunboxPhaserEngine::SetFeedback(float feedback)
{
    feedback_ = fclamp(feedback, 0.0f, 0.92f);
}

void FunboxPhaser::Init(float sample_rate)
{
    for(size_t i = 0; i < kMaxPoles; i++)
        engines_[i].Init(sample_rate);
    poles_ = 6;
}

float FunboxPhaser::Process(float in)
{
    float sig = 0.0f;
    for(int i = 0; i < poles_; i++)
        sig += engines_[i].Process(in);
    return sig;
}

void FunboxPhaser::SetPoles(int poles)
{
    poles_ = DSY_CLAMP(poles, 1, 8);
}

void FunboxPhaser::SetFreq(float ap_freq)
{
    for(int i = 0; i < kMaxPoles; i++)
        engines_[i].SetFreq(ap_freq);
}

void FunboxPhaser::SetFeedback(float feedback)
{
    for(int i = 0; i < kMaxPoles; i++)
        engines_[i].SetFeedback(feedback);
}
