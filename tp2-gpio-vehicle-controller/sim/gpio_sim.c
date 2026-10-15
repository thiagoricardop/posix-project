/*
 * gpio_sim.c - wiringPi / softPwm replacement backed by POSIX shared memory.
 *
 * Every process that links this file sees the same virtual pin table, which
 * tools/gpio_ctl.c can read and drive from a shell.
 */
#include "gpio_sim.h"
#include "softPwm.h"
#include "wiringPi.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int valid_pin(int pin)
{
    return pin >= 0 && pin < SIM_GPIO_PINS;
}

SimGpio *sim_gpio_attach(void)
{
    static SimGpio *table;

    if (table != NULL) {
        return table;
    }

    int fd = shm_open(SIM_GPIO_SHM, O_CREAT | O_RDWR, 0600);
    if (fd == -1) {
        perror("sim gpio: shm_open");
        return NULL;
    }
    if (ftruncate(fd, (off_t)sizeof(SimGpio)) == -1) {
        perror("sim gpio: ftruncate");
        close(fd);
        return NULL;
    }
    void *mapping = mmap(NULL, sizeof(SimGpio), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) {
        perror("sim gpio: mmap");
        return NULL;
    }
    table = mapping;
    return table;
}

int wiringPiSetupGpio(void)
{
    return sim_gpio_attach() != NULL ? 0 : -1;
}

void pinMode(int pin, int mode)
{
    (void)pin;
    (void)mode;
}

void pullUpDnControl(int pin, int pud)
{
    (void)pin;
    (void)pud;
}

void digitalWrite(int pin, int value)
{
    SimGpio *table = sim_gpio_attach();

    if (table != NULL && valid_pin(pin)) {
        table->level[pin] = value ? HIGH : LOW;
    }
}

int digitalRead(int pin)
{
    SimGpio *table = sim_gpio_attach();

    return (table != NULL && valid_pin(pin)) ? table->level[pin] : LOW;
}

unsigned int millis(void)
{
    static struct timespec start;
    static int started;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!started) {
        start = now;
        started = 1;
    }
    return (unsigned int)((now.tv_sec - start.tv_sec) * 1000L +
                          (now.tv_nsec - start.tv_nsec) / 1000000L);
}

int softPwmCreate(int pin, int initialValue, int pwmRange)
{
    SimGpio *table = sim_gpio_attach();

    if (table == NULL || !valid_pin(pin)) {
        return -1;
    }
    table->pwm_range[pin] = pwmRange;
    table->pwm_value[pin] = initialValue;
    return 0;
}

void softPwmWrite(int pin, int value)
{
    SimGpio *table = sim_gpio_attach();

    if (table != NULL && valid_pin(pin)) {
        table->pwm_value[pin] = value;
    }
}

void softPwmStop(int pin)
{
    softPwmWrite(pin, 0);
}
