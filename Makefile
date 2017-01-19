CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread
SRC=src/main.c src/map.c src/server.c src/queue.c src/util.c src/commands.c src/persistence.c src/cluster.c src/networking.c src/messagequeue.c src/serializer.c src/hashing.h

shibui: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o shibui

shibui-cli: src/shibui-cli.c
	$(CC) $(CFLAGS) src/shibui-cli.c -o shibui-cli

clean:
	rm -f shibui
