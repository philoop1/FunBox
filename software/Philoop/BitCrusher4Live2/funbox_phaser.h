#pragma once
#ifndef FUNBOX_PHASER_H
#define FUNBOX_PHASER_H

#include <stdint.h>
#include "Utility/delayline.h"

namespace daisysp
{
class FunboxPhaserEngine
{
  public:
    FunboxPhaserEngine() {}
    ~FunboxPhaserEngine() {}

    void  Init(float sample_rate);
    float Process(float in);
    void  SetFreq(float ap_freq);
    void  SetFeedback(float feedback);

  private:
    static constexpr int32_t kDelayLength = 2400;

    float sample_rate_;
    float os_;
    float feedback_;
    float ap_freq_;
    float deltime_;
    float last_sample_;

    DelayLine<float, kDelayLength> del_;
};

class FunboxPhaser
{
  public:
    FunboxPhaser() {}
    ~FunboxPhaser() {}

    void  Init(float sample_rate);
    float Process(float in);
    void  SetPoles(int poles);
    void  SetFreq(float ap_freq);
    void  SetFeedback(float feedback);

  private:
    static constexpr int kMaxPoles = 8;
    FunboxPhaserEngine  engines_[kMaxPoles];
    int                 poles_;
};
} // namespace daisysp

#endif
