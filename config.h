#ifndef CONFIG_H
#define CONFIG_H

// Configuration générale
#define LOG_FILE_PATH "tchatator.log" // Chemin par défaut du fichier de log
#define PORT 8080
#define BUFFER_SIZE 1024
#define ban_duration  86400 // en secondes
#define max_messages  100
#define max_message_size  512
#define MAX_NAME_LENGTH 256
#define TOKEN_LENGTH  64
#define TOKEN_EXPIRATION_TIME  3600 // Durée de vie du token en secondes (1 heure)
#define MAX_TOKENS  100 // En secondes

#endif // CONFIG_H
