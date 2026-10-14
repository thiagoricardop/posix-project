/*
 * car_panel.c - car command panel (process).
 *
 * Text menu that simulates the driver's controls. Every action is sent to the
 * controller through a POSIX message queue.
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#include "common.h"

#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum read_result { READ_OK, READ_INVALID, READ_EOF };

static mqd_t mq = (mqd_t)-1;

static void print_menu(void)
{
    printf("=== Car Panel ===\n");
    printf("1 - Press accelerator pedal\n");
    printf("2 - Press brake pedal\n");
    printf("3 - Toggle low beam\n");
    printf("4 - Toggle high beam\n");
    printf("5 - Toggle left turn signal\n");
    printf("6 - Toggle right turn signal\n");
    printf("0 - Exit (ends the test)\n");
    printf("Choose an option: ");
    fflush(stdout);
}

static void send_command(char command, int pedal)
{
    Message msg;

    memset(&msg, 0, sizeof msg);
    msg.command = command;
    msg.pedal = pedal;
    if (mq_send(mq, (const char *)&msg, sizeof msg, 0) == -1) {
        perror("mq_send");
    }
}

static enum read_result read_percentage(const char *prompt, int *value)
{
    char line[64];

    printf("%s", prompt);
    fflush(stdout);
    if (fgets(line, sizeof line, stdin) == NULL) {
        return READ_EOF;
    }
    if (sscanf(line, "%d", value) != 1 || *value < 0 || *value > 100) {
        return READ_INVALID;
    }
    return READ_OK;
}

/* Returns false when the input stream ended. */
static bool pedal_command(char command, const char *pedal_name)
{
    char prompt[96];
    int percentage = 0;

    snprintf(prompt, sizeof prompt, "Enter the %s pedal pressure (0-100): ", pedal_name);
    switch (read_percentage(prompt, &percentage)) {
    case READ_OK:
        printf("%s pedal at %d%%.\n", pedal_name, percentage);
        send_command(command, percentage);
        return true;
    case READ_INVALID:
        printf("Invalid percentage. Try again.\n");
        return true;
    default:
        return false;
    }
}

int main(void)
{
    char line[64];

    mq = mq_open(MQ_NAME, O_WRONLY);
    if (mq == (mqd_t)-1) {
        perror("mq_open (is ./controller running?)");
        return EXIT_FAILURE;
    }

    bool active = true;

    while (active) {
        char option;

        print_menu();
        if (fgets(line, sizeof line, stdin) == NULL) {
            break;
        }
        if (sscanf(line, " %c", &option) != 1) {
            continue;
        }

        switch (option) {
        case CMD_ACCELERATOR:
            active = pedal_command(option, "accelerator");
            break;
        case CMD_BRAKE:
            active = pedal_command(option, "brake");
            break;
        case CMD_LOW_BEAM:
            send_command(option, 0);
            printf("Low beam toggled.\n");
            break;
        case CMD_HIGH_BEAM:
            send_command(option, 0);
            printf("High beam toggled.\n");
            break;
        case CMD_LEFT_SIGNAL:
            send_command(option, 0);
            printf("Left turn signal toggled.\n");
            break;
        case CMD_RIGHT_SIGNAL:
            send_command(option, 0);
            printf("Right turn signal toggled.\n");
            break;
        case CMD_EXIT:
            printf("Closing the car panel...\n");
            send_command(option, 0);
            active = false;
            break;
        default:
            printf("Invalid option. Try again.\n");
            break;
        }
    }

    mq_close(mq);
    return EXIT_SUCCESS;
}
