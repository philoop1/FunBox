#include "funbox_wavefolder.h"

#include <math.h>

void FunboxWavefolder::Init() {}

float FunboxWavefolder::Clamp01(float v) const
{
    if(v < 0.0f)
        return 0.0f;
    if(v > 1.0f)
        return 1.0f;
    return v;
}

float FunboxWavefolder::BipolarDeadband(float v) const
{
    static constexpr float kDeadband = 0.04f;
    float b = Clamp01(v) * 2.0f - 1.0f;
    if(fabsf(b) < kDeadband)
        return 0.0f;

    const float sign = b < 0.0f ? -1.0f : 1.0f;
    b = (fabsf(b) - kDeadband) / (1.0f - kDeadband);
    return sign * Clamp01(b);
}

float FunboxWavefolder::ExpMap(float knob, float min_v, float max_v) const
{
    return min_v * powf(max_v / min_v, Clamp01(knob));
}

float FunboxWavefolder::Fold(float in) const
{
    const float ft = floorf((in + 1.0f) * 0.5f);
    const float sgn = static_cast<int>(ft) % 2 == 0 ? 1.0f : -1.0f;
    return sgn * (in - 2.0f * ft);
}

float FunboxWavefolder::Process(float in,
                                float threshold_control,
                                float env_to_threshold,
                                float lfo_to_threshold,
                                float lfo_signal,
                                float env)
{
    const float base_threshold = ExpMap(threshold_control, 1.0f, 0.001f);

    const float env_threshold
        = powf(2.0f, -BipolarDeadband(env_to_threshold) * env * 8.0f);
    const float lfo_amount
        = Clamp01(lfo_to_threshold) < 0.02f ? 0.0f : Clamp01(lfo_to_threshold);
    const float lfo_threshold = powf(2.0f, -lfo_signal * lfo_amount * 8.0f);
    const float threshold
        = fminf(fmaxf(base_threshold * env_threshold * lfo_threshold, 0.0005f), 2.0f);

    const float folded = Fold(in / threshold) * threshold;
    const float over_threshold = fabsf(in) - threshold;
    const float fold_activity
        = Clamp01(over_threshold / fmaxf(threshold * 4.0f, 0.001f));
    const float makeup = fminf(powf(threshold, -0.70f), 96.0f);

    return folded * (1.0f + fold_activity * (makeup - 1.0f));
}
