#include "common.h"
#include <sys/select.h>

volatile int running = 1;
CarData *shared_data;
sem_t *sem_data;

// Variáveis para cálculos
float rpm_calculado = 0.0;
float vel_calculada = 0.0;
float temp_motor = 0.0;
static unsigned long last_time_motor = 0;
static unsigned long last_time_roda = 0;
static unsigned long last_time_temp = 0;
static unsigned last_hall_motor = 0;
static unsigned last_hall_a = 0;
unsigned long pulsos_motor = 0;
unsigned long pulsos_roda = 0;

void handle_sigint(int sig) {
    running = 0;
}

void update_measurements(unsigned long current_time) {
    sem_wait(sem_data);
    
    // Atualizar RPM (a cada 2 segundos)
    if (current_time - last_time_motor >= 1000) {
        // Limitar o RPM entre MIN_ROTACAO e MAX_ROTACAO
        rpm_calculado = (pulsos_motor * 60);
        shared_data->rpm = rpm_calculado;
        pulsos_motor = 0;
        last_time_motor = current_time;
    }
    else if (current_time - last_time_roda >= 1000) {
        // Atualizar velocidade (a cada segundo)
        vel_calculada = pulsos_roda * (0.63* PI * 3.6); // Calcula a velocidade em km/h
        shared_data->velocidade = vel_calculada;
        pulsos_roda = 0;
        last_time_roda = current_time;
    } else if (current_time - last_time_temp >= 1000) {
        // Calcular temperatura baseada no RPM atual e velocidade
        float temp_rise = (rpm_calculado - MIN_ROTACAO) * FACTOR_ACELERACAO;
        float cooling_effect = vel_calculada * FATOR_RESFRIAMENTO_AR;

        // Temperatura deve estar entre BASE_TEMP e MAX_TEMP_MOTOR
        temp_motor = fmin(
            fmax(
                BASE_TEMP + temp_rise - cooling_effect,
                BASE_TEMP
            ),
            MAX_TEMP_MOTOR
        );

        shared_data->temp_motor = temp_motor;
        last_time_temp = current_time;
        }

    sem_post(sem_data);    
}
   
    
void process_hall_sensors() {
    
    // Leitura do sensor Hall do motor
    int hall_motor = digitalRead(PIN_SENSOR_HALL_MOTOR);
    
    // Só incrementa pulsos se houver mudança de estado e acelerador estiver pressionado
    if (hall_motor != last_hall_motor && hall_motor == HIGH) {
        pulsos_motor++;
    }
    last_hall_motor = hall_motor;
    
    // Leitura dos sensores Hall da roda
    int hall_a = digitalRead(PIN_SENSOR_HALL_RODA_A);
    
    if (hall_a != last_hall_a) {
        pulsos_roda++;
    }
    
    last_hall_a = hall_a;
}

void init_gpio() {
    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "Falha ao inicializar GPIO\n");
        exit(1);
    }
    
    // Configurar pinos dos sensores
    pinMode(PIN_SENSOR_HALL_MOTOR, INPUT);
    pinMode(PIN_SENSOR_HALL_RODA_A, INPUT);
    pinMode(PIN_SENSOR_HALL_RODA_B, INPUT);
    
    // Configurar pull-down para os sensores
    pullUpDnControl(PIN_SENSOR_HALL_MOTOR, PUD_DOWN);
    pullUpDnControl(PIN_SENSOR_HALL_RODA_A, PUD_DOWN);
    pullUpDnControl(PIN_SENSOR_HALL_RODA_B, PUD_DOWN);
}

void init_shared_memory() {
    // Criar/abrir memória compartilhada
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Erro ao criar memória compartilhada");
        exit(1);
    }
    
    if (ftruncate(shm_fd, sizeof(CarData)) == -1) {
        perror("Erro ao definir tamanho da memória compartilhada");
        exit(1);
    }
    
    shared_data = mmap(NULL, sizeof(CarData), PROT_READ | PROT_WRITE, 
                      MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        perror("Erro ao mapear memória compartilhada");
        exit(1);
    }
    
    // Inicializar dados
    memset(shared_data, 0, sizeof(CarData));

    // Inicializar todas as variáveis com zero
    sem_wait(sem_data);
    
    // Inicializar dados dos sensores
    shared_data->velocidade = 0.0;
    shared_data->rpm = 0.0;
    shared_data->temp_motor = BASE_TEMP;  // Temperatura inicial = temperatura base
    
    // Inicializar estado dos atuadores
    shared_data->farol_alto = 0;
    shared_data->farol_baixo = 0;
    shared_data->seta_esquerda = 0;
    shared_data->seta_direita = 0;
    shared_data->luz_freio = 0;
    shared_data->pedal_acelerador = 0;
    shared_data->pedal_freio = 0;
    
    // Inicializar Cruise Control
    shared_data->cruise_control_ativo = 0;
    shared_data->velocidade_cruzeiro = 0.0;
    
    sem_post(sem_data);
}

void display_status() {
    printf("\rRPM: %.0f | Vel: %.1f km/h | Temp: %.1f°C",
           rpm_calculado,
           vel_calculada,
           temp_motor);
    fflush(stdout);
}

void cleanup() {
    sem_wait(sem_data);
    
    // Inicializar dados dos sensores
    shared_data->velocidade = 0.0;
    shared_data->rpm = 0.0;
    shared_data->temp_motor = BASE_TEMP;  // Temperatura inicial = temperatura base
    shared_data->direcao = 0;  // 0 = parado
    
    // Inicializar estado dos atuadores
    shared_data->farol_alto = 0;
    shared_data->farol_baixo = 0;
    shared_data->seta_esquerda = 0;
    shared_data->seta_direita = 0;
    shared_data->luz_freio = 0;
    shared_data->pedal_acelerador = 0;
    shared_data->pedal_freio = 0;
    
    // Inicializar Cruise Control
    shared_data->cruise_control_ativo = 0;
    shared_data->velocidade_cruzeiro = 0.0;
    
    sem_post(sem_data);
    sem_close(sem_data);
    sem_unlink(SEM_NAME);
    munmap(shared_data, sizeof(CarData));
}

int main() {
    signal(SIGINT, handle_sigint);
    
    init_gpio();
    
    
    // Criar/abrir semáforo
    sem_data = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem_data == SEM_FAILED) {
        perror("Erro ao criar semáforo");
        exit(1);
    }

    init_shared_memory();

    printf("Sensores iniciados. Monitorando...\n");
    
    struct timeval timeout;
    time_t last_display = 0;
    
    while(running) {
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000; // 1ms
        
        // Processar sensores
        process_hall_sensors();
        
        // Atualizar medições
        unsigned long current_time = millis();
        update_measurements(current_time);
        
        // Atualizar display a cada segundo
        time_t now = time(NULL);
        if (now - last_display >= 1) {
            display_status();
            last_display = now;
        }
        
        // Dormir para economizar CPU
        select(0, NULL, NULL, NULL, &timeout);
    }
    printf("\nEncerrando sensores...\n");
    cleanup();
    
    return 0;
}