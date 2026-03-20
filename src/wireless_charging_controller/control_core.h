#ifndef CONTROL_CORE_H
#define CONTROL_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float cc_low_pass(float previous, float input, float alpha);
uint8_t cc_adjust_pwm_step(
    uint8_t pwm,
    float error,
    float posThreshold,
    float negThreshold,
    uint8_t upStep,
    uint8_t downStep,
    uint8_t minVal,
    uint8_t maxVal
);

#ifdef __cplusplus
}
#endif

#endif
