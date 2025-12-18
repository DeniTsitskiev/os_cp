CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread
LDFLAGS = -lzmq -lpthread

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

clean:
	rm -f server client

.PHONY: all clean
