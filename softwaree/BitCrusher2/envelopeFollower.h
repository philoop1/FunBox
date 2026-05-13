#ifndef ENVELOPE_FOLLOWER_H
#define ENVELOPE_FOLLOWER_H

#include <math.h>

class EnvelopeFollower {
public:
    // Initialize with sample rate
    void Init(float sample_rate) {
        sr_ = sample_rate;
        env_ = 0.0f;
        attack_ = 0.01f;  // Default 10ms attack
        release_ = 0.1f;  // Default 100ms release
        UpdateCoefficients();
    }

    // Set attack time in seconds
    void SetAttack(float attack_seconds) {
        attack_ = attack_seconds;
        UpdateCoefficients();
    }

    // Set release time in seconds
    void SetRelease(float release_seconds) {
        release_ = release_seconds;
        UpdateCoefficients();
    }

    // Process input signal and return envelope
    float Process(float input) {
        float abs_input = fabs(input);
        env_ = abs_input > env_ ? attack_coeff_ * env_ + (1.0f - attack_coeff_) * abs_input :
                                  release_coeff_ * env_ + (1.0f - release_coeff_) * abs_input;
        return env_;
    }

    // Getter methods for debugging
    float GetAttack() const { return attack_; }
    float GetRelease() const { return release_; }
    float GetEnvelope() const { return env_; }

private:
    void UpdateCoefficients() {
        attack_coeff_ = exp(-1.0f / (attack_ * sr_));
        release_coeff_ = exp(-1.0f / (release_ * sr_));
    }

    float sr_;
    float env_;
    float attack_;
    float release_;
    float attack_coeff_;
    float release_coeff_;
};

#endif // ENVELOPE_FOLLOWER_H