#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>  // fstat için

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

int init_socket(int port);
void sigint_handler(int signo);
void broadcast_file(int sender_socket, int *client_sockets, int max_clients, const char *filename);

int main(int argc, char **argv){

    if (argc != 2){ // ip is localhost
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int server_socket = init_socket(atoi(argv[1]));
    int client_sockets[MAX_CLIENTS] = {0};

    if (server_socket < 0){
        return 1;
    }

    fd_set readfds;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, &sigint_handler);

    while (1){
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);

        int max_fd = server_socket;

        for (int i = 0; i < MAX_CLIENTS; i++){
            if (client_sockets[i] > 0){
                FD_SET(client_sockets[i], &readfds);
                if (client_sockets[i] > max_fd){
                    max_fd = client_sockets[i];
                }
            }
        }

        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0){
            perror("select() failed");
            break;
        }

        if (FD_ISSET(server_socket, &readfds)){
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_socket < 0){
                perror("accept() failed");
                break;
            }

            printf("New client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            for (int i = 0; i < MAX_CLIENTS; i++){
                if (client_sockets[i] == 0){
                    client_sockets[i] = client_socket;
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++){
            if (client_sockets[i] > 0 && FD_ISSET(client_sockets[i], &readfds)){
                char buffer[1024] = {0};
                int read_bytes = read(client_sockets[i], buffer, sizeof(buffer));
                if (read_bytes <= 0){
                    printf("Client disconnected\n");
                    close(client_sockets[i]);
                    client_sockets[i] = 0;
                } else {
                    printf("Received: %s", buffer);
                    if (strncmp(buffer, "send", 4) == 0){
                        char filename[BUFFER_SIZE];
                        sscanf(buffer + 5, "%s", filename);
                        printf("İstemciden dosya gönderme isteği: %s\n", filename);
                        broadcast_file(client_sockets[i], client_sockets, MAX_CLIENTS, filename);
                    } else {
                        for (int j = 0; j < MAX_CLIENTS; j++){
                            if (client_sockets[j] != 0 && client_sockets[j] != client_sockets[i]){
                                if (send(client_sockets[j], buffer, read_bytes, 0) < 0){
                                    perror("send() failed");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

int init_socket(int port){
    int server_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (server_socket < 0){
        perror("socket() failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("bind() failed");
        close(server_socket);
        return -1;
    }

    if (listen(server_socket, MAX_CLIENTS) < 0){
        perror("listen() failed");
        close(server_socket);
        return -1;
    }

    printf("Server started on port %d\n", port);

    return server_socket;
}

void sigint_handler(int signo){
    printf("Server is shutting down\n");
    exit(0);
}

void broadcast_file(int sender_socket, int *client_sockets, int max_clients, const char *filename) {
    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        perror("Dosya açılamadı.");
        return;
    }

    // Dosya boyutunu al
    struct stat file_stat;
    if (fstat(file_fd, &file_stat) < 0) {
        perror("Dosya bilgileri alınamadı.");
        close(file_fd);
        return;
    }

    // Dosya adını ve boyutunu içeren bir başlık hazırla
    char header[BUFFER_SIZE];
    snprintf(header, BUFFER_SIZE, "FILE:%s:%lld", filename, file_stat.st_size);
    
    // Başlığı tüm istemcilere gönder
    for (int i = 0; i < max_clients; i++) {
        if (client_sockets[i] != 0 && client_sockets[i] != sender_socket) {
            if (send(client_sockets[i], header, strlen(header), 0) < 0) {
                perror("Dosya başlığı gönderilemedi.");
            }
            // İstemcilere hazırlanmaları için kısa bir süre ver
            usleep(100000); // 100ms
        }
    }

    // Dosya içeriğini oku ve gönder
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    while ((bytes_read = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        for (int i = 0; i < max_clients; i++) {
            if (client_sockets[i] != 0 && client_sockets[i] != sender_socket) {
                if (send(client_sockets[i], buffer, bytes_read, 0) < 0) {
                    perror("Dosya gönderimi başarısız.");
                }
            }
        }
    }

    printf("Dosya %s istemcilere gönderildi.\n", filename);
    close(file_fd);
}