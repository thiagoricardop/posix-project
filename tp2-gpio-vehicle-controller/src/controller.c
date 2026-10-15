/*
 * controller.c - central controller (process).
 *
 * Owns the POSIX IPC objects (shared memory, named semaphore, message queue).
 * Receives the panel commands, enforces the safety limits, runs the on/off
 * cruise control and drives the actuators through GPIO / 1 kHz soft PWM.
 *
 * Event-driven: the loop sleeps in mq_timedreceive() and wakes up on a command
 * or after LOOP_PERIOD_MS, so it never spins at 100 % CPU.
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#include "common.h"

#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <softPwm.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOOP_PERIOD_MS    20
#define DISPLAY_PERIOD_MS 1000

typedef struct {
    int accel_pedal;     /* manual duty, percent (0 = released) */
    int brake_pedal;     /* manual duty, percent */
    int cruise_throttle; /* duty requested by the cruise controller */
    bool cruise_active;
    float cruise_speed;
    bool low_beam;
    bool high_beam;
    bool left_signal;
    bool right_signal;
    bool emergency; /* safety limit exceeded: controlled stop */
    float speed;
    float rpm;
    float temperature;
    int direction;
} Vehicle;

typedef struct {
    int motor_duty;
    int brake_duty;
} Outputs;

/* Digital outputs; the two PWM pins are handled separately. */
static const int OUTPUT_PINS[] = {
    PIN_MOTOR_DIR1,        PIN_MOTOR_DIR2,       PIN_LAMP_LOW_BEAM,    PIN_LAMP_HIGH_BEAM,
    PIN_LAMP_BRAKE,        PIN_LAMP_LEFT_SIGNAL, PIN_LAMP_RIGHT_SIGNAL, PIN_LAMP_ENGINE_TEMP,
};
#define OUTPUT_PIN_COUNT (sizeof OUTPUT_PINS / sizeof OUTPUT_PINS[0])

static CarData *car;
static sem_t *sem;
static mqd_t mq = (mqd_t)-1;
static volatile sig_atomic_t running = 1;

static void die(const char *what) __attribute__((noreturn));

static void die(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

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

static void init_gpio(void)
{
    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "Failed to initialise GPIO (are you root?)\n");
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < OUTPUT_PIN_COUNT; i++) {
        pinMode(OUTPUT_PINS[i], OUTPUT);
        digitalWrite(OUTPUT_PINS[i], LOW);
    }
    if (softPwmCreate(PIN_MOTOR_PWM, 0, PWM_RANGE) != 0 ||
        softPwmCreate(PIN_BRAKE_PWM, 0, PWM_RANGE) != 0) {
        fprintf(stderr, "Failed to create the PWM outputs\n");
        exit(EXIT_FAILURE);
    }
}

/* Turns every actuator off (requirement: leave the hardware safe on exit). */
static void release_gpio(void)
{
    softPwmWrite(PIN_MOTOR_PWM, 0);
    softPwmWrite(PIN_BRAKE_PWM, 0);
    softPwmStop(PIN_MOTOR_PWM);
    softPwmStop(PIN_BRAKE_PWM);
    for (size_t i = 0; i < OUTPUT_PIN_COUNT; i++) {
        digitalWrite(OUTPUT_PINS[i], LOW);
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
    if (ftruncate(fd, (off_t)sizeof(CarData)) == -1) {
        die("ftruncate");
    }
    void *mapping = mmap(NULL, sizeof(CarData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) {
        die("mmap");
    }
    car = mapping;
    memset(car, 0, sizeof *car);
    car->engine_temp_c = BASE_TEMP;

    memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg = MQ_MAX_MESSAGES;
    attr.mq_msgsize = sizeof(Command);
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
    car->shutdown = 1;
    sem_unlock(sem);

    sem_close(sem);
    sem_unlink(SEM_NAME);
    mq_close(mq);
    mq_unlink(MQ_NAME);
    munmap(car, sizeof *car);
    shm_unlink(SHM_NAME);
}

static void read_measurements(Vehicle *v)
{
    sem_lock(sem);
    v->speed = car->speed_kmh;
    v->rpm = car->rpm;
    v->temperature = car->engine_temp_c;
    v->direction = car->direction;
    sem_unlock(sem);
}

static int clamp_percent(int value)
{
    return value < 0 ? 0 : (value > 100 ? 100 : value);
}

static void cancel_cruise(Vehicle *v)
{
    v->cruise_active = false;
    v->cruise_throttle = 0;
}

static void process_command(Vehicle *v, const Command *cmd)
{
    int value = clamp_percent(cmd->value);

    switch (cmd->type) {
    case CMD_ACCELERATE:
        v->accel_pedal = value;
        break;
    case CMD_BRAKE:
        v->brake_pedal = value;
        if (value > 0) {
            cancel_cruise(v);
        }
        break;
    case CMD_LOW_BEAM:
        v->low_beam = !v->low_beam;
        if (v->low_beam) {
            v->high_beam = false;
        }
        break;
    case CMD_HIGH_BEAM:
        v->high_beam = !v->high_beam;
        if (v->high_beam) {
            v->low_beam = false;
        }
        break;
    case CMD_LEFT_SIGNAL:
        v->left_signal = !v->left_signal;
        if (v->left_signal) {
            v->right_signal = false;
        }
        break;
    case CMD_RIGHT_SIGNAL:
        v->right_signal = !v->right_signal;
        if (v->right_signal) {
            v->left_signal = false;
        }
        break;
    case CMD_CRUISE_RESUME:
        if (v->speed > 0.0f) {
            v->cruise_active = true;
            v->cruise_speed = v->speed;
        }
        break;
    case CMD_CRUISE_CANCEL:
        cancel_cruise(v);
        break;
    case CMD_CRUISE_SET_UP:
        if (v->cruise_active && v->cruise_speed + 1.0f <= MAX_SPEED) {
            v->cruise_speed += 1.0f;
        }
        break;
    case CMD_CRUISE_SET_DOWN:
        if (v->cruise_active && v->cruise_speed - 1.0f >= 0.0f) {
            v->cruise_speed -= 1.0f;
        }
        break;
    default:
        break;
    }
}

/* Speed or temperature limit exceeded: controlled stop until the car is at standstill. */
static void check_safety_limits(Vehicle *v)
{
    if (v->speed >= MAX_SPEED || v->temperature >= MAX_ENGINE_TEMP) {
        v->emergency = true;
        cancel_cruise(v);
    } else if (v->emergency && v->speed < STANDSTILL_KMH) {
        v->emergency = false;
    }
}

/* Simple on/off cruise control with hysteresis around the set point. */
static void update_cruise_control(Vehicle *v)
{
    if (!v->cruise_active) {
        v->cruise_throttle = 0;
        return;
    }

    float error = v->cruise_speed - v->speed;
    if (error > CC_DEADBAND_KMH) {
        v->cruise_throttle = ACCEL_DUTY_PERCENT;
    } else if (error < -CC_DEADBAND_KMH) {
        v->cruise_throttle = 0;
    }
}

/* Brake has priority over the throttle; the rev limiter cuts the throttle. */
static Outputs compute_outputs(const Vehicle *v)
{
    Outputs out;

    out.brake_duty = v->emergency ? BRAKE_DUTY_PERCENT : v->brake_pedal;
    out.motor_duty = v->accel_pedal > 0 ? v->accel_pedal : v->cruise_throttle;
    if (out.brake_duty > 0 || v->emergency || v->rpm >= MAX_RPM) {
        out.motor_duty = 0;
    }
    return out;
}

static void update_actuators(const Vehicle *v, const Outputs *out)
{
    /* Driver truth table: free (0,0), forward (1,0), brake (1,1). */
    digitalWrite(PIN_MOTOR_DIR1, (out->motor_duty > 0 || out->brake_duty > 0) ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_DIR2, out->brake_duty > 0 ? HIGH : LOW);
    softPwmWrite(PIN_MOTOR_PWM, percent_to_pwm(out->motor_duty));
    softPwmWrite(PIN_BRAKE_PWM, percent_to_pwm(out->brake_duty));

    bool blink_on = (monotonic_ms() / SIGNAL_HALF_PERIOD_MS) % 2 == 0;
    digitalWrite(PIN_LAMP_BRAKE, out->brake_duty > 0 ? HIGH : LOW);
    digitalWrite(PIN_LAMP_LOW_BEAM, v->low_beam ? HIGH : LOW);
    digitalWrite(PIN_LAMP_HIGH_BEAM, v->high_beam ? HIGH : LOW);
    digitalWrite(PIN_LAMP_LEFT_SIGNAL, (v->left_signal && blink_on) ? HIGH : LOW);
    digitalWrite(PIN_LAMP_RIGHT_SIGNAL, (v->right_signal && blink_on) ? HIGH : LOW);
    digitalWrite(PIN_LAMP_ENGINE_TEMP, v->temperature >= TEMP_WARNING ? HIGH : LOW);
}

static void publish_state(const Vehicle *v, const Outputs *out)
{
    sem_lock(sem);
    car->low_beam = v->low_beam;
    car->high_beam = v->high_beam;
    car->left_signal = v->left_signal;
    car->right_signal = v->right_signal;
    car->brake_light = out->brake_duty > 0;
    car->motor_duty = out->motor_duty;
    car->brake_duty = out->brake_duty;
    car->cruise_active = v->cruise_active;
    car->cruise_speed_kmh = v->cruise_speed;
    sem_unlock(sem);
}

static void display_status(const Vehicle *v, const Outputs *out)
{
    printf("\rSpeed: %.1f km/h | RPM: %.0f | Temp: %.1f°C | Motor: %d%% | Brake: %d%%",
           v->speed, v->rpm, v->temperature, out->motor_duty, out->brake_duty);
    if (v->cruise_active) {
        printf(" | CC: %.0f km/h", v->cruise_speed);
    }
    if (v->emergency) {
        printf(" | SAFETY STOP");
    }
    printf("\033[K");
    fflush(stdout);
}

static void deadline_in_ms(struct timespec *ts, long ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_nsec += ms * 1000000L;
    ts->tv_sec += ts->tv_nsec / 1000000000L;
    ts->tv_nsec %= 1000000000L;
}

int main(void)
{
    Vehicle vehicle;
    long long last_display = 0;

    memset(&vehicle, 0, sizeof vehicle);
    install_signal_handlers();
    init_gpio();
    create_ipc();

    printf("Controller started...\n");

    while (running) {
        Command cmd;
        struct timespec deadline;

        memset(&cmd, 0, sizeof cmd);
        deadline_in_ms(&deadline, LOOP_PERIOD_MS);
        ssize_t received = mq_timedreceive(mq, (char *)&cmd, sizeof cmd, NULL, &deadline);
        if (received == -1 && errno != ETIMEDOUT && errno != EINTR) {
            perror("mq_timedreceive");
            break;
        }

        read_measurements(&vehicle);
        if (received == (ssize_t)sizeof cmd && (unsigned)cmd.type < (unsigned)CMD_COUNT) {
            process_command(&vehicle, &cmd);
        }
        check_safety_limits(&vehicle);
        update_cruise_control(&vehicle);

        Outputs outputs = compute_outputs(&vehicle);
        update_actuators(&vehicle, &outputs);
        publish_state(&vehicle, &outputs);

        long long now = monotonic_ms();
        if (now - last_display >= DISPLAY_PERIOD_MS) {
            display_status(&vehicle, &outputs);
            last_display = now;
        }
    }

    printf("\nShutting down controller...\n");
    release_gpio();
    destroy_ipc();
    return EXIT_SUCCESS;
}
