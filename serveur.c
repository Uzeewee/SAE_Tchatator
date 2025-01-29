#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <libpq-fe.h>
#include <getopt.h>
#include <stdbool.h>
#include <time.h>

#include "config_constants.h"
#include <postgresql/libpq-fe.h>



// Structure pour stocker un token et sa date d'expiration
typedef struct {
    char token[TOKEN_LENGTH];
    time_t expiration_time;
    char client_name[MAX_NAME_LENGTH];
} SessionToken;

SessionToken active_tokens[MAX_TOKENS];

void afficher_aide() {
    printf("Usage : ./serveur [OPTIONS]\n");
    printf("\nOptions disponibles :\n");
    printf("  --help                Affiche cette aide et quitte.\n");
    printf("\n");
    exit(0);
}

void execute_config_generator() {
    // Exécution du programme generate_config
    int result = system("./generate_config");
    if (result != 0) {
        fprintf(stderr, "Erreur : impossible de générer les configurations depuis .env.\n");
        exit(1);
    }
}

// Fonction pour gérer les options
void gerer_options(int argc, char *argv[]) {
    int opt;
    int option_index = 0;

    // Définition des options longues
    struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // Boucle pour traiter les options
    while ((opt = getopt_long(argc, argv, "h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h': // --help
                afficher_aide();
                break;

            default: // Option inconnue
                fprintf(stderr, "Option inconnue. Utilisez --help pour afficher l'aide.\n");
                exit(1);
        }
    }
}

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
    printf("Durée du ban: %d secondes\n", BAN_DURATION);
    printf("Messages max LOG: %d\n", MAX_MESSAGES);
    printf("Taille max des messages: %d octets\n", MAX_MESSAGE_SIZE);
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

int verify_api_key(PGconn *conn, const char *api_key, char *client_name, int *client_id ,size_t name_size,char *client_type) {
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
        // Déterminer le type d'utilisateur en fonction du préfixe de la clé API
        if (strncmp(api_key, "rwda-", 5) == 0) {
            strcpy(client_type, "Admin");
        } else if (strncmp(api_key, "rwd-", 4) == 0) {
            strcpy(client_type, "Pro");
        } else if (strncmp(api_key, "rw-", 3) == 0) {
            strcpy(client_type, "Membre");
        } else {
            strcpy(client_type, "inconnu");
        }
        PQclear(res);
        return 1; // Clé API valide
    } else {
        PQclear(res);
        return 0; // Clé API invalide
    }
}

int store_message(PGconn *conn, const char *emetteur, int id_emetteur, 
                  const char *destinataire, int id_destinataire, 
                  const char *direction, const char *content , int *last_id) {
    char query[2048];
    snprintf(query, sizeof(query),
             "INSERT INTO _chat_message (emetteur, id_emetteur, destinataire, id_destinataire, direction, content) "
             "VALUES ('%s', %d, '%s', %d, '%s', '%s') RETURNING idmessage;",
             emetteur, id_emetteur, destinataire, id_destinataire, direction, content);

    PGresult *res = PQexec(conn, query);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Erreur lors de l'insertion du message : %s\n", PQerrorMessage(conn));
        PQclear(res);
        return 0; // Échec
    }

    *last_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    return 1; // Succès
}


void handle_client(int client_socket, PGconn *conn) {


    char buffer[BUFFER_SIZE];
    char client_name[MAX_NAME_LENGTH] = {};
    char client_type[10] = {};
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

    char query_update[256];
    snprintf(query_update, sizeof(query_update), 
            "DELETE FROM _chat_tokensession;");

    PQexec(conn, query_update);

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
            if (verify_api_key(conn, api_key, client_name, &client_id ,MAX_NAME_LENGTH,client_type)) {
                // Vérifier si le client est banni
                char query_check[512];
                snprintf(query_check, sizeof(query_check), 
                        "SELECT COUNT(*) FROM _chat_ban WHERE client_id = %d;", 
                        client_id);

                PGresult *res_check = PQexec(conn, query_check);

                int is_banned = atoi(PQgetvalue(res_check, 0, 0));
                PQclear(res_check);

                if (is_banned == 1) {
                    snprintf(buffer, BUFFER_SIZE, "Impossible de vous connecter, vous êtes Banni.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                     write_log(client_name, client_ip, "Impossible de connecter le client car banni");
                    continue;
                }else{
                    is_logged_in = 1;
                    generate_token(token, TOKEN_LENGTH);
                    add_token(conn,client_name, token,client_id); // Ajouter le token
                    snprintf(buffer, sizeof(buffer), "%s\nConnecté en tant que :%s \nVotre TOKEN:%s\n",client_name,client_type,token);
                    send(client_socket, buffer, strlen(buffer), 0);
                    snprintf(log_message, sizeof(log_message), "Utilisateur connecté : %s %s, TOKEN : %s", client_name,client_type, token);
                    write_log(client_name, client_ip, log_message);
                }
            } else {
                send(client_socket, "LOGIN FAILED\n", strlen("LOGIN FAILED\n"), 0);
                write_log(NULL, client_ip, "Échec de la connexion : clé API invalide");
            }
        }else if(is_logged_in && strncmp(buffer, "MSG", 3) == 0){
            // Requête pour récupérer les messages destinés à l'utilisateur
            char query_select[512];
            snprintf(query_select, sizeof(query_select),
                    "SELECT idmessage, content, direction, id_emetteur, emetteur "
                    "FROM _chat_message "
                    "WHERE id_destinataire = %d AND direction = 'emis' AND est_supprime = false "
                    "ORDER BY timestamp_envoie ASC;", client_id);
            PGresult *res_select = PQexec(conn, query_select);

            if (PQresultStatus(res_select) == PGRES_TUPLES_OK) {
                int rows = PQntuples(res_select);
                if (rows == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Aucun message trouvé.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    char messages_buffer[10240] = "";
                    for (int i = 0; i < rows; i++) {
                        char ajout[] = {};
                        // Récupérer les informations du message
                        int id_message = atoi(PQgetvalue(res_select, i, 0));
                        char *contenu = PQgetvalue(res_select, i, 1);
                        char *direction = PQgetvalue(res_select, i, 2);
                        int id_emetteur = atoi(PQgetvalue(res_select, i, 3));
                        char *emetteur = PQgetvalue(res_select, i, 4);
                        
                        char temp[512];
                        snprintf(temp, sizeof(temp), "Message ID: %d De %s (ID: %d): %s\n",
                            id_message, emetteur, id_emetteur, contenu);
                        strcat(messages_buffer, temp);

                        // Construire la requête SQL d'UPDATE
                        char query_update[256];
                        snprintf(query_update, sizeof(query_update),
                                "UPDATE _chat_message SET direction = 'recu' WHERE idmessage = %d;", id_message);

                        // Exécuter la requête
                        PGresult *res_update = PQexec(conn, query_update);
                        PQclear(res_update);
                    }
                    send(client_socket, messages_buffer, strlen(messages_buffer), 0);
                }
                write_log(client_name, client_ip, "Requete de MSG reussi");
            }else {
                snprintf(buffer, BUFFER_SIZE, "Erreur lors de la récupération des messages.\n");
                send(client_socket, buffer, strlen(buffer), 0);

            }
            PQclear(res_select);

        }else if (is_logged_in && strncmp(buffer, "NEW_MSG:", 8) == 0) {
            char message[MAX_MESSAGE_LENGTH];
            char *ptr;
            int id_destinataire;

            // Vérification du token
            if (!verify_token(token, client_name)) {
                snprintf(buffer, BUFFER_SIZE, "Votre token a expiré ou est invalide. Veuillez vous reconnecter.\n");
                send(client_socket, buffer, strlen(buffer), 0);
                continue;
            }

            // Extraire les informations du message
            ptr = buffer + 8;
            sscanf(ptr, "%d,", &id_destinataire);

            // Extraire le message
            ptr = strchr(ptr, ',') + 1; // Aller après <id_destinataire>
            strncpy(message, ptr, sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0'; // Assurer la terminaison du message

            // Vérifier que la taille du message ne dépasse pas 1000 caractères
            if (strlen(message) > 1000) {
                snprintf(buffer, BUFFER_SIZE, "Erreur : le message est trop long, il ne doit pas dépasser 1000 caractères.\n");
                send(client_socket, buffer, strlen(buffer), 0);
                write_log(client_name, client_ip, "Message trop Long");
                continue;
            }

            // Obtenir les informations de l'émetteur (déjà connecté)
            int id_emetteur = client_id; // Supposons que client_id est défini pour l'utilisateur connecté
            char emetteur[MAX_NAME_LENGTH] = {0};
            strncpy(emetteur, client_name, MAX_NAME_LENGTH - 1);

            // Obtenir le nom et le rôle du destinataire
            char destinataire[MAX_NAME_LENGTH] = {0};
            char dest_api[512]={}; 
            char dest_type[10]={};// Type du destinataire (ex : "rw", "rwd")
            char query[512];
            snprintf(query, sizeof(query), "SELECT prenomCompte, chat_cleApi FROM _compte WHERE idCompte = %d;", id_destinataire);
            PGresult *res = PQexec(conn, query);

            if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1) {
                // Récupérer le nom du destinataire
                strncpy(destinataire, PQgetvalue(res, 0, 0), MAX_NAME_LENGTH - 1);
                destinataire[MAX_NAME_LENGTH - 1] = '\0'; // Assurer la terminaison

                // Récupérer le rôle du destinataire
                strncpy(dest_api, PQgetvalue(res, 0, 1), sizeof(dest_api) - 1);
                dest_api[sizeof(dest_api) - 1] = '\0'; // Assurer la terminaison
                if (strncmp(dest_api, "rwda-", 5) == 0) {
                    strcpy(dest_type, "Admin");
                } else if (strncmp(dest_api, "rwd-", 4) == 0) {
                    strcpy(dest_type, "Pro");
                } else if (strncmp(dest_api, "rw-", 3) == 0) {
                    strcpy(dest_type, "Membre");
                } else {
                    strcpy(dest_type, "inconnu");
                }
            } else {
                // Destinataire introuvable
                snprintf(buffer, BUFFER_SIZE, "Erreur : destinataire introuvable.\n");
                write_log(client_name, client_ip, "destinataire du message introuvable");
                send(client_socket, buffer, strlen(buffer), 0);
                PQclear(res);
                continue;
            }
            PQclear(res);

            // Vérifier les règles de communication entre rôles
            if ((strcmp(client_type, "Pro") == 0 && strcmp(dest_type, "Pro") == 0) || 
                (strcmp(client_type, "Membre") == 0 && strcmp(dest_type, "Membre") == 0)) {
                snprintf(buffer, BUFFER_SIZE, "Erreur : les membres ne peuvent pas discuter entre eux, ni les professionnels entre eux.\n");
                write_log(client_name, client_ip, "discussions impossible");
                send(client_socket, buffer, strlen(buffer), 0);
                continue;
            }


            char query_block_check_1[512];
            snprintf(query_block_check_1, sizeof(query_block_check_1),
            "SELECT COUNT(*) FROM _chat_bloque WHERE client_id = %d AND blocked_by = %d;",
            id_emetteur, id_destinataire);


            PGresult *res_block_check_1 = PQexec(conn, query_block_check_1);

            if (PQresultStatus(res_block_check_1) != PGRES_TUPLES_OK) {
                fprintf(stderr, "Erreur lors de la vérification du blocage existant : %s\n", PQerrorMessage(conn));
                snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du blocage existant.\n");
                send(client_socket, buffer, strlen(buffer), 0);
                PQclear(res_block_check_1);
                continue;
            }

            int is_already_blocked_1 = atoi(PQgetvalue(res_block_check_1, 0, 0));
            PQclear(res_block_check_1);

            if (is_already_blocked_1 > 0) {
                snprintf(buffer, BUFFER_SIZE, 
                        "Le client ID %d vous a bloqué.\n", id_destinataire);
                send(client_socket, buffer, strlen(buffer), 0);
                write_log(client_name, client_ip, "Le client ne peux pas envoyer de message car Banni");
                continue;
            }

            char query_block_check_2[512];
            snprintf(query_block_check_2, sizeof(query_block_check_2),
            "SELECT COUNT(*) FROM _chat_bloque WHERE client_id = %d AND blocked_by = %d;",
            id_destinataire, id_emetteur);


            PGresult *res_block_check_2 = PQexec(conn, query_block_check_2);

            if (PQresultStatus(res_block_check_2) != PGRES_TUPLES_OK) {
                fprintf(stderr, "Erreur lors de la vérification du blocage existant : %s\n", PQerrorMessage(conn));
                snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du blocage existant.\n");
                send(client_socket, buffer, strlen(buffer), 0);
                PQclear(res_block_check_2);
                continue;
            }

            int is_already_blocked_2 = atoi(PQgetvalue(res_block_check_2, 0, 0));
            PQclear(res_block_check_2);

            if (is_already_blocked_2 > 0) {
                snprintf(buffer, BUFFER_SIZE, 
                        "Vous avez bloqué ce client id : %d\n", id_destinataire);
                send(client_socket, buffer, strlen(buffer), 0);
                write_log(client_name, client_ip, "Le client a été bloqué");
                continue;
            }



            int id ;
            // Enregistrer le message dans la base de données
            if (store_message(conn, emetteur, id_emetteur, destinataire, id_destinataire, "emis", message,&id)) {

                #pragma GCC diagnostic ignored "-Wformat-truncation"
                snprintf(buffer, BUFFER_SIZE, "id:%d Message envoyé à %s : %s\n",id, destinataire, message);
                write_log(client_name, client_ip, "Requete de MSG bien Envoyé");
            } else {
                snprintf(buffer, BUFFER_SIZE, "Échec de l'envoi du message.\n");
                write_log(client_name, client_ip, "Echec de l'envoie");
            }

            send(client_socket, buffer, strlen(buffer), 0);
        } else if (strcmp(buffer, "CONFIG") == 0) {
            snprintf(buffer, BUFFER_SIZE, "Port: %d\nDurée du ban: %d secondes\nMessages max: %d\nTaille max des messages: %d octets\nTaille max des prenoms: %d octets\nTaille des tokens: %d octets\nDurée d'expirations: %d secondes\nNombre max de Token : %d \n",
                     PORT, BAN_DURATION, MAX_MESSAGES, MAX_MESSAGE_SIZE ,MAX_NAME_LENGTH,TOKEN_LENGTH,TOKEN_EXPIRATION_TIME,MAX_TOKENS);
            send(client_socket, buffer, strlen(buffer), 0);
        } else if (is_logged_in && strncmp(buffer, "LOG:",4) == 0) {

            char query_select[512];
            char response[10000] = {0};
            int id_reference = -1;
            char *ptr = buffer + 4;

            // Extraire l'ID de référence s'il est fourni
            if (sscanf(ptr, "%d", &id_reference) != 1) {
                id_reference = -1; // Aucun ID de référence fourni
            }
            
            if (id_reference == -1)
            {
                snprintf(query_select, sizeof(query_select),
                    "SELECT idmessage, emetteur, content, direction, timestamp_envoie "
                    "FROM _chat_message "
                    "WHERE id_destinataire = %d AND est_supprime = false "
                    "ORDER BY timestamp_envoie ASC "
                    "LIMIT %d;", client_id,MAX_MESSAGES);
            } else {
                snprintf(query_select, sizeof(query_select),
                    "SELECT idmessage, emetteur, content, direction, timestamp_envoie "
                    "FROM _chat_message "
                    "WHERE id_destinataire = %d AND est_supprime = false "
                    "AND idmessage < %d "
                    "ORDER BY timestamp_envoie ASC "
                    "LIMIT %d;", client_id,id_reference,MAX_MESSAGES);
            }
            
            
            PGresult *res = PQexec(conn, query_select);

            if (PQresultStatus(res) == PGRES_TUPLES_OK) {
                int num_rows = PQntuples(res);

                if (num_rows == 0) {
                    // Aucun message trouvé
                    snprintf(response, sizeof(response), "Aucun message trouvé.\n");
                } else {
                    // Construire la réponse avec les messages
                    for (int i = 0; i < num_rows; i++) {
                        int id_message = atoi(PQgetvalue(res, i, 0));
                        char *emetteur = PQgetvalue(res, i, 1);
                        char *contenu = PQgetvalue(res, i, 2);
                        char *direction = PQgetvalue(res, i, 3);
                        char *date_message = PQgetvalue(res, i, 4);

                        // Ajouter chaque message au buffer de réponse
                        char message_line[256];
                        snprintf(message_line, sizeof(message_line),
                                "ID: %d, Émetteur: %s, Date: %s, Contenu: %s\n",
                                id_message, emetteur, date_message, contenu);

                        // Vérifier la taille du buffer avant d'ajouter
                        if (strlen(response) + strlen(message_line) < sizeof(response)) {
                            strcat(response, message_line);
                        } else {
                            // Si le buffer est plein, interrompre la construction
                            strcat(response, "Résultat tronqué. Trop de messages.\n");
                            break;
                        }
                    }
                     write_log(client_name, client_ip, "Requete LOG");
                }
            } else {
                // Gestion des erreurs SQL
                snprintf(response, sizeof(response), "Erreur lors de la récupération des messages : %s\n", PQerrorMessage(conn));
            }

            // Envoyer la réponse au client
            send(client_socket, response, strlen(response), 0);

            // Libérer les ressources
            PQclear(res);

        }else if (is_logged_in && strncmp(buffer,"MODIF_MSG:",10)==0) {

            char message_mod[MAX_MESSAGE_LENGTH];
            char *ptr;
            int id_message;

            // Vérification du token
            if (!verify_token(token, client_name)) {
                snprintf(buffer, BUFFER_SIZE, "Votre token a expiré ou est invalide. Veuillez vous reconnecter.\n");
                send(client_socket, buffer, strlen(buffer), 0);
                continue;
            }

            

            // Extraire les informations du message
            ptr = buffer + 10;
            sscanf(ptr, "%d,%1023[^\n]", &id_message, message_mod) != 2;
            


            // Requête pour vérifier les informations du message
            char query[512];
            snprintf(query, sizeof(query), 
                    "SELECT id_emetteur, est_supprime, direction "
                    "FROM _chat_message WHERE idmessage = %d;", id_message);

            PGresult *res = PQexec(conn, query);

            if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1) {
                int id_emetteur = atoi(PQgetvalue(res, 0, 0));
                char *est_supprime = PQgetvalue(res, 0, 1);
                char *direction = PQgetvalue(res, 0, 2);

                // Vérification des conditions
                if (id_emetteur != client_id) {
                    snprintf(buffer, BUFFER_SIZE, "Erreur : Vous n'êtes pas l'émetteur de ce message.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else if (strcmp(est_supprime, "t") == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Erreur : Ce message a déjà été supprimé.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else if (strcmp(direction, "recu") == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Erreur : Le message a deja été reçu.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    // Marquer le message comme supprimé
                    char query_update[256];
                    snprintf(query_update, sizeof(query_update), 
                            "UPDATE _chat_message SET content = '%s' WHERE idmessage = %d;",message_mod, id_message);

                    PGresult *res_update = PQexec(conn, query_update);

                    if (PQresultStatus(res_update) == PGRES_COMMAND_OK) {
                        snprintf(buffer, BUFFER_SIZE, "Message ID %d a été modifié avec succès.\n", id_message);
                         write_log(client_name, client_ip, "message modifier");
                        send(client_socket, buffer, strlen(buffer), 0);
                    } else {
                        snprintf(buffer, BUFFER_SIZE, "Erreur : Échec de la modification du message.%s\n",PQerrorMessage(conn));
                         write_log(client_name, client_ip, "Echec de la modification du message");
                        send(client_socket, buffer, strlen(buffer), 0);
                    }

                    PQclear(res_update);
                }
            } else {
                snprintf(buffer, BUFFER_SIZE, "Erreur : Message introuvable.\n");
                send(client_socket, buffer, strlen(buffer), 0);
            }

            PQclear(res);


        }else if(is_logged_in && strncmp(buffer,"BLOCK_CLIENT:",13)==0){
            char *ptr;
            int client_to_block;
            int target_pro_id; // Par défaut, aucun professionnel ciblé (utile pour admin)


            if(strcmp(client_type, "Membre") == 0){
                snprintf(buffer, BUFFER_SIZE, 
                        "Vous ne pouvez pas executer cette commande en tant que membre");
                send(client_socket, buffer, strlen(buffer), 0);

            }else if (strcmp(client_type, "Admin") == 0) {

                // Mode administrateur

                ptr = buffer + 13; // Décalage après "BLOCK_CLIENT:"
                if (sscanf(ptr, "%d,%d", &client_to_block, &target_pro_id) != 2) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Format de la commande invalide. Utilisation : BLOCK_CLIENT:<client_id>,<pro_id>\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                //Vérification que le client existe et qu'il est un membre actif avec une clé API valide
                char query_check[512];
                snprintf(query_check, sizeof(query_check), 
                        "SELECT COUNT(*) FROM _compte WHERE idCompte = %d AND chat_cleApi LIKE 'rw-%%';", client_to_block);

                PGresult *res_check = PQexec(conn, query_check);

                if (PQresultStatus(res_check) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du client : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du client.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_check);
                    continue;
                }

                // Vérifier si le client existe et est valide
                int client_exists = atoi(PQgetvalue(res_check, 0, 0));
                PQclear(res_check);

                if (client_exists == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d n'existe pas ou n'est pas un membre valide.\n", client_to_block);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                //Vérification que le pro existe et qu'il est un membre actif avec une clé API valide
                char query_check_pro[512];
                snprintf(query_check_pro, sizeof(query_check_pro), 
                        "SELECT COUNT(*) FROM _compte WHERE idCompte = %d AND chat_cleApi LIKE 'rwd-%%';", target_pro_id);

                PGresult *res_check_pro = PQexec(conn, query_check_pro);

                if (PQresultStatus(res_check_pro) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du client : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du client.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_check_pro);
                    continue;
                }

                // Vérifier si le Pro existe et est valide
                int pro_exists = atoi(PQgetvalue(res_check_pro, 0, 0));
                PQclear(res_check_pro);

                if (pro_exists == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Le pro ID %d n'existe pas ou n'est pas un pro valide.\n", target_pro_id);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                char query_block_check[512];
                snprintf(query_block_check, sizeof(query_block_check),
                 "SELECT COUNT(*) FROM _chat_bloque WHERE client_id = %d AND blocked_by = %d;",
                 client_to_block, target_pro_id);


                PGresult *res_block_check = PQexec(conn, query_block_check);

                if (PQresultStatus(res_block_check) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du blocage existant : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du blocage existant.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_block_check);
                    continue;
                }

                int is_already_blocked = atoi(PQgetvalue(res_block_check, 0, 0));
                PQclear(res_block_check);

                if (is_already_blocked > 0) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Le client ID %d est déjà bloqué par vous.\n", client_to_block);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Requête pour bloquer un client pour un professionnel spécifique
                char query[512];
                snprintf(query, sizeof(query),
                        "INSERT INTO _chat_bloque (client_id, blocked_by, is_admin) "
                        "VALUES (%d, %d, %d);",
                        client_to_block, target_pro_id, client_id);

                PGresult *res = PQexec(conn, query);

                if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                    fprintf(stderr, "Erreur lors du blocage du client (mode admin) : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur lors du blocage du client en mode administrateur.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Le client ID %d a été bloqué pour communiquer avec le professionnel ID %d pendant 24 heures.\n", 
                            client_to_block, target_pro_id);
                    send(client_socket, buffer, strlen(buffer), 0);
                    write_log(client_name, client_ip, "CLient Bloque par l'admin");
                }

                PQclear(res);
            } else if (strcmp(client_type, "Pro") == 0) {

                ptr = buffer + 13; // Décalage après "BLOCK_CLIENT:"
                if (sscanf(ptr, "%d", &client_to_block )!=1) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Format de la commande invalide. Utilisation : BLOCK_CLIENT:<client_id>\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                //Vérification que le client existe et qu'il est un membre actif avec une clé API valide
                char query_check[512];
                snprintf(query_check, sizeof(query_check), 
                        "SELECT COUNT(*) FROM _compte WHERE idCompte = %d AND chat_cleApi LIKE 'rw-%%';", client_to_block);

                PGresult *res_check = PQexec(conn, query_check);

                if (PQresultStatus(res_check) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du client : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du client.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_check);
                    continue;
                }

                // Vérifier si le client existe et est valide
                int client_exists = atoi(PQgetvalue(res_check, 0, 0));
                PQclear(res_check);

                if (client_exists == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d n'existe pas ou n'est pas un membre valide.\n", client_to_block);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                char query_block_check[512];
                snprintf(query_block_check, sizeof(query_block_check),
                 "SELECT COUNT(*) FROM _chat_bloque WHERE client_id = %d AND blocked_by = %d;",
                 client_to_block, client_id);


                PGresult *res_block_check = PQexec(conn, query_block_check);

                if (PQresultStatus(res_block_check) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du blocage existant : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du blocage existant.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_block_check);
                    continue;
                }

                int is_already_blocked = atoi(PQgetvalue(res_block_check, 0, 0));
                PQclear(res_block_check);

                if (is_already_blocked > 0) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Le client ID %d est déjà bloqué par vous.\n", client_to_block);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Mode professionnel
                char query[512];
                snprintf(query, sizeof(query),
                        "INSERT INTO _chat_bloque (client_id, blocked_by, is_admin) "
                        "VALUES (%d, %d, -1 );",
                        client_to_block, client_id);

                PGresult *res = PQexec(conn, query);

                if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                    fprintf(stderr, "Erreur lors du blocage du client (mode pro) : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur lors du blocage du client en mode professionnel.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Le client ID %d a été bloqué pour vous envoyer des messages pendant 24 heures.\n", 
                            client_to_block);
                    send(client_socket, buffer, strlen(buffer), 0);
                    write_log(client_name, client_ip, "CLient Bloque par le pro");
                }

                PQclear(res);
            }

        }else if (strncmp(buffer, "UNBLOCK_CLIENT:", 15) == 0) {
            char *ptr;
            int client_to_unblock;
            int target_pro_id; // Par défaut, aucun professionnel ciblé (utile pour admin)

            if (strcmp(client_type, "Membre") == 0) {
                snprintf(buffer, BUFFER_SIZE, 
                        "Vous ne pouvez pas exécuter cette commande en tant que membre.");
                send(client_socket, buffer, strlen(buffer), 0);

            } else if (strcmp(client_type, "Admin") == 0) {
                // Mode administrateur
                ptr = buffer + 15; // Décalage après "UNBLOCK_CLIENT:"
                if (sscanf(ptr, "%d,%d", &client_to_unblock, &target_pro_id) != 2) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Format de la commande invalide. Utilisation : UNBLOCK_CLIENT:<client_id>,<pro_id>\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Vérifier si le blocage existe
                char query_check[512];
                snprintf(query_check, sizeof(query_check), 
                        "SELECT COUNT(*) FROM _chat_bloque WHERE client_id = %d AND blocked_by = %d;", 
                        client_to_unblock, target_pro_id);

                PGresult *res_check = PQexec(conn, query_check);

                if (PQresultStatus(res_check) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du blocage : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du blocage.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_check);
                    continue;
                }

                int block_exists = atoi(PQgetvalue(res_check, 0, 0));
                PQclear(res_check);

                if (block_exists == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d n'est pas bloqué par le pro ID %d.\n", 
                            client_to_unblock, target_pro_id);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Supprimer le blocage
                char query_delete[512];
                snprintf(query_delete, sizeof(query_delete), 
                        "DELETE FROM _chat_bloque WHERE client_id = %d AND blocked_by = %d;", 
                        client_to_unblock, target_pro_id);

                PGresult *res_delete = PQexec(conn, query_delete);

                if (PQresultStatus(res_delete) != PGRES_COMMAND_OK) {
                    fprintf(stderr, "Erreur lors de la suppression du blocage : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur lors de la suppression du blocage.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d a été débloqué par le pro ID %d.\n", 
                            client_to_unblock, target_pro_id);
                    send(client_socket, buffer, strlen(buffer), 0);
                    write_log(client_name, client_ip, "CLient a ete debloque par l'admin");
                }

                PQclear(res_delete);

            } else if (strcmp(client_type, "Pro") == 0) {
                // Mode professionnel
                ptr = buffer + 15; // Décalage après "UNBLOCK_CLIENT:"
                if (sscanf(ptr, "%d", &client_to_unblock) != 1) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Format de la commande invalide. Utilisation : UNBLOCK_CLIENT:<client_id>\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Vérifier si le blocage existe
                char query_check[512];
                snprintf(query_check, sizeof(query_check), 
                        "SELECT COUNT(*) FROM _chat_bloque WHERE client_id = %d AND blocked_by = %d;", 
                        client_to_unblock, client_id);

                PGresult *res_check = PQexec(conn, query_check);

                if (PQresultStatus(res_check) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du blocage : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du blocage.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_check);
                    continue;
                }

                int block_exists = atoi(PQgetvalue(res_check, 0, 0));
                PQclear(res_check);

                if (block_exists == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d n'est pas bloqué par vous.\n", 
                            client_to_unblock);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Supprimer le blocage
                char query_delete[512];
                snprintf(query_delete, sizeof(query_delete), 
                        "DELETE FROM _chat_bloque WHERE client_id = %d AND blocked_by = %d;", 
                        client_to_unblock, client_id);

                PGresult *res_delete = PQexec(conn, query_delete);

                if (PQresultStatus(res_delete) != PGRES_COMMAND_OK) {
                    fprintf(stderr, "Erreur lors de la suppression du blocage : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur lors de la suppression du blocage.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d a été débloqué avec succès.\n", 
                            client_to_unblock);
                    send(client_socket, buffer, strlen(buffer), 0);
                    write_log(client_name, client_ip, "CLient a ete debloque par le pro");
                }

                PQclear(res_delete);
            }
        }else if (strncmp(buffer, "BAN_CLIENT:", 11) == 0) {
            char *ptr;
            int client_to_ban;

            if (strcmp(client_type, "Admin") != 0) {
                snprintf(buffer, BUFFER_SIZE, "Seul un administrateur peut utiliser cette commande.\n");
                send(client_socket, buffer, strlen(buffer), 0);
            } else {
                ptr = buffer + 11; // Décalage après "BAN_CLIENT:"
                if (sscanf(ptr, "%d", &client_to_ban) != 1) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Format de la commande invalide. Utilisation : BAN_CLIENT:<client_id>\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Vérifier si le client existe et est valide
                char query_check[512];
                snprintf(query_check, sizeof(query_check), 
                        "SELECT COUNT(*) FROM _compte WHERE idCompte = %d AND (chat_cleApi LIKE 'rw-%%' OR chat_cleApi LIKE 'rwd-%%');", 
                        client_to_ban);

                PGresult *res_check = PQexec(conn, query_check);

                if (PQresultStatus(res_check) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du client : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du client.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_check);
                    continue;
                }

                int client_exists = atoi(PQgetvalue(res_check, 0, 0));
                PQclear(res_check);

                if (client_exists == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d n'existe pas ou n'est pas un membre valide.\n", 
                            client_to_ban);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Vérifier si le client est déjà banni
                char query_check_ban[512];
                snprintf(query_check_ban, sizeof(query_check_ban), 
                        "SELECT COUNT(*) FROM _chat_ban WHERE client_id = %d;", 
                        client_to_ban);

                PGresult *res_check_ban = PQexec(conn, query_check_ban);

                if (PQresultStatus(res_check_ban) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du bannissement : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du bannissement.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_check_ban);
                    continue;
                }

                int is_banned = atoi(PQgetvalue(res_check_ban, 0, 0));
                PQclear(res_check_ban);

                if (is_banned > 0) {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d est déjà banni.\n", client_to_ban);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Ajouter un bannissement
                char query_ban[512];
                snprintf(query_ban, sizeof(query_ban), 
                        "INSERT INTO _chat_ban (client_id, blocked_by) VALUES (%d, %d);", 
                        client_to_ban, client_id);

                PGresult *res_ban = PQexec(conn, query_ban);

                if (PQresultStatus(res_ban) != PGRES_COMMAND_OK) {
                    fprintf(stderr, "Erreur lors du bannissement du client : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur lors du bannissement du client.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Le client ID %d a été banni avec succès.\n", 
                            client_to_ban);
                    send(client_socket, buffer, strlen(buffer), 0);
                    write_log(client_name, client_ip, "Client banni par l'admin");
                }

                PQclear(res_ban);
            }
        }else if (strncmp(buffer, "UNBAN_CLIENT:", 13) == 0) {
            char *ptr;
            int client_to_unban;

            if (strcmp(client_type, "Admin") != 0) {
                snprintf(buffer, BUFFER_SIZE, "Seul un administrateur peut utiliser cette commande.\n");
                send(client_socket, buffer, strlen(buffer), 0);
            } else {
                ptr = buffer + 13; // Décalage après "UNBAN_CLIENT:"
                if (sscanf(ptr, "%d", &client_to_unban) != 1) {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Format de la commande invalide. Utilisation : UNBAN_CLIENT:<client_id>\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Vérifier si le client est banni
                char query_check[512];
                snprintf(query_check, sizeof(query_check), 
                        "SELECT COUNT(*) FROM _chat_ban WHERE client_id = %d;", 
                        client_to_unban);

                PGresult *res_check = PQexec(conn, query_check);

                if (PQresultStatus(res_check) != PGRES_TUPLES_OK) {
                    fprintf(stderr, "Erreur lors de la vérification du bannissement : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur interne lors de la vérification du bannissement.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                    PQclear(res_check);
                    continue;
                }

                int is_banned = atoi(PQgetvalue(res_check, 0, 0));
                PQclear(res_check);

                if (is_banned == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Le client ID %d n'est pas banni.\n", 
                            client_to_unban);
                    send(client_socket, buffer, strlen(buffer), 0);
                    continue;
                }

                // Supprimer le bannissement
                char query_unban[512];
                snprintf(query_unban, sizeof(query_unban), 
                        "DELETE FROM _chat_ban WHERE client_id = %d;", 
                        client_to_unban);

                PGresult *res_unban = PQexec(conn, query_unban);

                if (PQresultStatus(res_unban) != PGRES_COMMAND_OK) {
                    fprintf(stderr, "Erreur lors de la suppression du bannissement : %s\n", PQerrorMessage(conn));
                    snprintf(buffer, BUFFER_SIZE, "Erreur lors de la suppression du bannissement.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    snprintf(buffer, BUFFER_SIZE, 
                            "Le client ID %d a été débanni avec succès.\n", 
                            client_to_unban);
                    send(client_socket, buffer, strlen(buffer), 0);
                    write_log(client_name, client_ip, "CLient debanni par l'admin");
                }

                PQclear(res_unban);
            }
        }else if (is_logged_in && strncmp(buffer,"REMOVE_MSG:",11)==0){
            char *ptr;
            int id_suppr;

            // Extraire l'identifiant du message
            ptr = buffer + 11;
            sscanf(ptr, "%d", &id_suppr);

            // Requête pour vérifier les informations du message
            char query[512];
            snprintf(query, sizeof(query), 
                    "SELECT id_emetteur, est_supprime, direction "
                    "FROM _chat_message WHERE idmessage = %d;", id_suppr);

            PGresult *res = PQexec(conn, query);

            if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1) {
                int id_emetteur = atoi(PQgetvalue(res, 0, 0));
                char *est_supprime = PQgetvalue(res, 0, 1);
                char *direction = PQgetvalue(res, 0, 2);

                // Vérification des conditions
                if (id_emetteur != client_id) {
                    snprintf(buffer, BUFFER_SIZE, "Erreur : Vous n'êtes pas l'émetteur de ce message.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else if (strcmp(est_supprime, "t") == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Erreur : Ce message a déjà été supprimé.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else if (strcmp(direction, "recu") == 0) {
                    snprintf(buffer, BUFFER_SIZE, "Erreur : Le message a deja été reçu.\n");
                    send(client_socket, buffer, strlen(buffer), 0);
                } else {
                    // Marquer le message comme supprimé
                    char query_update[256];
                    snprintf(query_update, sizeof(query_update), 
                            "UPDATE _chat_message SET est_supprime = true WHERE idmessage = %d;", id_suppr);

                    PGresult *res_update = PQexec(conn, query_update);

                    if (PQresultStatus(res_update) == PGRES_COMMAND_OK) {
                        snprintf(buffer, BUFFER_SIZE, "Message ID %d a été supprimé avec succès.\nle destinataire ne recevra pas le message.\n", id_suppr);
                        send(client_socket, buffer, strlen(buffer), 0);
                         write_log(client_name, client_ip, "Message bien supprimé");
                    } else {
                        snprintf(buffer, BUFFER_SIZE, "Erreur : Échec de la suppression du message.\n");
                         write_log(client_name, client_ip, "Echec de la suppression du message");
                        send(client_socket, buffer, strlen(buffer), 0);
                    }

                    PQclear(res_update);
                }
            } else {
                snprintf(buffer, BUFFER_SIZE, "Erreur : Message introuvable.\n");
                send(client_socket, buffer, strlen(buffer), 0);
            }

            PQclear(res);



        }else if (strcmp(buffer, "NAME") == 0) {
            if (is_logged_in) {
                snprintf(buffer, BUFFER_SIZE, "%s connecté : %s\n",client_type, client_name);
                write_log(client_name[0] ? client_name : NULL, client_ip, "Commande NAME sucess");
            } else {
                snprintf(buffer, BUFFER_SIZE, "Non connecté\n");
            }
            send(client_socket, buffer, strlen(buffer), 0);
        }else if (strcmp(buffer, "LIST_TOKENS") == 0) {
            // Vérifier si le client est un admin
            if (strcmp(client_type, "Admin") != 0) {
                snprintf(buffer, BUFFER_SIZE, "Erreur : seul un administrateur peut accéder à la liste de tokens.\n");
                send(client_socket, buffer, strlen(buffer), 0);
                write_log(client_name[0] ? client_name : NULL, client_ip, "Commande LIST_TOKENS echec");
            }else{
                list_tokens(conn, client_socket);
                write_log(client_name[0] ? client_name : NULL, client_ip, "Commande LIST_TOKENS sucess");
            }
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

int main(int argc, char *argv[]) {
    // Appeler la fonction pour gérer les options
    gerer_options(argc, argv);

    execute_config_generator();

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
