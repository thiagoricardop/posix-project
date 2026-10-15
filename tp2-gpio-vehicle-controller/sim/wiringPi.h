/*
 * wiringPi.h - minimal GPIO shim used by `make SIM=1`.
 *
 * Provides the subset of the wiringPi API used by this project on top of a
 * POSIX shared-memory pin table, so the whole system can be built and run on
 * any Linux machine (see sim/gpio_sim.c and tools/gpio_ctl.c).
 */
#ifndef WIRINGPI_SIM_H
#define WIRINGPI_SIM_H

#define INPUT    0
#define OUTPUT   1
#define LOW      0
#define HIGH     1
#define PUD_OFF  0
#define PUD_DOWN 1
#define PUD_UP   2

int wiringPiSetupGpio(void);
void pinMode(int pin, int mode);
void pullUpDnControl(int pin, int pud);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
unsigned int millis(void);

#endif /* WIRINGPI_SIM_H */
