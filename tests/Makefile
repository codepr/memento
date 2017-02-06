CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread
SRC=../src/map.c ../src/queue.c ../src/util.c ../src/hashing.h

shibui: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o shibui

test: map_test.c
	$(CC) $(CFLAGS) $(SRC) -o map_test map_test.c
