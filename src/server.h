#ifndef SERVER_H
#define SERVER_H

#define PORT ("6373")
// #define MAX_EVENTS (100)

#include "map.h"
#include "queue.h"

void start_server(queue *, int, int, map_t, const char *, const char *);

#endif
