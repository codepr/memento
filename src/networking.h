#ifndef NETWORKING_H
#define NETWORKING_H

#include <sys/epoll.h>

#define MAX_EVENTS (1024)
#define MAX_DATA_SIZE (10485760)

struct s_handle {
    int efd;
    int sfd;
    int s;
    struct epoll_event event;
    struct epoll_event *events;
};


int set_socket_non_blocking(int);
struct s_handle *create_async_server(const char *, const char *);

#endif
