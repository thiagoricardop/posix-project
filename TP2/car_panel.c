#include "common.h"
#include <sys/select.h>
#include <time.h>

volatile int running = 1;
mqd_t mq;

void send_command(Command cmd) {
    if (mq_send(mq, (char*)&cmd, sizeof(Command), 0) == -1) {
        perror("Erro ao enviar comando");
    }
}

void handle_gpio_event(int pin) {
    Command cmd;
    int value = digitalRead(pin);

    switch(pin) {
        case PIN_PEDAL_ACEL:
            cmd.tipo = CMD_ACELERAR;
            cmd.valor = value ? 50 : 0;  // 50% de potência quando pressionado
            break;
        case PIN_PEDAL_FREIO:
            cmd.tipo = CMD_FREAR;
            cmd.valor = value ? 100 : 0;  // Freio total quando pressionado
            break;
        case PIN_CMD_FAROL:
            cmd.tipo = CMD_FAROL_BAIXO;
            cmd.valor = value;
            break;
        case PIN_CMD_FAROL_ALTO:
            cmd.tipo = CMD_FAROL_ALTO;
            cmd.valor = value;
            break;
        case PIN_CMD_SETA_ESQ:
            cmd.tipo = CMD_SETA_ESQ;
            cmd.valor = value;
            break;
        case PIN_CMD_SETA_DIR:
            cmd.tipo = CMD_SETA_DIR;
            cmd.valor = value;
            break;
        case PIN_CC_RES:
            cmd.tipo = CMD_CRUISE_CONTROL;
            cmd.valor = value;
            break;
        case PIN_CC_CANCEL:
            cmd.tipo = CMD_CC_CANCEL;
            cmd.valor = value;
            break;
        default:
            return;
    }

    // Se nenhum pedal está pressionado, enviar comando de parar
    if (pin == PIN_PEDAL_ACEL || pin == PIN_PEDAL_FREIO) {
        if (digitalRead(PIN_PEDAL_ACEL) == LOW && digitalRead(PIN_PEDAL_FREIO) == LOW) {
            cmd.tipo = CMD_PARAR;
            cmd.valor = 0;
        }
    }

    send_command(cmd);
}

void init_gpio() {
    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "Falha ao inicializar GPIO\n");
        exit(1);
    }

    // Array com todos os pinos que precisamos monitorar
    int pins[] = {
        PIN_PEDAL_ACEL, PIN_PEDAL_FREIO,
        PIN_CMD_FAROL, PIN_CMD_FAROL_ALTO,
        PIN_CMD_SETA_ESQ, PIN_CMD_SETA_DIR,
        PIN_CC_RES, PIN_CC_CANCEL
    };
    int num_pins = sizeof(pins) / sizeof(pins[0]);

    // Configurar cada pino
    for (int i = 0; i < num_pins; i++) {
        pinMode(pins[i], INPUT);
        pullUpDnControl(pins[i], PUD_DOWN);
    }
}

void cleanup() {
    mq_close(mq);
    mq_unlink(MQ_NAME);
}

void handle_sigint(int sig) {
    running = 0;
}

int main() {
    signal(SIGINT, handle_sigint);
    init_gpio();

    // Abrir fila de mensagens
    struct mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = sizeof(Command),
        .mq_curmsgs = 0
    };

    mq = mq_open(MQ_NAME, O_WRONLY | O_CREAT, 0666, &attr);
    if (mq == (mqd_t)-1) {
        perror("Erro ao abrir fila de mensagens");
        exit(1);
    }

    printf("Painel iniciado. Monitorando entradas...\n");
    printf("Pressione Ctrl+C para encerrar\n");

    // Array com todos os pinos que precisamos monitorar
    int pins[] = {
        PIN_PEDAL_ACEL, PIN_PEDAL_FREIO,
        PIN_CMD_FAROL, PIN_CMD_FAROL_ALTO,
        PIN_CMD_SETA_ESQ, PIN_CMD_SETA_DIR,
        PIN_CC_RES, PIN_CC_CANCEL
    };
    int num_pins = sizeof(pins) / sizeof(pins[0]);

    // Arrays para armazenar estados anteriores
    int prev_states[8] = {0};
    
    // Estruturas para select()
    struct timeval timeout;
    
    while(running) {
        // Configurar timeout para 100ms
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  // 100ms

        // Verificar mudanças nos pinos
        int changes = 0;
        for (int i = 0; i < num_pins; i++) {
            int current_state = digitalRead(pins[i]);
            if (current_state != prev_states[i]) {
                handle_gpio_event(pins[i]);
                prev_states[i] = current_state;
                changes++;
            }
        }

        // Se não houve mudanças, dormir
        if (changes == 0) {
            select(0, NULL, NULL, NULL, &timeout);
        }

        // Atualizar display a cada 1 segundo
        static time_t last_display = 0;
        time_t now = time(NULL);
        if (now - last_display >= 1) {
            printf("\rAcel: %d | Freio: %d | Farol baixo: %d | Farol alto: %d | Seta Esq: %d | Seta Dir: %d | CC: %d",
                   digitalRead(PIN_PEDAL_ACEL),
                   digitalRead(PIN_PEDAL_FREIO),
                   digitalRead(PIN_CMD_FAROL),
                   digitalRead(PIN_CMD_FAROL_ALTO),
                   digitalRead(PIN_CMD_SETA_ESQ),
                   digitalRead(PIN_CMD_SETA_DIR),
                   digitalRead(PIN_CC_RES));
            fflush(stdout);
            last_display = now;
        }
    }

    cleanup();
    return 0;
}