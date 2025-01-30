CC=gcc
CFLAGS=-I/usr/include/postgresql
LDFLAGS=-L/usr/lib/postgresql -lpq

serveur: serveur.c
	$(CC) $(CFLAGS) -o serveur serveur.c $(LDFLAGS)
