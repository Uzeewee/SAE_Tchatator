#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void show_main_menu() {
    printf("\nMenu principal:\n");
    printf("1. Se connecter\n");
    printf("2. Quitter\n");
    printf("Choisissez une option: ");
}

void show_user_menu(const char *user_type) {

    

    printf("\nMenu utilisateur:\n");
    printf("1. Envoyer un message\n");
    printf("2. Lire vos nouveau messages\n");
    printf("3. Modifier un message\n");
    printf("4. Supprimer un message\n");
    printf("5. Voir mon historique de message\n");
    if (strcmp(user_type, "Admin") == 0) {
        printf("6. Bloquer un utilisateur\n");
        printf("7. Débloquer un utilisateur\n");
        printf("8. Bannir un utilisateur\n");
        printf("9. Débannir un utilisateur\n");
        printf("10. Voir les tokens\n");
    } else if (strcmp(user_type, "Pro") == 0) {
        printf("6. Bloquer un client\n");
        printf("7. Débloquer un client\n");
    }
    printf("20. Voir mes infos\n");
    printf("30. Voir les config\n");
    printf("0. Déconnexion\n");
    printf("Choisissez une option: ");
}

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char message[BUFFER_SIZE];
    int choice;
    char user_type[20] = {0};
    int is_logged_in = 0;

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
        if (!is_logged_in) {
            show_main_menu();
            scanf("%d", &choice);
            getchar(); // Consommer le \n laissé par scanf

            if (choice == 1) {
                char api_key[BUFFER_SIZE];
                printf("Entrez votre clé API : ");
                fgets(api_key, BUFFER_SIZE, stdin);
                api_key[strcspn(api_key, "\n")] = 0;

                snprintf(message, BUFFER_SIZE, "LOGIN:%.1016s", api_key);

                send(sock, message, strlen(message), 0);

                memset(buffer, 0, BUFFER_SIZE);
                read(sock, buffer, BUFFER_SIZE);

                printf("%s\n", buffer); // Affiche la réponse complète du serveur

                // Analyser la réponse
                if (strstr(buffer, "Connecté en tant que :") != NULL) {
                    is_logged_in = 1; // Met à jour l'état de connexion

                    // Extraire le type d'utilisateur
                    char *user_type_ptr = strstr(buffer, "Connecté en tant que :") + 23; // Après "Connecté en tant que :"
                    sscanf(user_type_ptr, "%s", user_type);

                    printf("Vous êtes connecté en tant que : %s\n", user_type);
                } else {
                    printf("Connexion échouée. Veuillez réessayer.\n");
                }
            } else if (choice == 2) {
                printf("Au revoir !\n");
                break;
            } else {
                printf("Option invalide.\n");
            }
        } else {
            show_user_menu(user_type);
            scanf("%d", &choice);
            getchar(); // Consommer le \n laissé par scanf

            if (choice == 1) {
                int recipient_id;
                char content[BUFFER_SIZE];

                printf("Entrez l'ID du destinataire : ");
                scanf("%d", &recipient_id);
                getchar(); // Consommer le \n laissé par scanf

                printf("Entrez le contenu du message : ");
                fgets(content, BUFFER_SIZE, stdin);
                content[strcspn(content, "\n")] = 0;

                snprintf(message, BUFFER_SIZE, "NEW_MSG:%d,%.1000s", recipient_id, content);

                send(sock, message, strlen(message), 0);

                memset(buffer, 0, BUFFER_SIZE);
                read(sock, buffer, BUFFER_SIZE);
                printf("%s\n", buffer);
            } else if (choice == 2) {
                send(sock, "MSG", strlen("MSG"), 0);
                memset(buffer, 0, BUFFER_SIZE);
                read(sock, buffer, BUFFER_SIZE);
                printf("Messages reçus :\n%s\n", buffer);
            }else if(choice == 3 ){
                int recipient_id;
                char content[BUFFER_SIZE];

                printf("Entrez l'ID du message : ");
                scanf("%d", &recipient_id);
                getchar(); // Consommer le \n laissé par scanf

                printf("Entrez le nouveau contenu du message : ");
                fgets(content, BUFFER_SIZE, stdin);
                content[strcspn(content, "\n")] = 0;

                snprintf(message, BUFFER_SIZE, "MODIF_MSG:%d,%.1000s", recipient_id, content);

                send(sock, message, strlen(message), 0);

                memset(buffer, 0, BUFFER_SIZE);
                read(sock, buffer, BUFFER_SIZE);
                printf("%s\n", buffer);
            }else if (choice == 4){
                int id_message;
                printf("Entrez l'ID du message a supprimer : ");
                scanf("%d", &id_message);
                getchar();
                snprintf(message, BUFFER_SIZE, "REMOVE_MSG:%d",id_message);

            }else if(choice == 5){
                int id_message;
                printf("Entrez l'ID du message a partir du quelle vous voulez voir l'historique : ");
                scanf("%d", &id_message);
                getchar();
                snprintf(message, BUFFER_SIZE, "LOG:%d",id_message);
            }else if (strcmp(user_type, "Admin") == 0 && ( choice == 6 || choice == 7 || choice == 8 || choice == 9 || choice == 10)) {
                int target_id;
                int target_pro;

                

                if (choice == 6) {
                    printf("Entrez l'ID de la cible : ");
                    scanf("%d", &target_id);
                    getchar();
                    printf("Entrez l'ID du pro : ");
                    scanf("%d", &target_pro);
                    getchar();
                    snprintf(message, BUFFER_SIZE, "BLOCK_CLIENT:%d,%d", target_id,target_pro);
                } else if (choice == 7) {
                    printf("Entrez l'ID de la cible : ");
                    scanf("%d", &target_id);
                    getchar();
                    printf("Entrez l'ID du pro : ");
                    scanf("%d", &target_pro);
                    getchar();
                    snprintf(message, BUFFER_SIZE, "UNBLOCK_CLIENT:%d,%d", target_id,target_pro);
                } else if (choice == 8) {
                    printf("Entrez l'ID de la cible : ");
                    scanf("%d", &target_id);
                    getchar();
                    snprintf(message, BUFFER_SIZE, "BAN_CLIENT:%d", target_id);
                } else if (choice == 9) {
                    printf("Entrez l'ID de la cible : ");
                    scanf("%d", &target_id);
                    getchar();
                    snprintf(message, BUFFER_SIZE, "UNBAN_CLIENT:%d", target_id);
                } else if (choice == 10){
                    send(sock, "LIST_TOKENS", strlen("LIST_TOKENS"), 0);
                    memset(buffer, 0, BUFFER_SIZE);
                    read(sock, buffer, BUFFER_SIZE);
                    printf("%s\n", buffer);
                }

                send(sock, message, strlen(message), 0);
                memset(buffer, 0, BUFFER_SIZE);
                read(sock, buffer, BUFFER_SIZE);
                printf("%s\n", buffer);
            } else if (strcmp(user_type, "Pro") == 0 && ( choice == 6 || choice == 7 )) {
                int client_id;


                if (choice == 6) {
                    snprintf(message, BUFFER_SIZE, "BLOCK_CLIENT:%d", client_id);
                    printf("Entrez l'ID du client : ");
                    scanf("%d", &client_id);
                    getchar();
                } else if (choice == 7) {
                    printf("Entrez l'ID du client : ");
                    scanf("%d", &client_id);
                    getchar();
                    snprintf(message, BUFFER_SIZE, "UNBLOCK_CLIENT:%d", client_id);
                }

                send(sock, message, strlen(message), 0);
                memset(buffer, 0, BUFFER_SIZE);
                read(sock, buffer, BUFFER_SIZE);
                printf("%s\n", buffer);
            } else if (choice == 0) {
                printf("Déconnexion en cours...\n");
                send(sock, "LOGOUT", strlen("LOGOUT"), 0);
                is_logged_in = 0;
            } else if (choice == 20) {
                send(sock, "TOKEN", strlen("TOKEN"), 0);
                memset(buffer, 0, BUFFER_SIZE);
                read(sock, buffer, BUFFER_SIZE);
                printf("Info :\n%s\n", buffer);
            } else if (choice == 30) {
                send(sock, "CONFIG", strlen("CONFIG"), 0);
                memset(buffer, 0, BUFFER_SIZE);
                read(sock, buffer, BUFFER_SIZE);
                printf("Info :\n%s\n", buffer);
            } else {
                printf("Option invalide.\n");
            }
        }
    }

    close(sock);
    return 0;
}
