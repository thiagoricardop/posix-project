/*
 * common.h - definitions shared by the three TP2 processes
 * (controller, sensors and car_panel).
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#ifndef TP2_COMMON_H
#define TP2_COMMON_H

#include <errno.h>
#include <semaphore.h>
#include <time.h>
#include <wiringPi.h>

/* ---- Raspberry Pi 4 GPIO map (BCM numbering) ---- */

/* Outputs */
#define PIN_MOTOR_DIR1        17
#define PIN_MOTOR_DIR2        18
#define PIN_MOTOR_PWM         23
#define PIN_BRAKE_PWM         24
#define PIN_LAMP_LOW_BEAM     19
#define PIN_LAMP_HIGH_BEAM    26
#define PIN_LAMP_BRAKE        25
#define PIN_LAMP_LEFT_SIGNAL   8
#define PIN_LAMP_RIGHT_SIGNAL  7
#define PIN_LAMP_ENGINE_TEMP  12

/* Inputs */
#define PIN_ACCEL_PEDAL       27
#define PIN_BRAKE_PEDAL       22
#define PIN_HALL_MOTOR        11
#define PIN_HALL_WHEEL_A       5
#define PIN_HALL_WHEEL_B       6
#define PIN_CMD_LOW_BEAM      16
#define PIN_CMD_HIGH_BEAM      1
#define PIN_CMD_LEFT_SIGNAL   20
#define PIN_CMD_RIGHT_SIGNAL  21
#define PIN_CMD_CC_RESUME     13
#define PIN_CMD_CC_CANCEL      0

/* POSIX IPC objects. The controller creates and unlinks them (owner-only access). */
#define SHM_NAME "/tp2_car_shm"
#define SEM_NAME "/tp2_car_sem"
#define MQ_NAME  "/tp2_car_commands"
#define IPC_MODE 0600
#define MQ_MAX_MESSAGES 10

/* Vehicle limits and calibration. */
#define MAX_SPEED         200.0f /* km/h */
#define MAX_RPM           7000.0f
#define MIN_RPM           800.0f
#define MAX_ENGINE_TEMP   140.0f /* degrees Celsius */
#define BASE_TEMP         80.0f  /* degrees Celsius */
#define TEMP_WARNING      120.0f /* engine temperature lamp threshold */
#define TEMP_PER_RPM      0.01f  /* degC added per RPM above idle */
#define AIR_COOLING       0.05f  /* degC removed per km/h */
#define WHEEL_DIAMETER_M  0.63f
#define PULSE_TO_KMH      (WHEEL_DIAMETER_M * 3.14159265f * 3.6f)
#define SENSOR_WINDOW_MS  1000

/* Actuation. */
#define PWM_RANGE            10   /* 10 steps x 100 us = 1 kHz soft PWM */
#define ACCEL_DUTY_PERCENT   50
#define BRAKE_DUTY_PERCENT   100
#define CC_DEADBAND_KMH      1.0f /* on/off cruise control hysteresis */
#define SIGNAL_HALF_PERIOD_MS 500 /* turn signals blink once per second */
#define STANDSTILL_KMH       1.0f

/* Shared memory block. Sensors write the measurements, the controller the actuator state. */
typedef struct {
    /* Measurements (written by sensors) */
    float speed_kmh;
    float rpm;
    float engine_temp_c;
    int direction; /* 1 forward, -1 reverse, 0 stopped */

    /* Actuator state (written by controller) */
    int low_beam;
    int high_beam;
    int left_signal;
    int right_signal;
    int brake_light;
    int motor_duty;  /* percent */
    int brake_duty;  /* percent */
    int cruise_active;
    float cruise_speed_kmh;

    volatile int shutdown; /* set by the controller when it exits */
} CarData;

/* Commands sent by the panel through the POSIX message queue. */
typedef enum {
    CMD_ACCELERATE,
    CMD_BRAKE,
    CMD_LOW_BEAM,
    CMD_HIGH_BEAM,
    CMD_LEFT_SIGNAL,
    CMD_RIGHT_SIGNAL,
    CMD_CRUISE_RESUME,
    CMD_CRUISE_CANCEL,
    CMD_CRUISE_SET_UP,
    CMD_CRUISE_SET_DOWN,
    CMD_COUNT
} CommandType;

typedef struct {
    CommandType type;
    int value; /* pedal duty in percent, unused by the buttons */
} Command;

static inline void sem_lock(sem_t *sem)
{
    while (sem_wait(sem) == -1 && errno == EINTR) {
    }
}

static inline void sem_unlock(sem_t *sem)
{
    sem_post(sem);
}

static inline long long monotonic_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}

static inline void sleep_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static inline int percent_to_pwm(int percent)
{
    return percent * PWM_RANGE / 100;
}

#endif /* TP2_COMMON_H */
