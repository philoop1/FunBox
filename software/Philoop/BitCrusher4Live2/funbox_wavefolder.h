#pragma once

class FunboxWavefolder
{
  public:
    void Init();
    float Process(float in,
                  float threshold_control,
                  float env_to_threshold,
                  float lfo_to_threshold,
                  float lfo_signal,
                  float env);

  private:
    float Fold(float in) const;
    float Clamp01(float v) const;
    float BipolarDeadband(float v) const;
    float ExpMap(float knob, float min_v, float max_v) const;
};
