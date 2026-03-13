#define BS_SERVER_IP "185.231.247.18"
#define BS_SERVER_PORT 443

#define BS_CONN_LENGTH 10


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pty.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#include <pthread.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

int serial_fd;
int bs_fd;
struct sockaddr_in bs_server_addr;

int sent_bytes = 0;
bool connected = false;

void sleep_ms(int ms){
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = ms * 1000000;
    nanosleep(&req, NULL); 
}

void bs_reconnect(){
    printf("reconnecting...\n");

    if ((bs_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Ошибка создания сокета");
        exit(EXIT_FAILURE);
    }
    while(!connected){
        if (connect(bs_fd, (struct sockaddr *)&bs_server_addr, sizeof(bs_server_addr)) < 0) {
            perror("Ошибка подключения");
            continue;
        }
        connected = true;
    }

    sent_bytes = 0;

    printf("reconnected\n");
}

void* serial_to_bs_loop(void* args){
    printf("serial input loop started\n");

    uint8_t buf[65535];
    while(true){
        while(!connected){
            sleep_ms(1);
        }
        int len = read(serial_fd, buf, MIN(BS_CONN_LENGTH-sent_bytes,sizeof(buf)));
        if (len > 0) {
            printf("received %i bytes\n",len);
            sent_bytes += len;
            write(bs_fd, buf, len);
        }else{
            perror("ошибка одна и ошибся ты");
        }
        if(sent_bytes == BS_CONN_LENGTH){
            printf("reached sent bytes limit\n");
            connected = false;
            shutdown(bs_fd, SHUT_RDWR);
        }
    }

    return NULL;
}

void* bs_to_serial_loop(void* args){
    printf("serial output loop started\n");

    while(true){
        while(!connected){
            sleep_ms(1);
        }
        uint8_t buf[65535];
        int len = read(bs_fd, buf, sizeof(buf));
        if(len>0){
            printf("received %i bytes\n",len);
            write(serial_fd, buf, len);
        }else{
            perror("aboba");
            printf("received EOF\n");
            connected = false;
            close(bs_fd);
            bs_reconnect();
        }
    }

    return NULL;
}

int main() {
    //create serial port
    int serial_slave_fd;
    char serial_name[256];
    if (openpty(&serial_fd, &serial_slave_fd, serial_name, NULL, NULL) == -1) {
        perror("openpty failed");
        exit(1);
    }
    printf("created serial port: %s\n", serial_name);

    //create BS
    memset(&bs_server_addr, '0', sizeof(bs_server_addr));

    bs_server_addr.sin_family = AF_INET;
    bs_server_addr.sin_port = htons(BS_SERVER_PORT);

    if(inet_pton(AF_INET, BS_SERVER_IP, &bs_server_addr.sin_addr) <= 0) {
        perror("Неверный IP адрес");
        exit(EXIT_FAILURE);
    }

    if ((bs_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Ошибка создания сокета");
        exit(EXIT_FAILURE);
    }

    if (connect(bs_fd, (struct sockaddr *)&bs_server_addr, sizeof(bs_server_addr)) < 0) {
        perror("Ошибка подключения");
        exit(EXIT_FAILURE);
    }

    connected = true;

    //create threads
    pthread_t serial_output_thread;
    if (pthread_create(&serial_output_thread, NULL, bs_to_serial_loop, NULL) != 0) {
        perror("Error creating serial_output_thread");
    }
    pthread_t serial_input_thread;
    if (pthread_create(&serial_input_thread, NULL, serial_to_bs_loop, NULL) != 0) {
        perror("Error creating serial_input_thread");
    }

    sleep_ms(100);

    printf("commands:\n");
    printf(" - exit\n");

    while(true){
        char line[128];
        printf("> ");
        fflush(stdout);
        if (fgets(line, 128, stdin) != NULL) {
            line[strcspn(line, "\n")] = 0;
            if(strcmp("exit",line)==0){
                break;
            }
        } else {
            perror("Error reading console input");
        }
    }

    return 0;
}