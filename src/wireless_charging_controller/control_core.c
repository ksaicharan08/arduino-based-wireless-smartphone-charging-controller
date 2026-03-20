#include "control_core.h"

float cc_low_pass(float previous, float input, float alpha) {
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    return previous + alpha * (input - previous);
}

uint8_t cc_adjust_pwm_step(
    uint8_t pwm,
    float error,
    float posThreshold,
    float negThreshold,
    uint8_t upStep,
    uint8_t downStep,
    uint8_t minVal,
    uint8_t maxVal
) {
    uint16_t nextPwm = pwm;

    if (error > posThreshold) {
        nextPwm = (uint16_t)pwm + upStep;
    } else if (error < negThreshold) {
        if (pwm > downStep) {
            nextPwm = (uint16_t)pwm - downStep;
        } else {
            nextPwm = 0;
        }
    }

    if (nextPwm < minVal) {
        nextPwm = minVal;
    }
    if (nextPwm > maxVal) {
        nextPwm = maxVal;
    }

    return (uint8_t)nextPwm;
}
