#makefile for Lab6

CC=gcc
CGLAGS = -g

all: Server Client

Client: Client.c
	$(CC) $(CFLAGS) -o Client Client.c
	chmod a+x Client

Server: Server.c
	$(CC) $(CFLAGS) -o Server Server.c
	chmod a+x Server

clean:
	rm Client Server
