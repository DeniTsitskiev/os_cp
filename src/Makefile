CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pthread
LIBS = -lzmq -lpthread

SOURCES_COMMON = func.c func.h
SOURCES_SERVER = server.c $(SOURCES_COMMON)
SOURCES_CLIENT = client.c $(SOURCES_COMMON)

TARGETS = server client

.PHONY: all clean install

all: $(TARGETS)

server: $(SOURCES_SERVER)
	$(CC) $(CFLAGS) -o $@ server.c func.c $(LIBS)

client: $(SOURCES_CLIENT)
	$(CC) $(CFLAGS) -o $@ client.c func.c $(LIBS)

clean:
	rm -f $(TARGETS) *.o

install: all
	mkdir -p bin
	cp $(TARGETS) bin/

help:
	@echo "Доступные команды:"
	@echo "  make all      - скомпилировать сервер и клиент"
	@echo "  make server   - скомпилировать только сервер"
	@echo "  make client   - скомпилировать только клиент"
	@echo "  make clean    - удалить скомпилированные файлы"
	@echo "  make install  - установить в папку bin/"
	@echo "  make help     - вывести эту справку"
