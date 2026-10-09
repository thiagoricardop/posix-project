#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <wiringPi.h>
#include <softPwm.h>
#include <pthread.h>
#include <semaphore.h>

// Definições dos pinos GPIO
#define PIN_MOTOR_DIR1      17
#define PIN_MOTOR_DIR2      18
#define PIN_MOTOR_PWM       23
#define PIN_FREIO_PWM       24
#define PIN_PEDAL_ACEL      27
#define PIN_PEDAL_FREIO     22
#define PIN_SENSOR_HALL_MOTOR 11
#define PIN_SENSOR_HALL_RODA_A 5
#define PIN_SENSOR_HALL_RODA_B 6
#define PIN_FAROL_BAIXO     19
#define PIN_FAROL_ALTO      26
#define PIN_LUZ_FREIO       25
#define PIN_LUZ_SETA_ESQ    8
#define PIN_LUZ_SETA_DIR    7
#define PIN_LUZ_TEMP_MOTOR  12
#define PIN_CMD_FAROL       16
#define PIN_CMD_FAROL_ALTO  1
#define PIN_CMD_SETA_ESQ    20
#define PIN_CMD_SETA_DIR    21
#define PIN_CC_RES          13
#define PIN_CC_CANCEL       0

// Nomes para IPC
#define SHM_NAME "/shm_car_data"
#define MQ_NAME "/mq_car_commands"
#define SEM_NAME "/sem_car_data"

// Valores máximos e mínimos
#define FACTOR_ACELERACAO 0.01
#define FATOR_RESFRIAMENTO_AR 0.05
#define MAX_SPEED 200.0
#define MIN_SPEED 0.0
#define MAX_TEMP_MOTOR 140
#define BASE_TEMP 80
#define MAX_ROTACAO 7000
#define MIN_ROTACAO 800
#define PI 3.1415

// Estrutura para dados do carro
typedef struct {
    // Dados dos sensores
    float velocidade;
    float rpm;
    float temp_motor;
    int direcao;  // 1 para frente, -1 para trás, 0 parado
    
    // Estado dos atuadores
    int farol_alto;
    int farol_baixo;
    int seta_esquerda;
    int seta_direita;
    int luz_freio;
    int pedal_acelerador;
    int pedal_freio;
    
    // Cruise Control
    int cruise_control_ativo;
    float velocidade_cruzeiro;
    
} CarData;

// Tipos de comandos
typedef enum {
    CMD_ACELERAR,
    CMD_FREAR,
    CMD_FAROL_ALTO,
    CMD_FAROL_BAIXO,
    CMD_SETA_ESQ,
    CMD_SETA_DIR,
    CMD_CRUISE_CONTROL,
    CMD_CC_CANCEL,
    CMD_PARAR
} CommandType;

// Estrutura para comandos
typedef struct {
    CommandType tipo;
    int valor;  // Valor associado
} Command;

extern volatile int running;

#endif