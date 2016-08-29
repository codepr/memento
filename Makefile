CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt
SRC=src/main.c src/map.c src/server.c src/queue.c src/partition.c src/util.c

shibui: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o shibui

clean:
	rm -f shibui
