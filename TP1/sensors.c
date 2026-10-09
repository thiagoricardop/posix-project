//////////////////////////////////////////////////////////////////////////////////
// Universidade Federal de Perambuco (UFPE)
// Curso: Residencia Tecnologica em Engenharia de Software
// Disciplina : Padrão POSIX
// Professor : Renato Coral Sampaio
// Aluno: Thiago Ricardo Oliveira Pinto
//////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>

// Definição dos sensores e constantes
#define SENSOR_COUNT 5
#define SHARED_MEM_NAME "/shared_sensors"
#define SEMAPHORE_NAME "/sensor_semaphore"
#define UPDATE_INTERVAL 1 // Intervalo de atualização em segundos

// Definições de cores para Display
#define CLEAR_SCREEN "\033[2J\033[H"
#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define YELLOW "\033[0;33m"
#define RESET "\033[0m"

// Constantes de cálculo
#define FACTOR_ACELERACAO 0.01
#define FATOR_RESFRIAMENTO_AR 0.05
#define MAX_TEMP_MOTOR 140
#define MAX_SPEED 200.0
#define MAX_ROTACAO 8000
#define BASE_TEMP 80
#define RPM_BASE 800 // RPM inicial
#define VELOCIDADE_BASE 0 // velocidade inicial

typedef struct {
    double velocidade;
    int rpm;
    double temperatura;
    bool acelera;
    bool freia;
    int pedal;
} SensorData;

// Variáveis globais
SensorData *shared_data;
sem_t *sem;

// Funções para cálculos
double calculate_engine_temp(double velocidade, double rpm) {
    double temp_rise = rpm * FACTOR_ACELERACAO;
    double cooling_effect = velocidade * FATOR_RESFRIAMENTO_AR;
    double temp = BASE_TEMP + temp_rise - cooling_effect;
    return temp;
}

double calculate_speed(int rpm, double velocidade) {
    if(shared_data->acelera == true){
        double fator_variacao = rpm * 0.001;
        return fmin(velocidade + fator_variacao, MAX_SPEED);
    }
    else if(shared_data->acelera == false && shared_data->freia == false){
        return velocidade;
    }
    else if(shared_data->acelera == false && shared_data->freia == true){
        double fator_variacao = (rand() % 10) + 1;
        return fmax(velocidade - fator_variacao, 0);
    }
}

int calculate_rpm(int rpm_atual, int pedal) {
    if(shared_data->acelera == true){
        float pedal_percent = pedal / 100.0;
        int fator_aceleracao = ((rand() % 21) + 50) * pedal_percent;
        return fmin((rpm_atual + fator_aceleracao), MAX_ROTACAO);
    }
    else if(shared_data->acelera == false && shared_data->freia == true){
        float pedal_percent = pedal / 100.0;
        int fator_desaceleracao = ((rand() % 21) + 50) * pedal_percent;
        return fmax((rpm_atual - fator_desaceleracao), 0);
    }
    else if(shared_data->acelera == false && shared_data->freia == false){
        return rpm_atual;
    }
}

void display_sensors_data() {
    printf(CLEAR_SCREEN);
    printf("\n=== Vehicle Sensors Data ===\n\n");
    printf(RED "RPM: %d rpm\n" RESET, shared_data->rpm);
    printf(GREEN "Speed: %.2f km/h\n" RESET, shared_data->velocidade);
    printf(YELLOW "Temperature: %.2f °C\n" RESET, shared_data->temperatura);
    printf("\nPedal Status: %s\n", 
           shared_data->acelera ? "Accelerating" : 
           shared_data->freia ? "Braking" : "Neutral");
    printf("\nPress Ctrl+C to exit\n");
    fflush(stdout);
}

// Função para simular o comportamento de cada sensor
void *simulate_sensor(void *arg) {
    int sensor_id = *(int *)arg;
    free(arg);

    while (1) {
        sem_wait(sem); // Acesso exclusivo à memória compartilhada

        switch (sensor_id) {
            case 0: { // Rotação
                shared_data->rpm = calculate_rpm(shared_data->rpm, shared_data->pedal); // Calcula a rotação
                
                break;
            }
            case 1: { // Velocidade
                shared_data->velocidade = calculate_speed(shared_data->rpm, shared_data->velocidade); // Calcula a velocidade
                
                break;
            }
            
            case 2: { // Temperatura
                shared_data->temperatura = calculate_engine_temp(shared_data->velocidade, shared_data->rpm); // Calcula a temperatura
                
                break;
            }
        }
        display_sensors_data();
        sem_post(sem); // Libera acesso
        sleep(UPDATE_INTERVAL);
    }
    return NULL;
}

int main() {
    pthread_t threads[SENSOR_COUNT];

    // Criação da memória compartilhada
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Erro ao criar memória compartilhada");
        exit(EXIT_FAILURE);
    }

    // Configuração do tamanho da memória compartilhada
    if (ftruncate(shm_fd, sizeof(SensorData)) == -1) {
        perror("Erro ao configurar tamanho da memória compartilhada");
        exit(EXIT_FAILURE);
    }

    // Mapeamento da memória compartilhada
    shared_data = mmap(NULL, sizeof(SensorData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        perror("Erro ao mapear memória compartilhada");
        exit(EXIT_FAILURE);
    }

    // Inicialização dos dados na memória compartilhada
    shared_data->velocidade = VELOCIDADE_BASE;
    shared_data->rpm = RPM_BASE;
    shared_data->temperatura = BASE_TEMP;
    shared_data->acelera = false;
    shared_data->freia = false;
    shared_data->pedal = 0;

    // Criação do semáforo para sincronização
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("Erro ao criar semáforo");
        exit(EXIT_FAILURE);
    }

    // Criação das threads para cada sensor
    for (int i = 0; i < SENSOR_COUNT; i++) {
        int *sensor_id = malloc(sizeof(int));
        *sensor_id = i;
        if (pthread_create(&threads[i], NULL, simulate_sensor, sensor_id) != 0) {
            perror("Erro ao criar thread");
            exit(EXIT_FAILURE);
        }
    }

    // Aguardar o término das threads
    for (int i = 0; i < SENSOR_COUNT; i++) {
        pthread_join(threads[i], NULL);
        
    }

    
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);
    munmap(shared_data, sizeof(SensorData));
    shm_unlink(SHARED_MEM_NAME);

    return 0;
}
