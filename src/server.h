#ifndef SERVER_H
#define SERVER_H

#define PORT "6373"
#define MAX_DATA_SIZE 10485760
#define MAX_EVENTS 100

#include "map.h"

void start_server(const char *);
int process_command(h_map *, char *, int);

#endif
