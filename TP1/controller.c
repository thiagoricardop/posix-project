//////////////////////////////////////////////////////////////////////////////////
// Universidade Federal de Perambuco (UFPE)
// Curso: Residencia Tecnologica em Engenharia de Software
// Disciplina : Padrão POSIX
// Professor : Renato Coral Sampaio
// Aluno: Thiago Ricardo Oliveira Pinto
//////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <mqueue.h>  // Para mqueue


// Definições de teclas e constantes do sistema
#define UPDATE_INTERVAL 1
#define MAX_SPEED 200.0
#define MIN_SPEED 0.0
#define MAX_TEMP_MOTOR 140
#define MAX_ROTACAO 7000
#define SHARED_MEM_NAME "/shared_sensors"
#define SEMAPHORE_NAME "/sensor_semaphore"
#define MQ_NAME "/controller_mqueue"

// Estado do sistema
typedef struct {
    int acelerador;
    int freio;
    int farol_baixo;
    int farol_alto;
    int seta_esquerda;
    int seta_direita;
    double velocidade;
    int rpm;
    double temperatura;
} SystemState;

typedef struct {
    double velocidade;
    int rpm;
    double temperatura;
    bool acelera;
    bool freia;
    int pedal;
} SensorData;

typedef struct {
    long msg_type;
    char command;
    int pedal;
} Message;

sem_t *sem;

volatile sig_atomic_t running = 1;
volatile sig_atomic_t paused = 0;

// Função de manipulação de sinais
void handle_signal(int signal) {
    if (signal == SIGUSR1) {
        paused = !paused;
    } else if (signal == SIGUSR2) {
        running = 0;
    }
}

// Exibição do estado do sistema
void display_status(const SystemState *state) {
    system("clear");
    printf("=== Estado do Sistema ===\n");
    printf("Velocidade: %.2f km/h\n", state->velocidade);
    printf("Rotação (RPM): %d\n", state->rpm);
    printf("Temperatura: %.2f °C\n", state->temperatura);
    printf("Acelerador: %s\n", state->acelerador ? "Ativado" : "Desativado");
    printf("Freio: %s\n", state->freio ? "Ativado" : "Desativado");
    printf("Farol baixo: %s\n", state->farol_baixo ? "Ligado" : "Desligado");
    printf("Farol alto: %s\n", state->farol_alto ? "Ligado" : "Desligado");
    printf("Seta Direita: %s\n", state->seta_direita ? "Ligado" : "Desligado");
    printf("Seta Esquerda: %s\n", state->seta_esquerda ? "Ligado" : "Desligado");
    printf("=========================\n");
}

int main() {
    // Configurar sinais
    signal(SIGUSR1, handle_signal);
    signal(SIGUSR2, handle_signal);

    // Configuração da memória compartilhada para o controlador
    int shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Erro ao abrir memória compartilhada");
        exit(EXIT_FAILURE);
    }

    // Configuração do tamanho da memória compartilhada (garantir que já foi definido pelo sensor)
    if (ftruncate(shm_fd, sizeof(SensorData)) == -1) {
        perror("Erro ao configurar tamanho da memória compartilhada");
        exit(EXIT_FAILURE);
    }

    // Mapeamento da memória compartilhada no controlador
    SensorData *shared_data = mmap(NULL, sizeof(SensorData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        perror("Erro ao mapear memória compartilhada");
        exit(EXIT_FAILURE);
    }

    // Criação do semáforo para sincronização
    sem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("Erro ao criar semáforo");
        exit(EXIT_FAILURE);
    }
    // Configuração da fila de mensagens POSIX
    mqd_t mq_descriptor;
    struct mq_attr mq_attributes;
    mq_attributes.mq_flags = 0;
    mq_attributes.mq_maxmsg = 10;
    mq_attributes.mq_msgsize = sizeof(Message);
    mq_attributes.mq_curmsgs = 0;

    mq_descriptor = mq_open(MQ_NAME, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &mq_attributes);
    if (mq_descriptor == (mqd_t)-1) {
        perror("Erro ao criar/abrir a fila de mensagens");
        shmdt(shared_data);
        shmctl(shm_fd, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    SystemState state = {0};
    

    while (running) {
        if (!paused) {
            sem_wait(sem); // Acesso exclusivo à memória compartilhada
            state.velocidade = shared_data->velocidade;
            state.rpm = shared_data->rpm;
            state.temperatura = shared_data->temperatura;
            state.acelerador = shared_data->acelera;
            state.freio = shared_data->freia;
            sem_post(sem); // Libera acesso à memória compartilhada

            // Exibir o estado atualizado
            display_status(&state);

            // Atualizar lógica de controle
            if (state.velocidade > MAX_SPEED) {
                state.freio = 1;
                state.acelerador = 0;
                sem_wait(sem);
                shared_data->acelera = false;
                shared_data->freia = true;
                sem_post(sem);
            }  else if (state.temperatura > MAX_TEMP_MOTOR) {
                state.acelerador = 0;
                state.freio = 1;
                sem_wait(sem);
                shared_data->acelera = false;
                shared_data->freia = true;
                sem_post(sem);
            } else if (state.rpm >= MAX_ROTACAO) {
                state.acelerador = 0;
                state.freio = 0;
                sem_wait(sem);
                shared_data->acelera = false;
                shared_data->freia = false;
                sem_post(sem);
            } else if (state.velocidade <= MIN_SPEED) {
                state.acelerador = 0;
                state.freio = 0;
                sem_wait(sem);
                shared_data->acelera = false;
                shared_data->freia = false;
                sem_post(sem);
            }
        }

        // Processar comandos recebidos do painel
        Message msg;
        if (mq_receive(mq_descriptor, (char *)&msg, sizeof(Message), NULL) != -1) {
            sem_wait(sem); // Acesso exclusivo à memória compartilhada
            switch (msg.command) {
                case '1': // Acelerador
                    if(state.acelerador == 1 && msg.pedal == 0){
                        state.acelerador = 0;
                        state.freio = 0;
                        shared_data->acelera = false;
                        shared_data->freia = false;
                        shared_data->pedal = msg.pedal;
                    }
                    else{
                        state.acelerador = 1;
                        state.freio = 0;
                        shared_data->acelera = true;
                        shared_data->freia = false;
                        shared_data->pedal = msg.pedal;
                    }

                    break;
                case '2': // Freio
                    if(state.freio == 1 && msg.pedal == 0){
                        state.freio = 0;
                        state.acelerador = 0;
                        shared_data->acelera = false;
                        shared_data->freia = false;
                        shared_data->pedal = msg.pedal;
                        break;
                    }
                    else{
                        state.freio = 1;
                        state.acelerador = 0;
                        shared_data->acelera = false;
                        shared_data->freia = true;
                        shared_data->pedal = msg.pedal;
                        break;
                    }
                                        
                case '3': // Farol baixo
                    state.farol_baixo = !state.farol_baixo;
                    break;
                case '4': // Farol alto
                    state.farol_alto = !state.farol_alto;
                    break;
                case '5': // Seta direita
                    state.seta_direita = !state.seta_direita;
                    if (state.seta_esquerda == 1) state.seta_esquerda = 0;
                    break;
                case '6': // Seta esquerda
                    state.seta_esquerda = !state.seta_esquerda;
                    if (state.seta_direita == 1) state.seta_direita = 0;
                    break;
                case '0': // Encerrar
                    printf("Encerrando o painel do carro...\n");
                    running = 0;
                    sem_post(sem);
                    break;
                default:
                    printf("Comando desconhecido: %c\n", msg.command);
            }
            sem_post(sem); // Libera acesso à memória compartilhada
        }

        sleep(UPDATE_INTERVAL); // Pausa para evitar loop excessivamente rápido
    }

    // Encerramento
    sem_close(sem);
    sem_unlink(SEMAPHORE_NAME);
    shmdt(shared_data);
    shmctl(shm_fd, IPC_RMID, NULL);
    if (mq_close(mq_descriptor) == -1) {
        perror("Erro ao fechar a fila de mensagens");
    }
    if (mq_unlink(MQ_NAME) == -1) {
        perror("Erro ao remover a fila de mensagens");
    }

    printf("Controlador encerrado.\n");
    return 0;
}
