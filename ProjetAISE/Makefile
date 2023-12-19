CC = gcc -pthread
CFLAGS = -Wall -Wextra

all: server

server: server.o 
	$(CC) $(CFLAGS) -o server server.o 

server.o: server.c server.h
	$(CC) $(CFLAGS) -c server.c



clean:
	rm -f server *.o
