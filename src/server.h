#ifndef SERVER_H
#define SERVER_H

#define PORT ("6373")
#define MAX_DATA_SIZE (10485760)
// #define MAX_EVENTS (100)

#include "map.h"
#include "queue.h"
#include "partition.h"

void start_server(queue *, int, int, partition **, const char *, const char *);

#endif
