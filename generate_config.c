#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Fonction pour supprimer les espaces en début et fin de chaîne
char *trim(char *str) {
    char *end;

    while (*str == ' ' || *str == '\t') str++; // Supprimer les espaces au début
    if (*str == 0) return str;

    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n')) end--; // Supprimer les espaces à la fin

    *(end + 1) = '\0';
    return str;
}

// Fonction pour lire le fichier .env et générer un fichier .c avec des constantes
int generate_config(const char *env_file, const char *output_file) {
    FILE *env = fopen(env_file, "r");
    if (!env) {
        perror("Erreur d'ouverture du fichier .env");
        return -1;
    }

    FILE *output = fopen(output_file, "w");
    if (!output) {
        perror("Erreur de création du fichier de configuration");
        fclose(env);
        return -1;
    }

    fprintf(output, "// Fichier généré automatiquement à partir de .env\n");
    fprintf(output, "// Ne modifiez pas ce fichier directement.\n\n");
    fprintf(output, "#ifndef CONFIG_CONSTANTS_H\n#define CONFIG_CONSTANTS_H\n\n");

    char line[256];
    while (fgets(line, sizeof(line), env)) {
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "=");

        if (!key || !value) continue;

        key = trim(key);
        value = trim(value);

        // Générer la constante en fonction du type de valeur (chaîne ou numérique)
        if (strchr(value, '.') || strchr(value, '-') || (value[0] >= '0' && value[0] <= '9')) {
            fprintf(output, "#define %s %s\n", key, value); // Numérique
        } else {
            fprintf(output, "#define %s \"%s\"\n", key, value); // Chaîne
        }
    }

    fprintf(output, "\n#endif // CONFIG_CONSTANTS_H\n");

    fclose(env);
    fclose(output);

    printf("Fichier de configuration généré : %s\n", output_file);
    return 0;
}

// Fonction principale
int main() {
    const char *env_file = "config.env";
    const char *output_file = "config_constants.h";

    if (generate_config(env_file, output_file) != 0) {
        return 1;
    }

    return 0;
}
