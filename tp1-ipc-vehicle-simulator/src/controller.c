/*
 * controller.c - central controller (process).
 *
 * Owns the POSIX IPC objects (shared memory, named semaphore, message queue),
 * applies the user commands received from the panel, enforces the vehicle
 * safety limits and prints a consolidated report when the test ends.
 *
 *   SIGUSR1  pause / resume the test
 *   SIGUSR2  end the test (also SIGINT, SIGTERM or the panel's "0" key)
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#include "common.h"

#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    bool low_beam;
    bool high_beam;
    bool left_signal;
    bool right_signal;
} Lights;

/* Which limits are currently exceeded, used to count events instead of samples. */
typedef struct {
    bool overspeed;
    bool overheat;
    bool overrev;
    bool emergency;
} LimitFlags;

typedef struct {
    time_t started;
    double max_speed;
    int max_rpm;
    double max_temperature;
    unsigned overspeed_events;
    unsigned overheat_events;
    unsigned overrev_events;
    unsigned emergency_stops;
    unsigned accelerator_actions;
    unsigned brake_actions;
    unsigned light_actions;
    unsigned signal_actions;
} Report;

static SensorData *data;
static sem_t *sem;
static mqd_t mq = (mqd_t)-1;
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t paused;

static void die(const char *what) __attribute__((noreturn));

static void die(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

static void on_signal(int sig)
{
    if (sig == SIGUSR1) {
        paused = !paused;
    } else {
        running = 0;
    }
}

static void install_signal_handlers(void)
{
    const int signals[] = { SIGUSR1, SIGUSR2, SIGINT, SIGTERM };
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; i++) {
        sigaction(signals[i], &sa, NULL);
    }
}

/* Creates the IPC objects, replacing leftovers of a previous run. */
static void create_ipc(void)
{
    struct mq_attr attr;

    shm_unlink(SHM_NAME);
    mq_unlink(MQ_NAME);
    sem_unlink(SEM_NAME);

    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, IPC_MODE);
    if (fd == -1) {
        die("shm_open");
    }
    if (ftruncate(fd, (off_t)sizeof(SensorData)) == -1) {
        die("ftruncate");
    }
    void *mapping = mmap(NULL, sizeof(SensorData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) {
        die("mmap");
    }
    data = mapping;
    memset(data, 0, sizeof *data);
    data->rpm = IDLE_RPM;
    data->temperature = BASE_TEMP;

    memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg = MQ_MAX_MESSAGES;
    attr.mq_msgsize = sizeof(Message);
    mq = mq_open(MQ_NAME, O_CREAT | O_EXCL | O_RDONLY, IPC_MODE, &attr);
    if (mq == (mqd_t)-1) {
        die("mq_open");
    }

    /* Created last: the other processes can wait for it to know the system is ready. */
    sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, IPC_MODE, 1);
    if (sem == SEM_FAILED) {
        die("sem_open");
    }
}

static void destroy_ipc(void)
{
    sem_lock(sem);
    data->shutdown = true;
    sem_unlock(sem);

    sem_close(sem);
    sem_unlink(SEM_NAME);
    mq_close(mq);
    mq_unlink(MQ_NAME);
    munmap(data, sizeof *data);
    shm_unlink(SHM_NAME);
}

static void deadline_in(struct timespec *ts, int seconds)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += seconds;
}

static void track(bool condition, bool *active, unsigned *counter)
{
    if (condition && !*active) {
        (*counter)++;
    }
    *active = condition;
}

/* Must be called with the semaphore held. */
static void apply_safety_limits(SensorData *d, LimitFlags *flags, Report *report)
{
    bool overspeed = d->speed >= MAX_SPEED;
    bool overheat = d->temperature > MAX_ENGINE_TEMP;
    bool overrev = d->rpm >= MAX_RPM;

    track(overspeed, &flags->overspeed, &report->overspeed_events);
    track(overheat, &flags->overheat, &report->overheat_events);
    track(overrev, &flags->overrev, &report->overrev_events);
    track(overspeed || overheat, &flags->emergency, &report->emergency_stops);

    if (overspeed || overheat) {
        /* Controlled stop: brake hard until the vehicle is at standstill. */
        d->accelerating = false;
        d->braking = true;
        d->pedal = 100;
    } else if (overrev) {
        d->accelerating = false;
    } else if (d->braking && d->speed <= MIN_SPEED) {
        d->braking = false;
        d->pedal = 0;
    }

    if (d->speed > report->max_speed) {
        report->max_speed = d->speed;
    }
    if (d->rpm > report->max_rpm) {
        report->max_rpm = d->rpm;
    }
    if (d->temperature > report->max_temperature) {
        report->max_temperature = d->temperature;
    }
}

/* Must be called with the semaphore held. */
static void handle_command(const Message *msg, SensorData *d, Lights *lights, Report *report)
{
    int pedal = msg->pedal < 0 ? 0 : (msg->pedal > 100 ? 100 : msg->pedal);

    switch (msg->command) {
    case CMD_ACCELERATOR:
        if (pedal > 0) {
            d->accelerating = true;
            d->braking = false;
            d->pedal = pedal;
            report->accelerator_actions++;
        } else if (d->accelerating) {
            d->accelerating = false;
            d->pedal = 0;
            report->accelerator_actions++;
        }
        break;
    case CMD_BRAKE:
        if (pedal > 0) {
            d->braking = true;
            d->accelerating = false;
            d->pedal = pedal;
            report->brake_actions++;
        } else if (d->braking) {
            d->braking = false;
            d->pedal = 0;
            report->brake_actions++;
        }
        break;
    case CMD_LOW_BEAM:
        lights->low_beam = !lights->low_beam;
        report->light_actions++;
        break;
    case CMD_HIGH_BEAM:
        lights->high_beam = !lights->high_beam;
        report->light_actions++;
        break;
    case CMD_LEFT_SIGNAL:
        lights->left_signal = !lights->left_signal;
        if (lights->left_signal) {
            lights->right_signal = false;
        }
        report->signal_actions++;
        break;
    case CMD_RIGHT_SIGNAL:
        lights->right_signal = !lights->right_signal;
        if (lights->right_signal) {
            lights->left_signal = false;
        }
        report->signal_actions++;
        break;
    case CMD_EXIT:
        running = 0;
        break;
    default:
        break; /* unknown commands are ignored */
    }
}

static const char *on_off(bool value)
{
    return value ? "ON" : "OFF";
}

static void display_status(const SensorData *d, const Lights *lights, bool is_paused)
{
    printf("\033[2J\033[H");
    printf("=== System State (PID %d) ===\n", (int)getpid());
    printf("Speed: %.2f km/h\n", d->speed);
    printf("RPM: %d\n", d->rpm);
    printf("Temperature: %.2f °C\n", d->temperature);
    printf("Accelerator: %s", on_off(d->accelerating));
    if (d->accelerating) {
        printf(" (%d%%)", d->pedal);
    }
    printf("\nBrake: %s", on_off(d->braking));
    if (d->braking) {
        printf(" (%d%%)", d->pedal);
    }
    printf("\nLow beam: %s\n", on_off(lights->low_beam));
    printf("High beam: %s\n", on_off(lights->high_beam));
    printf("Right signal: %s\n", on_off(lights->right_signal));
    printf("Left signal: %s\n", on_off(lights->left_signal));
    if (is_paused) {
        printf("State: PAUSED (send SIGUSR1 to resume)\n");
    }
    printf("============================\n");
    printf("kill -USR1 %d : pause/resume | kill -USR2 %d : end test\n", (int)getpid(), (int)getpid());
    fflush(stdout);
}

static void print_report(const Report *r)
{
    printf("\n=== Test report ===\n");
    printf("Duration:             %ld s\n", (long)(time(NULL) - r->started));
    printf("Max speed:            %.2f km/h\n", r->max_speed);
    printf("Max RPM:              %d\n", r->max_rpm);
    printf("Max temperature:      %.2f °C\n", r->max_temperature);
    printf("Overspeed events:     %u\n", r->overspeed_events);
    printf("Overheat events:      %u\n", r->overheat_events);
    printf("Over-rev events:      %u\n", r->overrev_events);
    printf("Emergency stops:      %u\n", r->emergency_stops);
    printf("Accelerator actions:  %u\n", r->accelerator_actions);
    printf("Brake actions:        %u\n", r->brake_actions);
    printf("Light actions:        %u\n", r->light_actions);
    printf("Signal actions:       %u\n", r->signal_actions);
    printf("===================\n");
}

int main(void)
{
    Lights lights;
    LimitFlags flags;
    Report report;
    SensorData snapshot;

    memset(&lights, 0, sizeof lights);
    memset(&flags, 0, sizeof flags);
    memset(&report, 0, sizeof report);
    report.started = time(NULL);

    install_signal_handlers();
    create_ipc();

    while (running) {
        Message msg;
        struct timespec deadline;

        memset(&msg, 0, sizeof msg);
        deadline_in(&deadline, UPDATE_INTERVAL_S);
        ssize_t received = mq_timedreceive(mq, (char *)&msg, sizeof msg, NULL, &deadline);
        if (received == -1 && errno != ETIMEDOUT && errno != EINTR) {
            perror("mq_timedreceive");
            break;
        }

        sem_lock(sem);
        if (received == (ssize_t)sizeof msg) {
            handle_command(&msg, data, &lights, &report);
        }
        data->paused = paused;
        if (!paused) {
            apply_safety_limits(data, &flags, &report);
        }
        snapshot = *data;
        sem_unlock(sem);

        display_status(&snapshot, &lights, paused);
    }

    print_report(&report);
    destroy_ipc();
    printf("Controller stopped.\n");
    return EXIT_SUCCESS;
}
