#include "harmonic_notch_filter.h"

#include <math.h>

namespace
{
inline float Clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}
} // namespace

void HarmonicNotchFilter::BiquadNotch::Set(float sample_rate, float freq_hz, float q)
{
    const float w0 = 2.0f * 3.14159265359f * freq_hz / sample_rate;
    const float cw = cosf(w0);
    const float sw = sinf(w0);
    const float alpha = sw / (2.0f * q);

    const float b0n = 1.0f;
    const float b1n = -2.0f * cw;
    const float b2n = 1.0f;
    const float a0 = 1.0f + alpha;
    const float a1n = -2.0f * cw;
    const float a2n = 1.0f - alpha;

    b0 = b0n / a0;
    b1 = b1n / a0;
    b2 = b2n / a0;
    a1 = a1n / a0;
    a2 = a2n / a0;
}

float HarmonicNotchFilter::BiquadNotch::ProcessL(float x)
{
    const float y = b0 * x + z1_l;
    z1_l = b1 * x - a1 * y + z2_l;
    z2_l = b2 * x - a2 * y;
    return y;
}

float HarmonicNotchFilter::BiquadNotch::ProcessR(float x)
{
    const float y = b0 * x + z1_r;
    z1_r = b1 * x - a1 * y + z2_r;
    z2_r = b2 * x - a2 * y;
    return y;
}

void HarmonicNotchFilter::BiquadNotch::Reset()
{
    z1_l = z2_l = 0.0f;
    z1_r = z2_r = 0.0f;
}

void HarmonicNotchFilter::Init(float sample_rate)
{
    sample_rate_ = sample_rate > 1000.0f ? sample_rate : 48000.0f;
    Rebuild();
}

void HarmonicNotchFilter::SetFundamentalHz(float hz)
{
    fundamental_hz_ = Clampf(hz, 20.0f, 4000.0f);
    Rebuild();
}

void HarmonicNotchFilter::SetQ(float q)
{
    q_ = Clampf(q, 1.0f, 60.0f);
    Rebuild();
}

void HarmonicNotchFilter::SetHarmonicCount(size_t count)
{
    if(count < 1)
        count = 1;
    if(count > kMaxHarmonics)
        count = kMaxHarmonics;
    harmonic_count_ = count;
    Rebuild();
}

void HarmonicNotchFilter::SetDepth(float depth)
{
    depth_ = Clampf(depth, 0.0f, 1.0f);
}

void HarmonicNotchFilter::SetSlope24dB(bool enabled)
{
    slope24db_ = enabled;
}

void HarmonicNotchFilter::SetExtraStageMix(float mix)
{
    extra_stage_mix_ = Clampf(mix, 0.0f, 1.0f);
}

float HarmonicNotchFilter::ClampFreq(float hz) const
{
    const float nyquist = sample_rate_ * 0.5f;
    return Clampf(hz, 20.0f, nyquist * 0.95f);
}

void HarmonicNotchFilter::Rebuild()
{
    for(size_t i = 0; i < kMaxHarmonics; ++i)
    {
        const float f = ClampFreq(fundamental_hz_ * static_cast<float>(i + 1));
        stage_a_[i].Set(sample_rate_, f, q_);
        stage_b_[i].Set(sample_rate_, f, q_);
        stage_c_[i].Set(sample_rate_, f, q_);
    }
}

void HarmonicNotchFilter::ProcessStereo(float in_l, float in_r, float& out_l, float& out_r)
{
    float wet_l = in_l;
    float wet_r = in_r;

    for(size_t i = 0; i < harmonic_count_; ++i)
    {
        wet_l = stage_a_[i].ProcessL(wet_l);
        wet_r = stage_a_[i].ProcessR(wet_r);
        if(slope24db_)
        {
            wet_l = stage_b_[i].ProcessL(wet_l);
            wet_r = stage_b_[i].ProcessR(wet_r);
            if(extra_stage_mix_ > 0.0f)
            {
                const float c_l = stage_c_[i].ProcessL(wet_l);
                const float c_r = stage_c_[i].ProcessR(wet_r);
                wet_l = wet_l + (c_l - wet_l) * extra_stage_mix_;
                wet_r = wet_r + (c_r - wet_r) * extra_stage_mix_;
            }
        }
    }

    const float dry = 1.0f - depth_;
    out_l = dry * in_l + depth_ * wet_l;
    out_r = dry * in_r + depth_ * wet_r;
}
