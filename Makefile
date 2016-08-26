CC=gcc
CFLAGS = -std=gnu99 -Wall -lrt
SRC = src
OBJS = $(SRC)/map.c $(SRC)/server.c $(SRC)/queue.c $(SRC)/partition.c $(SRC)/util.c
BIN = main
BIN_NAME = shibui

$(BIN): $(SRC)/$(BIN).c
	$(CC) $(CFLAGS) $(SRC)/$(BIN).c $(OBJS) -o $(BIN_NAME)
