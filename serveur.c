#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <libpq-fe.h>


#define BUFFER_SIZE 1024


#define CONFIG_FILE "config.conf"

int PORT = 8080;
int ban_duration = 300; // en secondes
int max_messages = 100;
int max_message_size = 512;
int MAX_NAME_LENGTH = 256;

void read_config_file() {
    FILE *file = fopen(CONFIG_FILE, "r");
    if (file == NULL) {
        perror("Erreur d'ouverture du fichier de configuration");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Ignorer les commentaires
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        // Lire les paramètres
        if (strncmp(line, "PORT=", 5) == 0) {
            PORT = atoi(line + 5);
        } else if (strncmp(line, "ban_duration=", 13) == 0) {
            ban_duration = atoi(line + 13);
        } else if (strncmp(line, "max_messages=", 13) == 0) {
            max_messages = atoi(line + 13);
        } else if (strncmp(line, "max_message_size=", 17) == 0) {
            max_message_size = atoi(line + 17);
        } else if (strncmp(line, "MAX_NAME_LENGTH=", 16) == 0) {
            MAX_NAME_LENGTH = atoi(line + 16);
        }
    }

    fclose(file);
}

void print_config() {
    printf("Configuration actuelle:\n");
    printf("Port: %d\n", PORT);
    printf("Durée du ban: %d secondes\n", ban_duration);
    printf("Messages max: %d\n", max_messages);
    printf("Taille max des messages: %d octets\n", max_message_size);
    printf("Taille max des prenoms: %d octets\n", MAX_NAME_LENGTH);
}

int verify_api_key(PGconn *conn, const char *api_key, char *client_name, size_t name_size) {
    char query[512];
    snprintf(query, sizeof(query), "SELECT prenomCompte FROM _compte WHERE chat_cleApi = '%s';", api_key);

    PGresult *res = PQexec(conn, query);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Échec de la requête : %s\n", PQerrorMessage(conn));
        PQclear(res);
        return 0; // Clé API invalide ou erreur
    }

    int rows = PQntuples(res);
    if (rows == 1) {
        strncpy(client_name, PQgetvalue(res, 0, 0), name_size - 1); // Récupérer le nom de l'utilisateur
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
    int is_logged_in = 0;

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        // Recevoir une requête du client
        int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            printf("Client déconnecté.\n");
            break;
        }

        buffer[bytes_read] = '\0'; // Assurez-vous que le buffer est une chaîne C valide
        printf("Requête reçue : %s\n", buffer);

        if (!is_logged_in && strncmp(buffer, "LOGIN:", 6) == 0) {
            char *api_key = buffer + 6;
            if (verify_api_key(conn, api_key, client_name, MAX_NAME_LENGTH)) {
                is_logged_in = 1;
                snprintf(buffer, sizeof(buffer), "LOGIN OK:\n");
                send(client_socket, buffer, strlen(buffer), 0);
            } else {
                send(client_socket, "LOGIN FAILED\n", strlen("LOGIN FAILED\n"), 0);
            }
        } else if (strcmp(buffer, "PARAM") == 0) {
            snprintf(buffer, BUFFER_SIZE, "Port: %d\nDurée du ban: %d secondes\nMessages max: %d\nTaille max des messages: %d octets\nTaille max des prenoms: %d octets\n",
                     PORT, ban_duration, max_messages, max_message_size,MAX_NAME_LENGTH);
            send(client_socket, buffer, strlen(buffer), 0);
        }else if (strcmp(buffer, "NAME") == 0) {
            if (is_logged_in) {
                snprintf(buffer, BUFFER_SIZE, "Client connecté : %s\n", client_name);
            } else {
                snprintf(buffer, BUFFER_SIZE, "Non connecté\n");
            }
            send(client_socket, buffer, strlen(buffer), 0);
        } else if (strcmp(buffer, "LOGOUT") == 0) {
            if (!is_logged_in) {
                snprintf(buffer, BUFFER_SIZE, "Aucun compte connecté.\n");
            } else {
                snprintf(buffer, BUFFER_SIZE, "Déconnexion réussie.\n");
                memset(client_name, 0, MAX_NAME_LENGTH);
                is_logged_in = 0;
            }
            send(client_socket, buffer, strlen(buffer), 0);
        } else if (strncmp(buffer, "EXIT", 4) == 0) {
            printf("Commande EXIT reçue. Fermeture de la connexion avec le client.\n");
            break;
        } else {
            send(client_socket, "UNKNOWN COMMAND\n", strlen("UNKNOWN COMMAND\n"), 0);
        }
    }

    close(client_socket);
}

int main() {
    read_config_file();
    print_config();
    const char *conninfo = "host=localhost port=5432 dbname=postgres user=postgres password=180618";
    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connexion à la base de données échouée : %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return 1;
    }
    printf("Connexion à la base de données réussie !\n");

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
