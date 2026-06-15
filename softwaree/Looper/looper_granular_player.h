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
    float Process(float speed, float transposition, float grain_size);

  private:
    float WrapPosition(float idx, float size);
    float ReadLinear(float idx);
    float CentsToRatio(float cents);
    float MsToSamps(float ms, float samplerate);
    float NegativeInvert(daisysp::Phasor* phs, float frequency);
    float Smooth(float current, float target, float coefficient);

    float* sample_;
    float  sample_rate_;
    int    size_;
    float  sample_frequency_;
    float  grain_env_[256] = {0};

    float smoothed_speed_;
    float smoothed_transposition_;
    float smoothed_grain_size_;

    daisysp::Phasor phs_;
    daisysp::Phasor phsImp_;
    daisysp::Phasor phs2_;
    daisysp::Phasor phsImp2_;
};
} // namespace funbox

#endif
