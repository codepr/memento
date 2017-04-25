/*
 * Copyright (c) 2016-2017 Andrea Giacomo Baldan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef NETWORKING_H
#define NETWORKING_H


#include <sys/epoll.h>


#define MAX_EVENTS (64)
#define BUFSIZE (2048)


/*
 * File descriptor handler, a functor used when incoming data from clients must
 * be handled
 */
typedef int (*fd_handler)(int, int);


typedef struct task {
    epoll_data_t data;
} task_t;


typedef struct userdata {
    int fd;
    unsigned int size;        // real received data size
    unsigned int heapmem : 1;
    unsigned int buscomm : 1;
    char *data;
} userdata_t;


struct worker_epoll {
    int efd;
    struct epoll_event event;
    struct epoll_event *events;
};



int send_all(int, char *, int *);
int set_nonblocking(int);
int listento(const char *, const char *);
int connectto(const char *, const char *);
int event_loop(int *, size_t, fd_handler);
void schedule_write(int, char *, unsigned long, unsigned int, unsigned int);

#endif
