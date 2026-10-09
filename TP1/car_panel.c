//////////////////////////////////////////////////////////////////////////////////
// Universidade Federal de Perambuco (UFPE)
// Curso: Residencia Tecnologica em Engenharia de Software
// Disciplina : Padrão POSIX
// Professor : Renato Coral Sampaio
// Aluno: Thiago Ricardo Oliveira Pinto
//////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>  /* POSIX types */
#include <sys/ipc.h>    /* IPC flags */
#include <sys/msg.h>    /* message queue functions */
#include <errno.h>      /* errno */
#include <unistd.h>     /* POSIX API */
#include <mqueue.h>
#define MQ_NAME "/controller_mqueue" // Nome da fila de mensagens

// Definições dos comandos
#define ACCELERATOR         '1'
#define BRAKE               '2'
#define LOW_BEAM            '3'
#define HIGH_BEAM           '4'
#define LEFT_TURN_SIGNAL    '5'
#define RIGHT_TURN_SIGNAL   '6'
#define EXIT                '0'

typedef struct {
    long type;
    char command;
    int pedal;
} Message;

mqd_t mq_envia;
struct mq_attr mq_attributes;


#define MESSAGE_QUEUE_KEY 5678

void display_menu() {
    printf("=== Painel do Carro ===\n");
    printf("1 - Pressionar pedal do acelerador\n");
    printf("2 - Pressionar pedal do freio\n");
    printf("3 - Ligar/desligar farol baixo\n");
    printf("4 - Ligar/desligar farol alto\n");
    printf("5 - Ligar/desligar seta direita\n");
    printf("6 - Ligar/desligar seta esquerda\n");
    printf("0 - Sair\n");
    printf("Escolha uma opção: ");
}

// Função para enviar uma mensagem
void send_message(char command, int percentage) {
    Message msg;
    msg.type = 1;  // Tipo padrão
    msg.command = command;
    msg.pedal = percentage;

    if((mq_envia = mq_open(MQ_NAME, O_CREAT | O_RDWR, 0666, &mq_attributes)) == (mqd_t)-1)
    {
        perror("Cliente: mq_open");
        exit(1);
    }
     
    if(mq_send(mq_envia, (char*)&msg, sizeof(Message), 0) == (mqd_t)-1)
    {
        perror("Escritor: Unable to send message");
    }
    
}

int main() {
    // Configuração da fila de mensagens POSIX
    mq_attributes.mq_flags = 0;
    mq_attributes.mq_maxmsg = 10;
    mq_attributes.mq_msgsize = sizeof(Message);
    mq_attributes.mq_curmsgs = 0;

    display_menu();
    char option;
    int percentage = 0;
    while (1) {
        if(scanf(" %c", &option) == 1){           
            switch (option) {
                case LEFT_TURN_SIGNAL:
                    send_message(option, percentage);
                    printf("Seta esquerda acionada/desligada.\n");
                    break;
                case RIGHT_TURN_SIGNAL:
                    send_message(option, percentage);
                    printf("Seta direita acionada/desligada.\n");
                    break;
                case LOW_BEAM:
                    send_message(option, percentage);
                    printf("Farol baixo acionado/desligado.\n");
                    break;
                case HIGH_BEAM:
                    send_message(option, percentage);
                    printf("Farol alto acionado/desligado.\n");
                    break;
                case ACCELERATOR: {
                    printf("Digite a porcentagem de pressão no pedal do acelerador (0-100): ");
                    scanf("%d", &percentage);
                    if (percentage >= 0 && percentage <= 100) {
                        printf("Pedal do acelerador pressionado em %d%%.\n", percentage);
                        send_message(option, percentage);
                        break;
                    } else {
                        printf("Porcentagem inválida. Tente novamente.\n");
                        break;
                    }
                    
                }
                case BRAKE: {
                    
                    printf("Digite a porcentagem de pressão no pedal do freio (0-100): ");
                    scanf("%d", &percentage);
                    if (percentage >= 0 && percentage <= 100){
                        printf("Pedal do freio pressionado em %d%%.\n", percentage);
                        send_message(option, percentage);
                        break;
                    } else {
                        printf("Porcentagem inválida. Tente novamente.\n");
                        break;
                    }
                    
                }
                case EXIT:
                    printf("Encerrando o painel do carro...\n");
                    send_message(option, percentage);
                    exit(0);
                default:
                    printf("Opção inválida. Tente novamente.\n");
            }
            display_menu();
        }
    }

    if(mq_close(mq_envia) == (mqd_t)-1)
    {
        perror("Escritor: mq_close");
        exit(1);
    }

    return 0;
}