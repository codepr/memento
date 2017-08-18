/*
 * Copyright (c) 2016-2017 Andrea Giacomo Baldan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef NETWORKING_H
#define NETWORKING_H


#include <sys/epoll.h>


#define EPOLL_WORKERS 4
#define MAX_EVENTS	  64
#define BUFSIZE		  2048


/*
 * File descriptor handler, a functor used when incoming data from clients must
 * be handled
 */
typedef int (*fd_handler)(int, int);


typedef struct peer {
    int fd;
    unsigned int size;
    unsigned int alloc : 1;
	unsigned int tocli : 1;
    char *data;
} peer_t;


typedef struct {
	const char *host;			 /* hostname to listen */
	const char *server_port;	 /* main entry point for connecting clients */
	const char *cluster_port;	 /* cluster service connection port for nodes */
	int epoll_workers; 			 /* epoll workers threadpool number */
	int max_events;				 /* max number of events handled by epoll loop */
	int bufsize;				 /* buffer size for reading data from sockets */
    int epollfd;                 /* file descriptor for epoll */
	int bepollfd;				 /* file descriptor for epoll on the bus */
	int (*cluster_handler)(int); /* function pointer to cluster communication implementation function */
	int (*server_handler)(int);  /* function pointer to client-server communication implementation function */
} event_loop;


void init_event_loop(const char *, const char *, const char *);
int send_all(int, char *, int *);
int set_nonblocking(int);
int listento(const char *, const char *);
int connectto(const char *, const char *);
int start_loop();
void schedule_write(peer_t *);

#endif
