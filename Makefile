CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread
RELEASE=./release
SRC=src/main.c 			\
	src/map.c 			\
	src/util.c 			\
	src/commands.c 		\
	src/persistence.c 	\
	src/networking.c 	\
	src/serializer.c 	\
	src/hashing.h 		\
	src/cluster.c 		\
	src/list.c

shibui: $(SRC)
	mkdir -p $(RELEASE) && $(CC) $(CFLAGS) $(SRC) -o $(RELEASE)/shibui

shibui-cli: src/shibui-cli.c
	mkdir -p $(RELEASE) && $(CC) $(CFLAGS) src/shibui-cli.c -o $(RELEASE)/shibui-cli

test:
	cd tests && $(MAKE) test

clean:
	rm -f $(RELEASE)/shibui
