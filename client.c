#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Kullanım: %s <sunucu_ip> <port>\n", argv[0]);
        return 1;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket() başarısız");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("Geçersiz IP adresi");
        return 1;
    }

    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Sunucuya bağlanılamadı");
        return 1;
    }

    printf("Sunucuya bağlanıldı\n");

    char buffer[BUFFER_SIZE];
    fd_set readfds;
    int file_fd = -1;
    long file_size = 0;
    long received_bytes = 0;
    char current_file[BUFFER_SIZE] = {0};

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(socket_fd, &readfds);

        int activity = select(socket_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select() başarısız");
            break;
        }

        // Klavyeden giriş var mı?
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
                break;
            }
            
            send(socket_fd, buffer, strlen(buffer), 0);
        }

        // Sunucudan mesaj var mı?
        if (FD_ISSET(socket_fd, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int read_bytes = recv(socket_fd, buffer, BUFFER_SIZE, 0);
            
            if (read_bytes <= 0) {
                printf("Sunucu bağlantısı kapandı\n");
                break;
            }

            // Dosya transferi başlangıcı kontrolü
            if (strncmp(buffer, "FILE:", 5) == 0) {
                // Mevcut dosyayı kapat (eğer varsa)
                if (file_fd != -1) {
                    close(file_fd);
                    file_fd = -1;
                }
                
                // Dosya adı ve boyutu ayıkla
                char *file_name = strtok(buffer + 5, ":");
                char *size_str = strtok(NULL, ":");
                
                if (file_name && size_str) {
                    // Dosya adını kaydet
                    strcpy(current_file, file_name);
                    
                    // Dosya boyutunu al
                    file_size = atol(size_str);
                    received_bytes = 0;
                    
                    // Dosyayı oluştur/aç
                    file_fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (file_fd < 0) {
                        perror("Dosya oluşturulamadı");
                    } else {
                        printf("Dosya alınıyor: %s (%ld bytes)\n", file_name, file_size);
                    }
                }
            } 
            // Eğer dosya transferi devam ediyorsa
            else if (file_fd != -1) {
                // Verileri dosyaya yaz
                write(file_fd, buffer, read_bytes);
                received_bytes += read_bytes;
                
                // İlerlemeyi göster
                printf("\rDosya indiriliyor: %s - %ld/%ld bytes (%%%.1f)", 
                       current_file, received_bytes, file_size, 
                       (float)received_bytes * 100 / file_size);
                fflush(stdout);
                
                // Dosya tamamlandı mı?
                if (received_bytes >= file_size) {
                    printf("\nDosya başarıyla alındı: %s\n", current_file);
                    close(file_fd);
                    file_fd = -1;
                }
            } 
            // Normal mesaj
            else {
                printf("Sunucudan: %s", buffer);
            }
        }
    }

    close(socket_fd);
    return 0;
}