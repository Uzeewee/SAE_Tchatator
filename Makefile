CC=gcc
CFLAGS=-I/usr/include/postgresql
LDFLAGS=-L/usr/lib/postgresql -lpq


.PHONY: all serveur client generate_config stress clean telnet  # Ajout de .PHONY pour forcer l'exécution


all: generate_config serveur client stress telnet

serveur: serveur.c
	$(CC) $(CFLAGS) -o serveur serveur.c $(LDFLAGS)

client: client.c
	$(CC) -o client client.c

stress: stress.c
	$(CC) -o stress stress.c

telnet: telnet.c
	$(CC) -o telnet telnet.c

generate_config: generate_config.c
	$(CC) -o generate_config generate_config.c
	./generate_config



clean:
	rm -f serveur client generate_config stress telnet