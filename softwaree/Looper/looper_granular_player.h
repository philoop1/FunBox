#pragma once
#ifndef LOOPER_GRANULAR_PLAYER_H
#define LOOPER_GRANULAR_PLAYER_H

#include <stdint.h>
#include <cmath>
#include "Control/phasor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace funbox
{
class LooperGranularPlayer
{
  public:
    LooperGranularPlayer() {}
    ~LooperGranularPlayer() {}

    void Init(float* sample, int size, float sample_rate);
    float Process(float speed,
                  float transposition,
                  float grain_size,
                  float direction_mode,
                  float density,
                  float window_shape,
                  float buffer_age,
                  bool age_continuous,
                  float position_random,
                  float size_random,
                  float pitch_random);
    float ProcessFromReader(float base_position,
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
                            float pitch_random);
    void ProcessFromStereoReader(float base_position,
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
                                 float& out_r);

  private:
    static constexpr int kMaxGrains = 8;

    float WrapPosition(float idx, float size);
    float ReadLinear(float idx);
    float CentsToRatio(float cents);
    float MsToSamps(float ms, float samplerate);
    float Smooth(float current, float target, float coefficient);
    float RandomFloat();
    float Window(float phase, float shape);
    int VoiceCount(float density);
    void ResetVoicePhases(int voice_count, float density);
    float DirectionSign(float direction_mode);

    float* sample_;
    float  sample_rate_;
    int    size_;
    float  sample_frequency_;
    float  grain_env_[256] = {0};

    float smoothed_speed_;
    float smoothed_transposition_;
    float smoothed_grain_size_;
    float smoothed_density_;
    float smoothed_window_shape_;
    uint32_t random_seed_;
    int active_voice_count_;
    float phase_[kMaxGrains];
    float pitch_phase_[kMaxGrains];
    float speed_sign_[kMaxGrains];
    float age_offset_[kMaxGrains];
    float position_offset_[kMaxGrains];
    float size_factor_[kMaxGrains];
    float pitch_random_cents_[kMaxGrains];
};
} // namespace funbox

#endif
