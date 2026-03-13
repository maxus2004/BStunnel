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
volatile int bs_fd;
int server_fd;
struct sockaddr_in address;
int addrlen = sizeof(address);

int sent_bytes = 0;
bool connected = false;

pthread_mutex_t bs_mutex;

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
        exit(1);
    }
    while(!connected){
        if ((bs_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
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
            sent_bytes += len;
            pthread_mutex_lock( &bs_mutex );
            printf("sending %i bytes to socket %i\n",len,bs_fd);
            write(bs_fd, buf, len);
            printf("sent %i bytes\n",len);
            pthread_mutex_unlock( &bs_mutex );
        }else{
            perror("ошибка одна и ошибся ты");
        }
        if(sent_bytes == BS_CONN_LENGTH){
            printf("reached sent bytes limit\n");
            shutdown(bs_fd, SHUT_WR);
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
            pthread_mutex_lock( &bs_mutex );
            shutdown(bs_fd, SHUT_WR);
            close(bs_fd);
            bs_reconnect();
            pthread_mutex_unlock( &bs_mutex );
        }
    }

    return NULL;
}

int main() {
    pthread_mutex_init(&bs_mutex, NULL);

    //create serial port
    int serial_slave_fd;
    char serial_name[256];
    if (openpty(&serial_fd, &serial_slave_fd, serial_name, NULL, NULL) == -1) {
        perror("openpty failed");
        exit(1);
    }
    printf("created serial port: %s\n", serial_name);

    //create BS
    // Создаем сокет
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Ошибка создания сокета");
        exit(1);
    }

    // Разрешаем повторное использование порта
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Ошибка setsockopt");
        exit(1);
    }

    // Заполняем структуру адреса
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(BS_SERVER_PORT);

    // Привязываем сокет к адресу и порту
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Ошибка привязки");
        exit(1);
    }

    // Начинаем слушать входящие подключения
    if (listen(server_fd, 3) < 0) {
        perror("Ошибка listen");
        exit(1);
    }

    if ((bs_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("Ошибка accept");
        exit(1);
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