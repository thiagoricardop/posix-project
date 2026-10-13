/*
 * sensors.c - sensor simulator (process).
 *
 * One thread per sensor (RPM, speed, engine temperature) updates a block of
 * POSIX shared memory created by the controller. Every access is guarded by a
 * named semaphore. The main thread renders the current readings.
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#include "common.h"

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define SENSOR_COUNT 3

#define RPM_STEP_BASE   50    /* minimum RPM change per tick at full pedal */
#define RPM_STEP_SPREAD 21    /* random extra, gives 50..70 */
#define SPEED_PER_RPM   0.001 /* km/h gained per tick, per RPM */
#define TEMP_PER_RPM    0.01  /* degC added per RPM */
#define AIR_COOLING     0.05  /* degC removed per km/h */

enum sensor_id { SENSOR_RPM, SENSOR_SPEED, SENSOR_TEMPERATURE };

static SensorData *data;
static sem_t *sem;
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

static int clamp_rpm(int rpm)
{
    if (rpm > MAX_RPM) {
        return MAX_RPM;
    }
    return rpm < IDLE_RPM ? IDLE_RPM : rpm;
}

static int next_rpm(const SensorData *d, unsigned int *seed)
{
    int step = ((int)(rand_r(seed) % RPM_STEP_SPREAD) + RPM_STEP_BASE) * d->pedal / 100;

    if (d->accelerating) {
        return clamp_rpm(d->rpm + step);
    }
    if (d->braking) {
        return clamp_rpm(d->rpm - step);
    }
    return d->rpm;
}

static double next_speed(const SensorData *d, unsigned int *seed)
{
    if (d->accelerating) {
        return fmin(d->speed + d->rpm * SPEED_PER_RPM, MAX_SPEED);
    }
    if (d->braking) {
        return fmax(d->speed - (double)((int)(rand_r(seed) % 10) + 1), MIN_SPEED);
    }
    return d->speed;
}

static double engine_temperature(const SensorData *d)
{
    return BASE_TEMP + d->rpm * TEMP_PER_RPM - d->speed * AIR_COOLING;
}

static void display(const SensorData *d)
{
    printf("\033[2J\033[H");
    printf("=== Vehicle Sensors Data ===\n\n");
    printf("\033[0;31mRPM: %d rpm\033[0m\n", d->rpm);
    printf("\033[0;32mSpeed: %.2f km/h\033[0m\n", d->speed);
    printf("\033[0;33mTemperature: %.2f °C\033[0m\n", d->temperature);
    printf("\nPedal Status: %s\n",
           d->accelerating ? "Accelerating" : d->braking ? "Braking" : "Neutral");
    if (d->paused) {
        printf("State: PAUSED\n");
    }
    printf("\nPress Ctrl+C to exit\n");
    fflush(stdout);
}

static void *sensor_thread(void *arg)
{
    int id = (int)(intptr_t)arg;
    unsigned int seed = (unsigned int)time(NULL) + (unsigned int)id;

    while (running) {
        sem_lock(sem);
        if (!data->paused) {
            switch (id) {
            case SENSOR_RPM:
                data->rpm = next_rpm(data, &seed);
                break;
            case SENSOR_SPEED:
                data->speed = next_speed(data, &seed);
                break;
            case SENSOR_TEMPERATURE:
                data->temperature = engine_temperature(data);
                break;
            default:
                break;
            }
        }
        sem_unlock(sem);
        sleep(UPDATE_INTERVAL_S);
    }
    return NULL;
}

static int attach_ipc(void)
{
    int fd = shm_open(SHM_NAME, O_RDWR, 0);

    if (fd == -1) {
        perror("shm_open (is ./controller running?)");
        return -1;
    }
    void *mapping = mmap(NULL, sizeof(SensorData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    data = mapping;

    sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("sem_open (is ./controller running?)");
        munmap(data, sizeof(SensorData));
        return -1;
    }
    return 0;
}

int main(void)
{
    pthread_t threads[SENSOR_COUNT];
    int started = 0;

    install_signal_handlers();
    if (attach_ipc() == -1) {
        return EXIT_FAILURE;
    }

    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, sensor_thread, (void *)(intptr_t)i) != 0) {
            perror("pthread_create");
            running = 0;
            break;
        }
        started++;
    }

    while (running) {
        sem_lock(sem);
        SensorData snapshot = *data;
        sem_unlock(sem);

        if (snapshot.shutdown) {
            break;
        }
        display(&snapshot);
        sleep(UPDATE_INTERVAL_S);
    }

    running = 0;
    for (int i = 0; i < started; i++) {
        pthread_join(threads[i], NULL);
    }

    sem_close(sem);
    munmap(data, sizeof(SensorData));
    printf("\nSensors stopped.\n");
    return EXIT_SUCCESS;
}
