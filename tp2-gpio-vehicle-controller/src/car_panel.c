/*
 * car_panel.c - driver controls panel (process).
 *
 * Watches the pedal and button GPIO inputs and turns every change into a
 * command for the controller (POSIX message queue). Pedals are level inputs
 * (a command on press and on release); buttons act on the rising edge.
 *
 * Keyboard: type '+' or '-' followed by Enter to move the cruise set point.
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#include "common.h"

#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define POLL_INTERVAL_US  50000
#define STATUS_PERIOD_MS  1000

typedef struct {
    int pin;
    CommandType command;
    bool is_pedal;     /* true: send on both edges with the pedal duty */
    int pressed_value; /* duty sent when a pedal is pressed */
} Input;

static const Input INPUTS[] = {
    { PIN_ACCEL_PEDAL,      CMD_ACCELERATE,    true,  ACCEL_DUTY_PERCENT },
    { PIN_BRAKE_PEDAL,      CMD_BRAKE,         true,  BRAKE_DUTY_PERCENT },
    { PIN_CMD_LOW_BEAM,     CMD_LOW_BEAM,      false, 0 },
    { PIN_CMD_HIGH_BEAM,    CMD_HIGH_BEAM,     false, 0 },
    { PIN_CMD_LEFT_SIGNAL,  CMD_LEFT_SIGNAL,   false, 0 },
    { PIN_CMD_RIGHT_SIGNAL, CMD_RIGHT_SIGNAL,  false, 0 },
    { PIN_CMD_CC_RESUME,    CMD_CRUISE_RESUME, false, 0 },
    { PIN_CMD_CC_CANCEL,    CMD_CRUISE_CANCEL, false, 0 },
};
#define INPUT_COUNT (sizeof INPUTS / sizeof INPUTS[0])

static mqd_t mq = (mqd_t)-1;
static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static void send_command(CommandType type, int value)
{
    Command cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.type = type;
    cmd.value = value;
    if (mq_send(mq, (const char *)&cmd, sizeof cmd, 0) == -1) {
        perror("\nmq_send");
    }
}

static void init_gpio(void)
{
    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "Failed to initialise GPIO (are you root?)\n");
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < INPUT_COUNT; i++) {
        pinMode(INPUTS[i].pin, INPUT);
        pullUpDnControl(INPUTS[i].pin, PUD_DOWN); /* idle level is LOW */
    }
}

static void handle_input_change(const Input *input, int level)
{
    if (input->is_pedal) {
        send_command(input->command, level == HIGH ? input->pressed_value : 0);
    } else if (level == HIGH) {
        send_command(input->command, 0);
    }
}

static void handle_keyboard(bool *stdin_open)
{
    char line[32];

    if (fgets(line, sizeof line, stdin) == NULL) {
        *stdin_open = false; /* no terminal attached: keep polling the GPIO only */
        return;
    }
    if (strchr(line, '+') != NULL) {
        send_command(CMD_CRUISE_SET_UP, 0);
    } else if (strchr(line, '-') != NULL) {
        send_command(CMD_CRUISE_SET_DOWN, 0);
    }
}

static void print_status(void)
{
    printf("\rAccel: %d | Brake: %d | Low beam: %d | High beam: %d | Left: %d | Right: %d | "
           "CC resume: %d | CC cancel: %d\033[K",
           digitalRead(PIN_ACCEL_PEDAL), digitalRead(PIN_BRAKE_PEDAL),
           digitalRead(PIN_CMD_LOW_BEAM), digitalRead(PIN_CMD_HIGH_BEAM),
           digitalRead(PIN_CMD_LEFT_SIGNAL), digitalRead(PIN_CMD_RIGHT_SIGNAL),
           digitalRead(PIN_CMD_CC_RESUME), digitalRead(PIN_CMD_CC_CANCEL));
    fflush(stdout);
}

int main(void)
{
    int previous[INPUT_COUNT];
    bool stdin_open = true;
    long long last_status = 0;

    install_signal_handlers();
    init_gpio();

    mq = mq_open(MQ_NAME, O_WRONLY | O_NONBLOCK);
    if (mq == (mqd_t)-1) {
        perror("mq_open (is ./controller running?)");
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < INPUT_COUNT; i++) {
        previous[i] = digitalRead(INPUTS[i].pin);
    }

    printf("Panel started. Monitoring inputs...\n");
    printf("Press Ctrl+C to exit. Type '+' or '-' and Enter to adjust the cruise set point.\n");

    while (running) {
        fd_set readable;
        struct timeval timeout;

        FD_ZERO(&readable);
        if (stdin_open) {
            FD_SET(STDIN_FILENO, &readable);
        }
        timeout.tv_sec = 0;
        timeout.tv_usec = POLL_INTERVAL_US;
        if (select(STDIN_FILENO + 1, &readable, NULL, NULL, &timeout) > 0 &&
            FD_ISSET(STDIN_FILENO, &readable)) {
            handle_keyboard(&stdin_open);
        }

        for (size_t i = 0; i < INPUT_COUNT; i++) {
            int level = digitalRead(INPUTS[i].pin);
            if (level != previous[i]) {
                handle_input_change(&INPUTS[i], level);
                previous[i] = level;
            }
        }

        long long now = monotonic_ms();
        if (now - last_status >= STATUS_PERIOD_MS) {
            print_status();
            last_status = now;
        }
    }

    printf("\nClosing the panel...\n");
    mq_close(mq);
    return EXIT_SUCCESS;
}
