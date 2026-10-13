/*
 * common.h - definitions shared by the three TP1 processes
 * (controller, sensors and car_panel).
 *
 * UFPE - Automotive Software Development residency - POSIX standard course.
 * Author: Thiago Ricardo
 */
#ifndef TP1_COMMON_H
#define TP1_COMMON_H

#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>

/* POSIX IPC objects. The controller creates and unlinks them (owner-only access). */
#define SHM_NAME        "/tp1_sensors_shm"
#define SEM_NAME        "/tp1_sensors_sem"
#define MQ_NAME         "/tp1_commands_mq"
#define IPC_MODE        0600
#define MQ_MAX_MESSAGES 10

/* Simulation limits. */
#define UPDATE_INTERVAL_S 1
#define MAX_SPEED         200.0 /* km/h */
#define MIN_SPEED         0.0   /* km/h */
#define MAX_ENGINE_TEMP   140.0 /* degrees Celsius */
#define MAX_RPM           7000
#define IDLE_RPM          800
#define BASE_TEMP         80.0 /* degrees Celsius */

/* Panel keys, carried in Message.command. */
#define CMD_EXIT         '0'
#define CMD_ACCELERATOR  '1'
#define CMD_BRAKE        '2'
#define CMD_LOW_BEAM     '3'
#define CMD_HIGH_BEAM    '4'
#define CMD_LEFT_SIGNAL  '5'
#define CMD_RIGHT_SIGNAL '6'

/* Vehicle state written by the sensors and the controller (lives in shared memory). */
typedef struct {
    double speed;       /* km/h */
    int rpm;            /* engine revolutions per minute */
    double temperature; /* degrees Celsius */
    bool accelerating;
    bool braking;
    int pedal;          /* pedal pressure, 0-100 % */
    bool paused;        /* set by the controller on SIGUSR1 */
    bool shutdown;      /* set by the controller when the test ends */
} SensorData;

/* Command sent by the panel through the POSIX message queue. */
typedef struct {
    char command;
    int pedal; /* 0-100 %, only meaningful for the pedals */
} Message;

static inline void sem_lock(sem_t *sem)
{
    while (sem_wait(sem) == -1 && errno == EINTR) {
    }
}

static inline void sem_unlock(sem_t *sem)
{
    sem_post(sem);
}

#endif /* TP1_COMMON_H */
