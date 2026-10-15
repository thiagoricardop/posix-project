/*
 * softPwm.h - soft PWM shim used by `make SIM=1` (see wiringPi.h).
 */
#ifndef SOFTPWM_SIM_H
#define SOFTPWM_SIM_H

int softPwmCreate(int pin, int initialValue, int pwmRange);
void softPwmWrite(int pin, int value);
void softPwmStop(int pin);

#endif /* SOFTPWM_SIM_H */
