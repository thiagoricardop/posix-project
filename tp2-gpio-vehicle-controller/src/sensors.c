/*
 * sensors.c - Hall-sensor acquisition (process).
 *
 * Samples the engine and wheel Hall sensors every millisecond, converts the
 * pulse counts of each 1 s window into RPM, speed and engine temperature and
 * publishes them in the shared memory owned by the controller.
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#include "common.h"

#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static CarData *car;
static sem_t *sem;
static volatile sig_atomic_t running = 1;

static unsigned motor_pulses;
static unsigned wheel_pulses;
static int last_motor;
static int last_wheel_a;
static int direction;

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

static int init_gpio(void)
{
    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "Failed to initialise GPIO\n");
        return -1;
    }

    const int inputs[] = { PIN_HALL_MOTOR, PIN_HALL_WHEEL_A, PIN_HALL_WHEEL_B };
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        pinMode(inputs[i], INPUT);
        pullUpDnControl(inputs[i], PUD_DOWN);
    }
    last_motor = digitalRead(PIN_HALL_MOTOR);
    last_wheel_a = digitalRead(PIN_HALL_WHEEL_A);
    return 0;
}

static int attach_ipc(void)
{
    int fd = shm_open(SHM_NAME, O_RDWR, 0);

    if (fd == -1) {
        perror("shm_open (is ./controller running?)");
        return -1;
    }
    void *mapping = mmap(NULL, sizeof(CarData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    car = mapping;

    sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("sem_open (is ./controller running?)");
        munmap(car, sizeof(CarData));
        return -1;
    }
    return 0;
}

/*
 * Edge detection by polling: the fastest signal is 7000 RPM (~117 Hz), far
 * below the 1 kHz sampling rate. The engine sensor emits one pulse per turn
 * (rising edges are counted). The wheel channel A is counted on both edges,
 * and channel B tells the direction: when A rises while B is still low, A
 * leads B and the wheel turns forward.
 */
static void poll_hall_sensors(void)
{
    int motor = digitalRead(PIN_HALL_MOTOR);

    if (motor == HIGH && last_motor == LOW) {
        motor_pulses++;
    }
    last_motor = motor;

    int wheel_a = digitalRead(PIN_HALL_WHEEL_A);
    if (wheel_a != last_wheel_a) {
        wheel_pulses++;
        if (wheel_a == HIGH) {
            direction = digitalRead(PIN_HALL_WHEEL_B) == LOW ? 1 : -1;
        }
    }
    last_wheel_a = wheel_a;
}

/* Converts the pulses counted in the last window and publishes the result. */
static void publish_window(long long elapsed_ms)
{
    float scale = 1000.0f / (float)elapsed_ms;
    float rpm = (float)motor_pulses * 60.0f * scale;
    float speed = (float)wheel_pulses * PULSE_TO_KMH * scale;
    int dir = wheel_pulses > 0 ? direction : 0;

    float heating = (rpm - MIN_RPM) * TEMP_PER_RPM;
    float cooling = speed * AIR_COOLING;
    float temperature = fminf(fmaxf(BASE_TEMP + heating - cooling, BASE_TEMP), MAX_ENGINE_TEMP);

    motor_pulses = 0;
    wheel_pulses = 0;

    sem_lock(sem);
    car->rpm = rpm;
    car->speed_kmh = speed;
    car->engine_temp_c = temperature;
    car->direction = dir;
    if (car->shutdown) {
        running = 0;
    }
    sem_unlock(sem);

    printf("\rRPM: %.0f | Speed: %.1f km/h | Temp: %.1f°C | Direction: %s\033[K", rpm, speed,
           temperature, dir > 0 ? "forward" : dir < 0 ? "reverse" : "stopped");
    fflush(stdout);
}

static void reset_measurements(void)
{
    sem_lock(sem);
    car->rpm = 0.0f;
    car->speed_kmh = 0.0f;
    car->engine_temp_c = BASE_TEMP;
    car->direction = 0;
    sem_unlock(sem);
}

int main(void)
{
    install_signal_handlers();
    if (attach_ipc() == -1 || init_gpio() == -1) {
        return EXIT_FAILURE;
    }

    printf("Sensors started. Monitoring...\n");

    long long window_start = monotonic_ms();
    while (running) {
        poll_hall_sensors();

        long long now = monotonic_ms();
        if (now - window_start >= SENSOR_WINDOW_MS) {
            publish_window(now - window_start);
            window_start = now;
        }
        sleep_ms(1);
    }

    printf("\nStopping sensors...\n");
    reset_measurements();
    sem_close(sem);
    munmap(car, sizeof(CarData));
    return EXIT_SUCCESS;
}
