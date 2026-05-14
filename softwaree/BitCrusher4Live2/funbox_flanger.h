#pragma once
#ifndef FUNBOX_FLANGER_H
#define FUNBOX_FLANGER_H

#include <stdint.h>
#include "Utility/delayline.h"

namespace daisysp
{
class FunboxFlanger
{
  public:
    FunboxFlanger() {}
    ~FunboxFlanger() {}

    void  Init(float sample_rate);
    float Process(float in);
    void  SetDelayMs(float delay_ms);
    void  SetFeedback(float feedback);
    void  SetWet(float wet);

  private:
    static constexpr int32_t kDelayLength = 2400;

    float sample_rate_;
    float delay_samples_;
    float feedback_;
    float wet_;
    float last_delay_;
    float last_sample_;

    DelayLine<float, kDelayLength> del_;
};
} // namespace daisysp

#endif
