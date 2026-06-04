#pragma once

#include <stddef.h>

class HarmonicNotchFilter
{
  public:
    static constexpr size_t kMaxHarmonics = 16;

    void Init(float sample_rate);
    void SetFundamentalHz(float hz);
    void SetQ(float q);
    void SetHarmonicCount(size_t count);
    void SetDepth(float depth);
    void SetSlope24dB(bool enabled);
    void SetExtraStageMix(float mix);
    void ProcessStereo(float in_l, float in_r, float& out_l, float& out_r);

  private:
    struct BiquadNotch
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1_l = 0.0f, z2_l = 0.0f;
        float z1_r = 0.0f, z2_r = 0.0f;

        void Set(float sample_rate, float freq_hz, float q);
        float ProcessL(float x);
        float ProcessR(float x);
        void Reset();
    };

    void Rebuild();
    float ClampFreq(float hz) const;

    float sample_rate_ = 48000.0f;
    float fundamental_hz_ = 1000.0f;
    float q_ = 20.0f;
    size_t harmonic_count_ = 8;
    float depth_ = 1.0f;
    bool slope24db_ = false;
    BiquadNotch stage_a_[kMaxHarmonics];
    BiquadNotch stage_b_[kMaxHarmonics];
    BiquadNotch stage_c_[kMaxHarmonics];
    float extra_stage_mix_ = 0.0f;
};
