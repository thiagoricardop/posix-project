#include "common.h"
#include <sys/select.h>


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
    int cruise_control;
    double velocidade_cruzeiro;
} SystemState;

SystemState state = {0};
volatile int running = 1;
CarData *shared_data;
sem_t *sem_data;
mqd_t mq;

void update_system_state() {
    sem_wait(sem_data);
    
    // Atualizar estado do sistema com dados da memória compartilhada
    state.velocidade = shared_data->velocidade;
    state.rpm = shared_data->rpm;
    state.temperatura = shared_data->temp_motor;
    state.acelerador = shared_data->pedal_acelerador;
    state.freio = shared_data->pedal_freio;
    state.farol_baixo = shared_data->farol_baixo;
    state.farol_alto = shared_data->farol_alto;
    state.seta_esquerda = shared_data->seta_esquerda;
    state.seta_direita = shared_data->seta_direita;
    state.cruise_control = shared_data->cruise_control_ativo;
    state.velocidade_cruzeiro = shared_data->velocidade_cruzeiro;
    
    sem_post(sem_data);
}

void update_actuators() {
    
    // Atualizar PWM do motor baseado no estado do sistema
    if (state.acelerador) {
        softPwmWrite(PIN_MOTOR_PWM, 50);
        usleep(1000);
        softPwmWrite(PIN_FREIO_PWM, 0);
        digitalWrite(PIN_LUZ_FREIO, LOW);
    } else if (state.freio) {
        softPwmWrite(PIN_MOTOR_PWM, 0);
        usleep(1000);
        softPwmWrite(PIN_FREIO_PWM, 50);
        digitalWrite(PIN_LUZ_FREIO, HIGH);
    } else {
        softPwmWrite(PIN_MOTOR_PWM, 0);
        usleep(1000);
        softPwmWrite(PIN_FREIO_PWM, 0);
        digitalWrite(PIN_LUZ_FREIO, LOW);
    }
    
    // Atualizar luzes baseado no estado do sistema
    digitalWrite(PIN_FAROL_ALTO, state.farol_alto);
    digitalWrite(PIN_FAROL_BAIXO, state.farol_baixo);
    digitalWrite(PIN_LUZ_SETA_ESQ, state.seta_esquerda);
    digitalWrite(PIN_LUZ_SETA_DIR, state.seta_direita);
    
}

void process_command(Command cmd) {
    
    switch(cmd.tipo) {
        case CMD_ACELERAR:
            state.acelerador = cmd.valor;
            state.freio = 0;
            state.cruise_control = 0;
            break;
            
        case CMD_FREAR:
            state.acelerador = 0;
            state.freio = cmd.valor;
            state.cruise_control = 0;
            break;
            
        case CMD_PARAR:
            state.acelerador = 0;
            state.freio = 0;
            state.cruise_control = 0;
            break;
            
        case CMD_FAROL_ALTO:
            state.farol_alto = cmd.valor;
            break;
            
        case CMD_FAROL_BAIXO:
            state.farol_baixo = cmd.valor;
            break;
            
        case CMD_SETA_ESQ:
            state.seta_esquerda = cmd.valor;
            break;
            
        case CMD_SETA_DIR:
            state.seta_direita = cmd.valor;
            break;
            
        case CMD_CRUISE_CONTROL:
            if (state.velocidade > 0) {
                state.cruise_control = 1;
                state.velocidade_cruzeiro = state.velocidade;
            }
            break;
            
        case CMD_CC_CANCEL:
            state.cruise_control = 0;
            state.acelerador = 0;
            state.freio = 0;
            break;
    }
    sem_wait(sem_data);
    // Atualizar memória compartilhada com novo estado
    shared_data->pedal_acelerador = state.acelerador;
    shared_data->pedal_freio = state.freio;
    shared_data->farol_alto = state.farol_alto;
    shared_data->farol_baixo = state.farol_baixo;
    shared_data->seta_esquerda = state.seta_esquerda;
    shared_data->seta_direita = state.seta_direita;
    shared_data->cruise_control_ativo = state.cruise_control;
    shared_data->velocidade_cruzeiro = state.velocidade_cruzeiro;
    sem_post(sem_data);
}

void verify_system_state() {
    // Verificar limites de velocidade e temperatura
    if (state.velocidade > MAX_SPEED || state.temperatura > MAX_TEMP_MOTOR) {
        state.acelerador = 0;
        state.freio = 1;
        state.cruise_control = 0;
    }
    
    // Verificar RPM
    if (state.rpm >= MAX_ROTACAO) {
        state.acelerador = 0;
    }
    
    // Atualizar memória compartilhada
    sem_wait(sem_data);
    shared_data->pedal_acelerador = state.acelerador;
    shared_data->pedal_freio = state.freio;
    shared_data->cruise_control_ativo = state.cruise_control;
    sem_post(sem_data);
}

void update_cruise_control() {
    if (state.cruise_control) {
        float erro = state.velocidade_cruzeiro - state.velocidade;
        
        
        if (erro > 2) {
            state.acelerador = 1;
            
        } else if (erro < 2) {
            state.acelerador = 0;
            
        }
        sem_wait(sem_data);
        shared_data->pedal_acelerador = state.acelerador;
        shared_data->pedal_freio = state.freio;
        sem_post(sem_data);
    }
}

void display_status() {
    printf("\rVel: %.1f km/h | RPM: %d | Temp: %.1f°C | Motor: %d%% | Freio: %d%%",
           state.velocidade,
           state.rpm,
           state.temperatura,
           state.acelerador ? 50 : 0,
           state.freio ? 50 : 0);
    fflush(stdout);
}

void init_gpio() {
    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "Falha ao inicializar GPIO\n");
        exit(1);
    }
    
    // Configurar pinos
    pinMode(PIN_MOTOR_DIR1, OUTPUT);
    digitalWrite(PIN_MOTOR_DIR1, HIGH);
    pinMode(PIN_FAROL_ALTO, OUTPUT);
    pinMode(PIN_FAROL_BAIXO, OUTPUT);
    pinMode(PIN_LUZ_FREIO, OUTPUT);
    pinMode(PIN_LUZ_SETA_ESQ, OUTPUT);
    pinMode(PIN_LUZ_SETA_DIR, OUTPUT);
    
    // Criar PWM software
    if (softPwmCreate(PIN_MOTOR_PWM, 0, 100) != 0 ||
        softPwmCreate(PIN_FREIO_PWM, 0, 100) != 0) {
        printf("Erro ao criar PWMs\n");
        exit(1);
    }

    
}

void cleanup() {
    softPwmWrite(PIN_MOTOR_PWM, 0);
    softPwmWrite(PIN_FREIO_PWM, 0);
    softPwmStop(PIN_MOTOR_PWM);
    softPwmStop(PIN_FREIO_PWM);
    
    digitalWrite(PIN_LUZ_FREIO, LOW);
    digitalWrite(PIN_FAROL_ALTO, LOW);
    digitalWrite(PIN_FAROL_BAIXO, LOW);
    digitalWrite(PIN_LUZ_SETA_ESQ, LOW);
    digitalWrite(PIN_LUZ_SETA_DIR, LOW);
    digitalWrite(PIN_MOTOR_DIR1, LOW);
    sem_close(sem_data);
    sem_unlink(SEM_NAME);
    munmap(shared_data, sizeof(CarData));
    mq_close(mq);
    mq_unlink(MQ_NAME);
}

void handle_sigint(int sig) {
    running = 0;
}

int main() {
    signal(SIGINT, handle_sigint);
    init_gpio();

    // Abrir memória compartilhada
    int shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0666);
    ftruncate(shm_fd, sizeof(CarData));
    shared_data = mmap(NULL, sizeof(CarData), PROT_READ | PROT_WRITE, 
                      MAP_SHARED, shm_fd, 0);

    // Abrir semáforo
    sem_data = sem_open(SEM_NAME, O_CREAT, 0666, 1);

    // Abrir fila de mensagens
    struct mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = sizeof(Command),
        .mq_curmsgs = 0
    };
    mq = mq_open(MQ_NAME, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &attr);

    printf("Controlador iniciado...\n");

    struct timeval timeout;
    Command cmd;
    time_t last_display = 0;

    while(running) {
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000; // 1ms

        update_system_state();
        // Verificar comandos
        if (mq_receive(mq, (char*)&cmd, sizeof(Command), NULL) > 0) {
            process_command(cmd);
        }

        update_cruise_control();
        // Atualizar atuadores e verificar estado
        update_actuators();
        verify_system_state();

        // Atualizar display a cada 1 segundo
        time_t now = time(NULL);
        if (now - last_display >= 1) {
            display_status();
            last_display = now;
        }

        // Dormir para economizar CPU
        select(0, NULL, NULL, NULL, &timeout);
    }

    printf("Finalizando controlador...\n");
    cleanup();
    return 0;
}