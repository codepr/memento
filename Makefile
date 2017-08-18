CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread -O3 -pedantic
RELEASE=./release
SRC=src/map.c 			\
	src/util.c 			\
	src/commands.c 		\
	src/persistence.c 	\
	src/networking.c 	\
	src/serializer.c 	\
	src/hashing.h 		\
	src/cluster.c 		\
	src/queue.c			\
	src/event.c			\
	src/list.c

memento: $(SRC)
	mkdir -p $(RELEASE) && $(CC) $(CFLAGS) $(SRC) src/memento.c -o $(RELEASE)/memento

memento-cli: src/memento-cli.c
	mkdir -p $(RELEASE) && $(CC) $(CFLAGS) $(SRC) src/memento-cli.c -o $(RELEASE)/memento-cli

memento-benchmark: src/memento-benchmark.c
	mkdir -p $(RELEASE) && $(CC) $(CFLAGS) $(SRC) src/memento-benchmark.c -o $(RELEASE)/memento-benchmark

test:
	cd tests && $(MAKE) test

clean:
	rm -f $(RELEASE)/memento $(RELEASE)/dt_test $(RELEASE)/memento-benchmark
