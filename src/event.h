#ifndef EVENT_H
#define EVENT_H

#define ADD_FD(efd, fd) add_epollin(efd, fd)
#define SET_FD_OUT(efd, fd, data) set_epollout(efd, fd, data)
#define SET_FD_IN(efd, fd) set_epollin(efd, fd)


void add_epollin(int, int);
void set_epollout(int, int, void*);
void set_epollin(int, int);

#endif
