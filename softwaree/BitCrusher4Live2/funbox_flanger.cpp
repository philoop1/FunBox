#include "dsp.h"
#include "funbox_flanger.h"

using namespace daisysp;

void FunboxFlanger::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    delay_samples_ = sample_rate * 0.004f;
    feedback_ = 0.0f;
    wet_ = 0.55f;
    last_delay_ = delay_samples_;
    last_sample_ = 0.0f;
    del_.Init();
}

void FunboxFlanger::SetDelayMs(float delay_ms)
{
    const float max_delay = static_cast<float>(kDelayLength - 4);
    delay_samples_ = fclamp(delay_ms * sample_rate_ * 0.001f, 2.0f, max_delay);
}

void FunboxFlanger::SetFeedback(float feedback)
{
    feedback_ = fclamp(feedback, -0.99f, 0.99f);
}

void FunboxFlanger::SetWet(float wet)
{
    wet_ = fclamp(wet, 0.0f, 1.0f);
}

float FunboxFlanger::Process(float in)
{
    fonepole(last_delay_, delay_samples_, 0.002f);
    const float delayed = del_.ReadHermite(last_delay_);
    del_.Write(fclamp(in + delayed * feedback_, -1.0f, 1.0f));
    last_sample_ = delayed;
    return fclamp(in * (1.0f - wet_) + delayed * wet_, -1.0f, 1.0f);
}
