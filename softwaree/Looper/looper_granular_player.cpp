#include "looper_granular_player.h"

using namespace funbox;

void LooperGranularPlayer::Init(float* sample, int size, float sample_rate)
{
    sample_      = sample;
    size_        = size;
    sample_rate_ = sample_rate;

    phs_.Init(sample_rate_, 0.0f, 0.0f);
    phsImp_.Init(sample_rate_, 0.0f, 0.0f);
    phs2_.Init(sample_rate_, 0.0f, 0.5f);
    phsImp2_.Init(sample_rate_, 0.0f, 0.0f);

    sample_frequency_       = sample_rate_ / static_cast<float>(size_);
    smoothed_speed_         = 1.0f;
    smoothed_transposition_ = 0.0f;
    smoothed_grain_size_    = 80.0f;

    for(int i = 0; i < 256; i++)
    {
        const float phase = static_cast<float>(i) / 255.0f;
        const float hann = 0.5f - 0.5f * cosf(2.0f * M_PI * phase);
        grain_env_[i] = hann * hann;
    }
}

float LooperGranularPlayer::WrapPosition(float idx, float size)
{
    while(idx < 0.0f)
        idx += size;
    while(idx >= size)
        idx -= size;
    return idx;
}

float LooperGranularPlayer::ReadLinear(float idx)
{
    const float wrapped = WrapPosition(idx, static_cast<float>(size_));
    const int   i0      = static_cast<int>(wrapped);
    const int   i1      = (i0 + 1 >= size_) ? 0 : i0 + 1;
    const float frac    = wrapped - static_cast<float>(i0);
    return sample_[i0] + (sample_[i1] - sample_[i0]) * frac;
}

float LooperGranularPlayer::CentsToRatio(float cents)
{
    return powf(2.0f, cents / 1200.0f);
}

float LooperGranularPlayer::MsToSamps(float ms, float samplerate)
{
    return (ms * 0.001f) * samplerate;
}

float LooperGranularPlayer::NegativeInvert(daisysp::Phasor* phs, float frequency)
{
    const float phase = phs->Process();
    return (frequency >= 0.0f) ? phase : 1.0f - phase;
}

float LooperGranularPlayer::Smooth(float current, float target, float coefficient)
{
    return current + (target - current) * coefficient;
}

float LooperGranularPlayer::Process(float speed,
                                    float transposition,
                                    float grain_size)
{
    if(size_ < 2)
        return 0.0f;

    smoothed_speed_         = Smooth(smoothed_speed_, speed, 0.0015f);
    smoothed_transposition_ = Smooth(smoothed_transposition_, transposition, 0.0015f);
    smoothed_grain_size_    = Smooth(smoothed_grain_size_, fmaxf(grain_size, 1.0f), 0.0015f);

    const float playback_speed = smoothed_speed_ * sample_frequency_;
    const float pitch_speed
        = (CentsToRatio(smoothed_transposition_) - smoothed_speed_)
          * (1000.0f / smoothed_grain_size_);

    phs_.SetFreq(fabsf(playback_speed * 0.5f));
    phs2_.SetFreq(fabsf(playback_speed * 0.5f));
    phsImp_.SetFreq(fabsf(pitch_speed));
    phsImp2_.SetFreq(fabsf(pitch_speed));

    const float phase1 = NegativeInvert(&phs_, playback_speed);
    const float phase2 = NegativeInvert(&phs2_, playback_speed);
    const float pitch1 = NegativeInvert(&phsImp_, pitch_speed);
    const float pitch2 = NegativeInvert(&phsImp2_, pitch_speed);

    const float grain_samples = MsToSamps(smoothed_grain_size_, sample_rate_);
    const float idx1          = phase1 * static_cast<float>(size_) + pitch1 * grain_samples;
    const float idx2          = phase2 * static_cast<float>(size_) + pitch2 * grain_samples;

    int env1 = static_cast<int>(phase1 * 255.0f);
    int env2 = static_cast<int>(phase2 * 255.0f);
    if(env1 < 0)
        env1 = 0;
    else if(env1 > 255)
        env1 = 255;
    if(env2 < 0)
        env2 = 0;
    else if(env2 > 255)
        env2 = 255;

    const float sig1 = ReadLinear(idx1) * grain_env_[env1];
    const float sig2 = ReadLinear(idx2) * grain_env_[env2];
    return (sig1 + sig2) * 0.5f;
}
