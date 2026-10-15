/*
 * gpio_ctl.c - command-line control of the virtual GPIO table (make SIM=1).
 *
 *   gpio_ctl set <pin|name> <0|1>        drive an input pin
 *   gpio_ctl get <pin|name>              print a pin level
 *   gpio_ctl pwm <pin|name>              print a soft-PWM value
 *   gpio_ctl pulse <pin|name> <hz> <s>   square wave (hz rising edges per second)
 *   gpio_ctl dump                        list every pin with a non-zero state
 *   gpio_ctl reset                       delete the virtual GPIO table
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#include "common.h"
#include "gpio_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

static const struct {
    const char *name;
    int pin;
} PIN_NAMES[] = {
    { "motor-dir1", PIN_MOTOR_DIR1 },           { "motor-dir2", PIN_MOTOR_DIR2 },
    { "motor-pwm", PIN_MOTOR_PWM },             { "brake-pwm", PIN_BRAKE_PWM },
    { "accel", PIN_ACCEL_PEDAL },               { "brake", PIN_BRAKE_PEDAL },
    { "hall-motor", PIN_HALL_MOTOR },           { "hall-wheel-a", PIN_HALL_WHEEL_A },
    { "hall-wheel-b", PIN_HALL_WHEEL_B },       { "lamp-low", PIN_LAMP_LOW_BEAM },
    { "lamp-high", PIN_LAMP_HIGH_BEAM },        { "lamp-brake", PIN_LAMP_BRAKE },
    { "lamp-left", PIN_LAMP_LEFT_SIGNAL },      { "lamp-right", PIN_LAMP_RIGHT_SIGNAL },
    { "lamp-temp", PIN_LAMP_ENGINE_TEMP },      { "cmd-low", PIN_CMD_LOW_BEAM },
    { "cmd-high", PIN_CMD_HIGH_BEAM },          { "cmd-left", PIN_CMD_LEFT_SIGNAL },
    { "cmd-right", PIN_CMD_RIGHT_SIGNAL },      { "cc-resume", PIN_CMD_CC_RESUME },
    { "cc-cancel", PIN_CMD_CC_CANCEL },
};
#define PIN_NAME_COUNT (sizeof PIN_NAMES / sizeof PIN_NAMES[0])

static int usage(void)
{
    fprintf(stderr,
            "usage: gpio_ctl set <pin|name> <0|1>\n"
            "       gpio_ctl get <pin|name>\n"
            "       gpio_ctl pwm <pin|name>\n"
            "       gpio_ctl pulse <pin|name> <hz> <seconds>\n"
            "       gpio_ctl dump\n"
            "       gpio_ctl reset\n");
    return EXIT_FAILURE;
}

static int resolve_pin(const char *token)
{
    char *end;

    for (size_t i = 0; i < PIN_NAME_COUNT; i++) {
        if (strcmp(token, PIN_NAMES[i].name) == 0) {
            return PIN_NAMES[i].pin;
        }
    }
    long pin = strtol(token, &end, 10);
    if (*token == '\0' || *end != '\0' || pin < 0 || pin >= SIM_GPIO_PINS) {
        fprintf(stderr, "unknown pin: %s\n", token);
        return -1;
    }
    return (int)pin;
}

static void sleep_seconds(double seconds)
{
    struct timespec ts;

    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

static const char *pin_label(int pin)
{
    for (size_t i = 0; i < PIN_NAME_COUNT; i++) {
        if (PIN_NAMES[i].pin == pin) {
            return PIN_NAMES[i].name;
        }
    }
    return "-";
}

static int cmd_pulse(SimGpio *gpio, int pin, const char *hz_arg, const char *seconds_arg)
{
    double hz = strtod(hz_arg, NULL);
    double seconds = strtod(seconds_arg, NULL);

    if (hz <= 0.0 || hz > 1000.0 || seconds <= 0.0) {
        return usage();
    }
    long cycles = (long)(hz * seconds);
    for (long i = 0; i < cycles; i++) {
        gpio->level[pin] = 1;
        sleep_seconds(0.5 / hz);
        gpio->level[pin] = 0;
        sleep_seconds(0.5 / hz);
    }
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        return usage();
    }
    if (strcmp(argv[1], "reset") == 0) {
        shm_unlink(SIM_GPIO_SHM);
        return EXIT_SUCCESS;
    }

    SimGpio *gpio = sim_gpio_attach();
    if (gpio == NULL) {
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "dump") == 0) {
        for (int pin = 0; pin < SIM_GPIO_PINS; pin++) {
            if (gpio->level[pin] != 0 || gpio->pwm_value[pin] != 0 || gpio->pwm_range[pin] != 0) {
                printf("GPIO %2d %-13s level=%d pwm=%d/%d\n", pin, pin_label(pin),
                       gpio->level[pin], gpio->pwm_value[pin], gpio->pwm_range[pin]);
            }
        }
        return EXIT_SUCCESS;
    }

    if (argc < 3) {
        return usage();
    }
    int pin = resolve_pin(argv[2]);
    if (pin < 0) {
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "get") == 0) {
        printf("%d\n", gpio->level[pin]);
    } else if (strcmp(argv[1], "pwm") == 0) {
        printf("%d\n", gpio->pwm_value[pin]);
    } else if (strcmp(argv[1], "set") == 0 && argc == 4) {
        gpio->level[pin] = atoi(argv[3]) != 0 ? 1 : 0;
    } else if (strcmp(argv[1], "pulse") == 0 && argc == 5) {
        return cmd_pulse(gpio, pin, argv[3], argv[4]);
    } else {
        return usage();
    }
    return EXIT_SUCCESS;
}
