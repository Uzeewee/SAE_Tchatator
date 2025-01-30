#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define REQUEST_COUNT 14
#define REQUEST_INTERVAL 1  // Intervalle en secondes entre chaque requête

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char message[] = "TEST_REQUEST";

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Erreur de création du socket");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Adresse non valide");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erreur de connexion au serveur");
        return -1;
    }

    printf("Connexion au serveur réussie !\n");

    for (int i = 0; i < REQUEST_COUNT; i++) {
        send(sock, message, strlen(message), 0);
        printf("Requête %d envoyée : %s\n", i + 1, message);

        memset(buffer, 0, BUFFER_SIZE);
        read(sock, buffer, BUFFER_SIZE);
        printf("Réponse du serveur : %s\n", buffer);

        sleep(REQUEST_INTERVAL);
    }

    printf("Test terminé. Déconnexion.\n");
    close(sock);
    return 0;
}
