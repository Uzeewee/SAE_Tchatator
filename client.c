#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char message[BUFFER_SIZE];
    char client_name[BUFFER_SIZE] = "client";

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

    while (1) {
        printf("Entrez votre requête %s: ", client_name);
        fgets(message, BUFFER_SIZE, stdin);
        message[strcspn(message, "\n")] = 0;

        send(sock, message, strlen(message), 0);

        if (strcmp(message, "EXIT") == 0) {
            printf("Déconnexion demandée.\n");
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);
        read(sock, buffer, BUFFER_SIZE);

        if (strncmp(buffer, "LOGIN OK:", 9) == 0) {
            strcpy(client_name, buffer + 9); // Extraire le nom du client
            printf("Connexion réussie ! Bienvenue %s.\n", client_name);
        } else {
            printf("Réponse du serveur : %s\n", buffer);
        }
    }

    close(sock);
    return 0;
}
