CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread -O3 -pedantic
RELEASE=./release
SRC=src/memento.c		\
	src/map.c 			\
	src/util.c 			\
	src/commands.c 		\
	src/persistence.c 	\
	src/networking.c 	\
	src/serializer.c 	\
	src/hashing.h 		\
	src/cluster.c 		\
	src/list.c

memento: $(SRC)
	mkdir -p $(RELEASE) && $(CC) $(CFLAGS) $(SRC) -o $(RELEASE)/memento

memento-cli: src/memento-cli.c
	mkdir -p $(RELEASE) && $(CC) $(CFLAGS) src/memento-cli.c -o $(RELEASE)/memento-cli

test:
	cd tests && $(MAKE) test

clean:
	rm -f $(RELEASE)/memento
