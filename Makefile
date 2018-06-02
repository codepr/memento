CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread -O3 -pedantic
BIN=./bin
SRC=src/map.c 			\
	src/util.c 			\
	src/commands.c 		\
	src/persistence.c 	\
	src/networking.c 	\
	src/serializer.c 	\
	src/hashing.h 		\
	src/cluster.c 		\
	src/event.c			\
	src/list.c

memento: $(SRC)
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/memento.c -o $(BIN)/memento

memento-cli: src/memento-cli.c
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/memento-cli.c -o $(BIN)/memento-cli

memento-benchmark: src/memento-benchmark.c
	mkdir -p $(BIN) && $(CC) $(CFLAGS) $(SRC) src/memento-benchmark.c -o $(BIN)/memento-benchmark

test:
	cd tests && $(MAKE) test

clean:
	rm -f $(BIN)/memento $(BIN)/dt_test $(BIN)/memento-benchmark
