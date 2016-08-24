CC=gcc
CFLAGS = -Wall
SRC = src
OBJS = $(SRC)/map.c $(SRC)/server.c
BIN = main
BIN_NAME = shibui

$(BIN): $(SRC)/$(BIN).c
	$(CC) $(CFLAGS) $(SRC)/$(BIN).c $(OBJS) -o $(BIN_NAME)
