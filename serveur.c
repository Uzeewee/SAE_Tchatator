#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <libpq-fe.h>
#include <time.h>

#include "config.h"
#include <postgresql/libpq-fe.h>


// Structure pour stocker un token et sa date d'expiration
typedef struct {
    char token[TOKEN_LENGTH];
    time_t expiration_time;
    char client_name[MAX_NAME_LENGTH];
} SessionToken;

SessionToken active_tokens[MAX_TOKENS];

void list_tokens(PGconn *conn, int client_socket) {
    char query[] = "SELECT idChatTokenSession, idCompte, tokenSession, dateExpiration FROM _chat_tokensession;";
    PGresult *res = PQexec(conn, query);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Échec de la requête LIST_TOKENS : %s\n", PQerrorMessage(conn));
        send(client_socket, "Erreur lors de la récupération des tokens.\n", strlen("Erreur lors de la récupération des tokens.\n"), 0);
        PQclear(res);
        return;
    }

    char buffer[BUFFER_SIZE];
    int rows = PQntuples(res);

    if (rows == 0) {
        snprintf(buffer, BUFFER_SIZE, "Aucun token actif dans la base de données.\n");
        send(client_socket, buffer, strlen(buffer), 0);
    } else {
        snprintf(buffer, BUFFER_SIZE, "Liste des tokens actifs :\n");
        send(client_socket, buffer, strlen(buffer), 0);

        for (int i = 0; i < rows; i++) {
            snprintf(buffer, BUFFER_SIZE, "ID: %s | ID Compte: %s | Token: %s | Expiration: %s\n",
                     PQgetvalue(res, i, 0), PQgetvalue(res, i, 1),
                     PQgetvalue(res, i, 2), PQgetvalue(res, i, 3));
            send(client_socket, buffer, strlen(buffer), 0);
        }
    }

    PQclear(res);
}


void print_config() {
    printf("Configuration actuelle:\n");
    printf("Port: %d\n", PORT);
    printf("Durée du ban: %d secondes\n", ban_duration);
    printf("Messages max: %d\n", max_messages);
    printf("Taille max des messages: %d octets\n", max_message_size);
    printf("Taille max des prenoms: %d octets\n", MAX_NAME_LENGTH);
    printf("Taille des tokens: %d octets\n", TOKEN_LENGTH);
    printf("Durée d'expirations: %d secondes\n", TOKEN_EXPIRATION_TIME);
    printf("Nombre max de Token : %d \n", MAX_TOKENS);
}

void generate_token(char *token, size_t length) {
    const char *charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < length - 1; i++) {
        token[i] = charset[rand() % strlen(charset)];
    }
    token[length - 1] = '\0';
}

void add_token(PGconn *conn,const char *client_name, char *token, int client_id) {
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (active_tokens[i].token[0] == '\0') { // Si la place est libre
            strncpy(active_tokens[i].client_name, client_name, MAX_NAME_LENGTH);
            strcpy(active_tokens[i].token, token);
            active_tokens[i].expiration_time = time(NULL) + TOKEN_EXPIRATION_TIME;
            // Insérer dans la base de données
            char query[512];
            snprintf(query, sizeof(query),
                     "INSERT INTO public._chat_tokensession (idCompte, tokenSession, dateExpiration) "
                     "VALUES (%d, '%s', NOW() + INTERVAL '1 hour');",
                     client_id, token);

            PGresult *res = PQexec(conn, query);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "Erreur lors de l'insertion du token dans la base de données : %s\n", PQerrorMessage(conn));
            } else {
                printf("Token inséré dans la base de données pour l'utilisateur %s.\n", client_name);
            }
            PQclear(res);
            break;
        }
    }
}

int verify_token(const char *token, char *client_name) {
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (strcmp(active_tokens[i].token, token) == 0) {
            if (time(NULL) <= active_tokens[i].expiration_time) {
                strcpy(client_name, active_tokens[i].client_name);
                return 1; // Token valide
            } else {
                active_tokens[i].token[0] = '\0'; // Token expiré
                return 0; // Token expiré
            }
        }
    }
    return 0; // Token non trouvé
}

// Fonction pour écrire un log
void write_log(const char *client_name, const char *client_ip, const char *message) {
    FILE *log_file = fopen(LOG_FILE_PATH, "a");
    if (!log_file) {
        perror("Erreur lors de l'ouverture du fichier log");
        return;
    }

    // Récupérer la date et l'heure actuelle
    time_t now = time(NULL);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // Si le client_name est NULL, utiliser une chaîne vide
    if (!client_name) client_name = "";

    // Écrire dans le fichier log
    fprintf(log_file, "[%s] [%s] [%s] %s\n", time_str, client_name, client_ip, message);
    fclose(log_file);
}

int verify_api_key(PGconn *conn, const char *api_key, char *client_name, int *client_id ,size_t name_size) {
    char query[512];
    snprintf(query, sizeof(query), "SELECT idcompte, prenomCompte FROM _compte WHERE chat_cleApi = '%s';", api_key);

    PGresult *res = PQexec(conn, query);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Échec de la requête : %s\n", PQerrorMessage(conn));
        PQclear(res);
        return 0; // Clé API invalide ou erreur
    }

    int rows = PQntuples(res);
    if (rows == 1) {
        strncpy(client_name, PQgetvalue(res, 0, 1), name_size - 1); // Récupérer le nom de l'utilisateur
        *client_id = atoi(PQgetvalue(res, 0, 0));
        client_name[name_size - 1] = '\0'; // Assurer la terminaison
        PQclear(res);
        return 1; // Clé API valide
    } else {
        PQclear(res);
        return 0; // Clé API invalide
    }
}

void handle_client(int client_socket, PGconn *conn) {
    char buffer[BUFFER_SIZE];
    char client_name[MAX_NAME_LENGTH] = {};
    int client_id = -1 ;
    char token[TOKEN_LENGTH] = {};
    int is_logged_in = 0;

    // Récupérer l'adresse IP du client
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr *)&addr, &addr_len);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);

    // Log de la connexion
    write_log(NULL, client_ip, "Nouvelle connexion établie");

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        // Recevoir une requête du client
        int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            printf("Client déconnecté.\n");
            write_log(client_name[0] ? client_name : NULL, client_ip, "Client déconnecté");
            break;
        }

        buffer[bytes_read] = '\0'; // Assurez-vous que le buffer est une chaîne C valide
        printf("Requête reçue : %s\n", buffer);

        // Log de la requête
        char log_message[BUFFER_SIZE + 50];
        snprintf(log_message, sizeof(log_message), "Requête reçue : %s", buffer);
        write_log(client_name[0] ? client_name : NULL, client_ip, log_message);

        if (!is_logged_in && strncmp(buffer, "LOGIN:", 6) == 0) {
            char *api_key = buffer + 6;
            if (verify_api_key(conn, api_key, client_name, &client_id ,MAX_NAME_LENGTH)) {
                is_logged_in = 1;
                generate_token(token, TOKEN_LENGTH);
                add_token(conn,client_name, token,client_id); // Ajouter le token
                snprintf(buffer, sizeof(buffer), "Connecté en tant que :%s \nVotre TOKEN:%s\n",client_name,token);
                send(client_socket, buffer, strlen(buffer), 0);
                snprintf(log_message, sizeof(log_message), "Utilisateur connecté : %s, TOKEN : %s", client_name, token);
                write_log(client_name, client_ip, log_message);
            } else {
                send(client_socket, "LOGIN FAILED\n", strlen("LOGIN FAILED\n"), 0);
                write_log(NULL, client_ip, "Échec de la connexion : clé API invalide");
            }
        }else if (is_logged_in && strncmp(buffer, "MSG:", 4) == 0) {
            char *content = buffer + 4;
            
        } else if (strcmp(buffer, "CONFIG") == 0) {
            snprintf(buffer, BUFFER_SIZE, "Port: %d\nDurée du ban: %d secondes\nMessages max: %d\nTaille max des messages: %d octets\nTaille max des prenoms: %d octets\nTaille des tokens: %d octets\nDurée d'expirations: %d secondes\nNombre max de Token : %d \n",
                     PORT, ban_duration, max_messages, max_message_size,MAX_NAME_LENGTH,TOKEN_LENGTH,TOKEN_EXPIRATION_TIME,MAX_TOKENS);
            send(client_socket, buffer, strlen(buffer), 0);
        }else if (strcmp(buffer, "NAME") == 0) {
            if (is_logged_in) {
                snprintf(buffer, BUFFER_SIZE, "Client connecté : %s\n", client_name);
                write_log(client_name[0] ? client_name : NULL, client_ip, "Commande NAME sucess");
            } else {
                snprintf(buffer, BUFFER_SIZE, "Non connecté\n");
            }
            send(client_socket, buffer, strlen(buffer), 0);
        }else if (strcmp(buffer, "LIST_TOKENS") == 0) {
            list_tokens(conn, client_socket);
            write_log(client_name[0] ? client_name : NULL, client_ip, "Commande LIST_TOKENS sucess");
        }else if (strcmp(buffer, "LOGOUT") == 0) {
            if (!is_logged_in) {
                snprintf(buffer, BUFFER_SIZE, "Aucun compte connecté.\n");
            } else {
                snprintf(buffer, BUFFER_SIZE, "Déconnexion réussie.\n");
                write_log(client_name[0] ? client_name : NULL, client_ip, "Commande LOGOUT sucess");
                memset(client_name, 0, MAX_NAME_LENGTH);
                client_id = -1;
                is_logged_in = 0;
            }
            send(client_socket, buffer, strlen(buffer), 0);
        } else if (strncmp(buffer, "EXIT", 4) == 0) {
            printf("Commande EXIT reçue. Fermeture de la connexion avec le client.\n");
            write_log(client_name[0] ? client_name : NULL, client_ip, "Commande EXIT reçue");
            break;
        }else if (is_logged_in && strncmp(buffer, "TOKEN", 5) == 0) {
            if (verify_token(token, client_name)) {
                // Calculer le temps restant
                time_t current_time = time(NULL);
                time_t remaining_time = 0;
                for (int i = 0; i < MAX_TOKENS; i++) {
                    if (strcmp(active_tokens[i].token, token) == 0) {
                        remaining_time = active_tokens[i].expiration_time - current_time;
                        break;
                    }
                }

                if (remaining_time > 0) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Token valide pour l'utilisateur : %s\nToken actuel : %s\nTemps restant : %ld secondes\n", 
                            client_name, token, remaining_time);
                    write_log(client_name, client_ip, "Token validé avec succès");
                } else {
                    snprintf(buffer, BUFFER_SIZE, "Token expiré.\n");
                    write_log(client_name, client_ip, "Token expiré");
                }
            } else {
                snprintf(buffer, BUFFER_SIZE, "Token invalide ou expiré.\n");
                write_log(client_name, client_ip, "Token invalide ou expiré");
            }
            send(client_socket, buffer, strlen(buffer), 0);
        } else {
            send(client_socket, "UNKNOWN COMMAND\n", strlen("UNKNOWN COMMAND\n"), 0);
            snprintf(log_message, sizeof(log_message), "Commande inconnue : %s", buffer);
            write_log(client_name[0] ? client_name : NULL, client_ip, log_message);
        }
    }

    close(client_socket);
}

int main() {
    print_config();
    write_log(NULL, "localhost", "Démarrage du service");
    const char *conninfo = "host=localhost port=5432 dbname=postgres user=postgres password=180618";
    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connexion à la base de données échouée : %s\n", PQerrorMessage(conn));
        write_log(NULL, "localhost", "Connexion à la base de données échouée");
        PQfinish(conn);
        return 1;
    }
    printf("Connexion à la base de données réussie !\n");
    write_log(NULL, "localhost", "Connexion à la base de données réussie");


    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Erreur de création du socket");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Erreur de bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Erreur de listen");
        exit(EXIT_FAILURE);
    }

    printf("Serveur en attente de connexions sur le port %d...\n", PORT);

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("Erreur de accept");
            continue;
        }

        printf("Nouvelle connexion établie.\n");
        handle_client(client_socket, conn);
    }

    PQfinish(conn);
    return 0;
}
