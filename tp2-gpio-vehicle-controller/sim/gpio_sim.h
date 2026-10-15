/*
 * gpio_sim.h - virtual GPIO table shared by every simulated process.
 */
#ifndef GPIO_SIM_H
#define GPIO_SIM_H

#define SIM_GPIO_SHM  "/tp2_sim_gpio"
#define SIM_GPIO_PINS 64

typedef struct {
    volatile int level[SIM_GPIO_PINS];
    volatile int pwm_value[SIM_GPIO_PINS];
    volatile int pwm_range[SIM_GPIO_PINS];
} SimGpio;

/* Maps the shared pin table, creating it on first use. Returns NULL on failure. */
SimGpio *sim_gpio_attach(void);

#endif /* GPIO_SIM_H */
