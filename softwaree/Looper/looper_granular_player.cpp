#include "looper_granular_player.h"

using namespace funbox;

void LooperGranularPlayer::Init(float* sample, int size, float sample_rate)
{
    sample_      = sample;
    size_        = size;
    sample_rate_ = sample_rate;

    sample_frequency_       = sample_rate_ / static_cast<float>(size_);
    smoothed_speed_         = 1.0f;
    smoothed_transposition_ = 0.0f;
    smoothed_grain_size_    = 110.0f;
    smoothed_density_       = 0.0f;
    smoothed_window_shape_  = 1.0f;
    random_seed_            = 0x1234abcd;
    active_voice_count_     = 2;

    for(int i = 0; i < 256; i++)
    {
        const float phase = static_cast<float>(i) / 255.0f;
        grain_env_[i] = 0.5f - 0.5f * cosf(2.0f * M_PI * phase);
    }

    ResetVoicePhases(active_voice_count_, smoothed_density_);
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

float LooperGranularPlayer::Smooth(float current, float target, float coefficient)
{
    return current + (target - current) * coefficient;
}

float LooperGranularPlayer::RandomFloat()
{
    random_seed_ = random_seed_ * 1664525u + 1013904223u;
    return static_cast<float>((random_seed_ >> 8) & 0x00ffffff)
           * (1.0f / 16777215.0f);
}

float LooperGranularPlayer::Window(float phase, float shape)
{
    int env_index = static_cast<int>(phase * 255.0f);
    if(env_index < 0)
        env_index = 0;
    else if(env_index > 255)
        env_index = 255;

    const float hann = grain_env_[env_index];
    const float clipped_shape = fmaxf(0.0f, fminf(shape, 1.0f));
    if(clipped_shape < 0.5f)
    {
        const float t = clipped_shape * 2.0f;
        return sqrtf(hann) * (1.0f - t) + hann * t;
    }

    const float t = (clipped_shape - 0.5f) * 2.0f;
    return hann * (1.0f - t) + (hann * hann) * t;
}

int LooperGranularPlayer::VoiceCount(float density)
{
    if(density < 0.50f)
        return 2;
    return 4;
}

void LooperGranularPlayer::ResetVoicePhases(int voice_count, float density)
{
    const bool cloud = density > 0.90f;
    for(int i = 0; i < kMaxGrains; i++)
    {
        phase_[i] = cloud ? RandomFloat()
                          : static_cast<float>(i) / static_cast<float>(voice_count);
        pitch_phase_[i] = 0.0f;
        speed_sign_[i] = 1.0f;
        age_offset_[i] = 0.0f;
        position_offset_[i] = 0.0f;
        size_factor_[i] = 1.0f;
        pitch_random_cents_[i] = 0.0f;
        speed_drift_[i] = 0.0f;
    }
}

float LooperGranularPlayer::DirectionSign(float direction_mode)
{
    if(direction_mode <= 0.001f)
        return 1.0f;
    if(direction_mode >= 0.999f)
        return -1.0f;
    return RandomFloat() < direction_mode ? -1.0f : 1.0f;
}

float LooperGranularPlayer::Process(float speed,
                                    float transposition,
                                    float grain_size,
                                    float direction_mode,
                                    float density,
                                    float window_shape,
                                    float buffer_age,
                                    bool age_continuous,
                                    float position_random,
                                    float size_random,
                                    float pitch_random)
{
    if(size_ < 2)
        return 0.0f;

    smoothed_speed_         = Smooth(smoothed_speed_, speed, 0.0015f);
    smoothed_transposition_ = Smooth(smoothed_transposition_, transposition, 0.0015f);
    smoothed_grain_size_    = Smooth(smoothed_grain_size_, fmaxf(grain_size, 1.0f), 0.0015f);
    smoothed_density_       = Smooth(smoothed_density_, fmaxf(0.0f, fminf(density, 1.0f)), 0.0015f);
    smoothed_window_shape_  = Smooth(smoothed_window_shape_, fmaxf(0.0f, fminf(window_shape, 1.0f)), 0.0015f);

    const int voice_count = VoiceCount(smoothed_density_);
    if(voice_count != active_voice_count_)
    {
        active_voice_count_ = voice_count;
        ResetVoicePhases(active_voice_count_, smoothed_density_);
    }

    const float playback_speed = smoothed_speed_ * sample_frequency_;
    const float phase_inc = fabsf(playback_speed * 0.5f) / sample_rate_;
    const float grain_samples = MsToSamps(smoothed_grain_size_, sample_rate_);
    const float requested_age_offset = fmaxf(0.0f, fminf(buffer_age, 1.0f))
                                       * static_cast<float>(size_);
    const float random_width = fmaxf(0.0f, fminf(position_random, 1.0f))
                               * static_cast<float>(size_) * 0.5f;
    const float size_random_amount = fmaxf(0.0f, fminf(size_random, 1.0f));
    const float pitch_random_amount = fmaxf(0.0f, fminf(pitch_random, 1200.0f));
    float sum = 0.0f;

    for(int i = 0; i < active_voice_count_; i++)
    {
        phase_[i] += phase_inc;
        if(phase_[i] >= 1.0f)
        {
            phase_[i] -= 1.0f;
            speed_sign_[i] = DirectionSign(direction_mode);
            age_offset_[i] = requested_age_offset;
            position_offset_[i] = (RandomFloat() * 2.0f - 1.0f) * random_width;
            const float random_size = 0.20f + RandomFloat() * 2.80f;
            size_factor_[i] = 1.0f + (random_size - 1.0f) * size_random_amount;
            pitch_random_cents_[i] = (RandomFloat() * 2.0f - 1.0f) * pitch_random_amount;
        }

        const float voice_size = fmaxf(size_factor_[i], 0.20f);
        const float voice_pitch = smoothed_transposition_ + pitch_random_cents_[i];
        const float pitch_speed
            = (CentsToRatio(voice_pitch) - smoothed_speed_)
              * (1000.0f / smoothed_grain_size_);
        const float pitch_inc = fabsf(pitch_speed / voice_size) / sample_rate_;
        pitch_phase_[i] += pitch_inc;
        if(pitch_phase_[i] >= 1.0f)
            pitch_phase_[i] -= 1.0f;

        const float pitch_phase = pitch_speed >= 0.0f ? pitch_phase_[i] : 1.0f - pitch_phase_[i];
        const float read_phase = speed_sign_[i] < 0.0f ? 1.0f - phase_[i] : phase_[i];
        const float age_offset = age_continuous ? requested_age_offset : age_offset_[i];
        const float grain_samples_for_voice = grain_samples * voice_size;
        const float idx = read_phase * static_cast<float>(size_) + pitch_phase * grain_samples_for_voice
                          - age_offset + position_offset_[i];
        sum += ReadLinear(idx) * Window(phase_[i], smoothed_window_shape_);
    }

    return sum / static_cast<float>(active_voice_count_);
}

float LooperGranularPlayer::ProcessFromReader(float base_position,
                                              int source_size,
                                              float (*reader)(float),
                                              float speed,
                                              float transposition,
                                              float grain_size,
                                              float direction_mode,
                                              float density,
                                              float window_shape,
                                              float buffer_age,
                                              bool age_continuous,
                                              float position_random,
                                              float size_random,
                                              float pitch_random)
{
    if(source_size < 2 || reader == nullptr)
        return 0.0f;

    smoothed_speed_         = Smooth(smoothed_speed_, speed, 0.0015f);
    smoothed_transposition_ = Smooth(smoothed_transposition_, transposition, 0.0015f);
    smoothed_grain_size_    = Smooth(smoothed_grain_size_, fmaxf(grain_size, 1.0f), 0.0015f);
    smoothed_density_       = Smooth(smoothed_density_, fmaxf(0.0f, fminf(density, 1.0f)), 0.0015f);
    smoothed_window_shape_  = Smooth(smoothed_window_shape_, fmaxf(0.0f, fminf(window_shape, 1.0f)), 0.0015f);

    const int voice_count = VoiceCount(smoothed_density_);
    if(voice_count != active_voice_count_)
    {
        active_voice_count_ = voice_count;
        ResetVoicePhases(active_voice_count_, smoothed_density_);
    }

    const float source_size_f = static_cast<float>(source_size);
    const float source_frequency = sample_rate_ / source_size_f;
    const float playback_speed = smoothed_speed_ * source_frequency;
    const float phase_inc = fabsf(playback_speed * 0.5f) / sample_rate_;
    const float grain_samples = MsToSamps(smoothed_grain_size_, sample_rate_);
    const float requested_age_offset = fmaxf(0.0f, fminf(buffer_age, 1.0f)) * source_size_f;
    const float random_width = fmaxf(0.0f, fminf(position_random, 1.0f)) * source_size_f * 0.5f;
    const float size_random_amount = fmaxf(0.0f, fminf(size_random, 1.0f));
    const float pitch_random_amount = fmaxf(0.0f, fminf(pitch_random, 1200.0f));
    float sum = 0.0f;

    for(int i = 0; i < active_voice_count_; i++)
    {
        phase_[i] += phase_inc;
        if(phase_[i] >= 1.0f)
        {
            phase_[i] -= 1.0f;
            speed_sign_[i] = DirectionSign(direction_mode);
            age_offset_[i] = requested_age_offset;
            position_offset_[i] = (RandomFloat() * 2.0f - 1.0f) * random_width;
            const float random_size = 0.20f + RandomFloat() * 2.80f;
            size_factor_[i] = 1.0f + (random_size - 1.0f) * size_random_amount;
            pitch_random_cents_[i] = (RandomFloat() * 2.0f - 1.0f) * pitch_random_amount;
        }

        const float voice_size = fmaxf(size_factor_[i], 0.20f);
        const float voice_pitch = smoothed_transposition_ + pitch_random_cents_[i];
        const float pitch_speed
            = (CentsToRatio(voice_pitch) - smoothed_speed_)
              * (1000.0f / smoothed_grain_size_);
        const float pitch_inc = fabsf(pitch_speed / voice_size) / sample_rate_;
        pitch_phase_[i] += pitch_inc;
        if(pitch_phase_[i] >= 1.0f)
            pitch_phase_[i] -= 1.0f;

        const float pitch_phase = pitch_speed >= 0.0f ? pitch_phase_[i] : 1.0f - pitch_phase_[i];
        const float grain_scan = speed_sign_[i] < 0.0f ? 1.0f - pitch_phase : pitch_phase;
        const float age_offset = age_continuous ? requested_age_offset : age_offset_[i];
        const float grain_samples_for_voice = grain_samples * voice_size;
        const float idx = base_position + grain_scan * grain_samples_for_voice
                          - age_offset + position_offset_[i];
        sum += reader(WrapPosition(idx, source_size_f)) * Window(phase_[i], smoothed_window_shape_);
    }

    return sum / static_cast<float>(active_voice_count_);
}

void LooperGranularPlayer::ProcessFromStereoReader(float base_position,
                                                   int source_size,
                                                   void (*reader)(float, float&, float&),
                                                   float speed,
                                                   float transposition,
                                                   float grain_size,
                                                   float direction_mode,
                                                   float density,
                                                   float window_shape,
                                                   float buffer_age,
                                                   bool age_continuous,
                                                   float position_random,
                                                   float size_random,
                                                   float pitch_random,
                                                   float& out_l,
                                                   float& out_r)
{
    out_l = 0.0f;
    out_r = 0.0f;
    if(source_size < 2 || reader == nullptr)
        return;

    smoothed_speed_         = Smooth(smoothed_speed_, speed, 0.0015f);
    smoothed_transposition_ = Smooth(smoothed_transposition_, transposition, 0.0015f);
    smoothed_grain_size_    = Smooth(smoothed_grain_size_, fmaxf(grain_size, 1.0f), 0.0015f);
    smoothed_density_       = Smooth(smoothed_density_, fmaxf(0.0f, fminf(density, 1.0f)), 0.0015f);
    smoothed_window_shape_  = Smooth(smoothed_window_shape_, fmaxf(0.0f, fminf(window_shape, 1.0f)), 0.0015f);

    const int voice_count = VoiceCount(smoothed_density_);
    if(voice_count != active_voice_count_)
    {
        active_voice_count_ = voice_count;
        ResetVoicePhases(active_voice_count_, smoothed_density_);
    }

    const float source_size_f = static_cast<float>(source_size);
    const float grain_samples = MsToSamps(smoothed_grain_size_, sample_rate_);
    const float requested_age_offset = fmaxf(0.0f, fminf(buffer_age, 1.0f)) * source_size_f;
    const float random_width = fmaxf(0.0f, fminf(position_random, 1.0f)) * source_size_f * 0.5f;
    const float size_random_amount = fmaxf(0.0f, fminf(size_random, 1.0f));
    const float pitch_random_amount = fmaxf(0.0f, fminf(pitch_random, 1200.0f));
    const float drift_speed = smoothed_speed_ * 0.50f;

    for(int i = 0; i < active_voice_count_; i++)
    {
        const float voice_size_for_phase = fmaxf(size_factor_[i], 0.20f);
        const float grain_samples_for_phase = fmaxf(grain_samples * voice_size_for_phase, 8.0f);
        const float phase_inc = 1.0f / grain_samples_for_phase;
        phase_[i] += phase_inc;
        if(phase_[i] >= 1.0f)
        {
            phase_[i] -= 1.0f;
            speed_sign_[i] = DirectionSign(direction_mode);
            age_offset_[i] = requested_age_offset;
            position_offset_[i] = (RandomFloat() * 2.0f - 1.0f) * random_width;
            const float random_size = 0.20f + RandomFloat() * 2.80f;
            size_factor_[i] = 1.0f + (random_size - 1.0f) * size_random_amount;
            pitch_random_cents_[i] = (RandomFloat() * 2.0f - 1.0f) * pitch_random_amount;
        }

        const float voice_size = fmaxf(size_factor_[i], 0.20f);
        const float voice_pitch = smoothed_transposition_ + pitch_random_cents_[i];
        const float pitch_phase = phase_[i] * CentsToRatio(voice_pitch);
        const float grain_scan = speed_sign_[i] < 0.0f ? 1.0f - pitch_phase : pitch_phase;
        const float age_offset = age_continuous ? requested_age_offset : age_offset_[i];
        const float grain_samples_for_voice = grain_samples * voice_size;
        speed_drift_[i] = WrapPosition(speed_drift_[i] + drift_speed, source_size_f);
        const float idx = base_position + speed_drift_[i] + grain_scan * grain_samples_for_voice
                          - age_offset + position_offset_[i];
        float sample_l;
        float sample_r;
        reader(WrapPosition(idx, source_size_f), sample_l, sample_r);
        const float window = Window(phase_[i], smoothed_window_shape_);
        out_l += sample_l * window;
        out_r += sample_r * window;
    }

    const float scale = 1.0f / static_cast<float>(active_voice_count_);
    out_l *= scale;
    out_r *= scale;
}
