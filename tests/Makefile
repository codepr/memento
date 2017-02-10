CC=gcc
CFLAGS=-std=gnu99 -Wall -lrt -lpthread
RELEASE=../release
SRC=../src/map.c 		\
	../src/util.c 		\
	../src/hashing.h 	\
	../src/cluster.c	\
	../src/commands.c 	\
	../src/persistence.c\
	../src/networking.c \
	../src/serializer.c \
	../src/list.c


shibui: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o shibui

test: map_test.c
	mkdir -p $(RELEASE) && $(CC) $(CFLAGS) $(SRC) -o $(RELEASE)/map_test map_test.c && $(RELEASE)/map_test
