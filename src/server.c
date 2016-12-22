#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include "util.h"
#include "server.h"
#include "partition.h"
#include "commands.h"
#include "persistence.h"

#define COMMAND_NOT_FOUND (-4)
#define EXPIRATION_CHECK_INTERVAL (3) // check every 3s

static unsigned int is_checking = 0; /* expire-time-key-check thread flag */

// set non-blocking socket
static int set_socket_non_blocking(int fd) {
    int flags, result;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    result = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (result == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}

/* Check every key expire time, if any is set, remove the keys that has exceeded
 * their lifetime
 */
static int check_expire_time(any_t t1, any_t t2) {
    h_map *m = (h_map *) t1;
    kv_pair *kv = (kv_pair *) t2;
    if (m && kv) {
        if (kv->in_use == 1 && kv->has_expire_time != 0) {
            long current_ms = current_timestamp();
            long delta = current_ms - kv->creation_time;
            if (delta >= kv->expire_time) {
                trim(kv->key);
                m_remove(m, kv->key);
            }
        }
    }
    return 1;
}

/* daemon to control all expire time of keys in the partitions, iterates through
 * the entire keyspace checking the expire time of every key that has one set
 */
static void *expire_control_pthread(void *arg) {
    partition **buckets = (partition **) arg;
    if (buckets) {
        while(1) {
            for (int i = 0; i < PARTITION_NUMBER; i++) {
                if (buckets[i]->map->size > 0) {
                    m_iterate(buckets[i]->map, check_expire_time, buckets[i]->map);
                }
            }
            sleep(EXPIRATION_CHECK_INTERVAL);
        }
    }
    return NULL;
}

// auxiliary function for creating epoll server
static int create_and_bind(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     /* 0.0.0.0 all interfaces */

    s = getaddrinfo(host, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0) {
            /* We managed to bind successfully! */
            break;
        }

        close(sfd);
    }

    if (rp == NULL) {
        perror("Could not bind");
        return -1;
    }

    freeaddrinfo(result);

    return sfd;
}

/*
 * process incoming request from the file descriptor socket represented by
 * sock_fd, buckets is an array of partition, every partition store an instance
 * of hashmap, keys are distributed through consistent hashing calculated with
 * crc32 % PARTITION_NUMBER
 */
static int process_command(partition **buckets, char *buffer, int sock_fd) {
    char *command = NULL;
    command = strtok(buffer, " \r\n");
    // in case of empty command return nothing, next additions will be awaiting
    // for incoming chunks
    if (!command)
        return 1;
    // in case of 'QUIT' or 'EXIT' close the connection
    if (strncasecmp(command, "quit", strlen(command)) == 0 || strncasecmp(command, "exit", strlen(command)) == 0)
        return END;
    // check if the buffer contains a command and execute it
    for (int i = 0; i < commands_array_len(); i++) {
        if (strncasecmp(command, commands[i], strlen(command)) == 0) {
            return (*cmds_func[i])(buckets, buffer + strlen(command) + 1);
        }
    }
    // check if the buffer contains a query and execute it
    for (int i = 0; i < queries_array_len(); i++) {
        if (strncasecmp(command, queries[i], strlen(command)) == 0) {
            return (*qrs_func[i])(buckets, buffer + strlen(command) + 1, sock_fd);
        }
    }
    // check if the buffer contains an enumeration command and execute it
    for (int i = 0; i < enumerates_array_len(); i++) {
        if (strncasecmp(command, enumerates[i], strlen(command)) == 0) {
            return (*enum_func[i])(buckets, sock_fd);
        }
    }
    // check if the buffer contains a service command and execute it
    for (int i = 0; i < services_array_len(); i++) {
        if (strncasecmp(command, services[i], strlen(command)) == 0) {
            return (*srvs_func[i])(buckets);
        }
    }
    return COMMAND_NOT_FOUND;
}

// start server instance, by setting hostname
void start_server(const char *host, const char* port, int service) {
    h_map *peers = m_create();
    // partition buckets, every bucket can contain a variable number of
    // key-value pair
    partition **buckets = (partition **) malloc(sizeof(partition) * PARTITION_NUMBER);
    static pthread_t t;
    for (int i = 0; i < PARTITION_NUMBER; i++)
        buckets[i] = create_partition();
    // initialize log for persistence
    struct stat st;
    if (stat(PERSISTENCE_LOG, &st) == -1)
        mkdir(PERSISTENCE_LOG, 0644);
    // start expiration time checking thread
    if (is_checking == 0) {
        if (pthread_create(&t, NULL, &expire_control_pthread, buckets) != 0)
            perror("ERROR pthread");
        is_checking = 1;
    }
    int efd, sfd, s;
    struct epoll_event event, *events;

    sfd = create_and_bind(host, port);
    if (sfd == -1)
        abort();

    s = set_socket_non_blocking(sfd);
    if (s == -1)
        abort();

    s = listen(sfd, SOMAXCONN);
    if (s == -1) {
        perror("listen");
        abort();
    }

    efd = epoll_create1(0);
    if (efd == -1) {
        perror("epoll_create");
        abort();
    }

    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
    if (s == -1) {
        perror("epoll_ctl");
        abort();
    }

    /* Buffer where events are returned */
    events = calloc(MAX_EVENTS, sizeof event);

    if (host == NULL)
        printf("Server listening on 127.0.0.1:%s\n", port);
    else printf("Server listening on %s:%s\n", host, port);

    /* start the event loop */
    while (1) {
        int n, i;

        n = epoll_wait(efd, events, MAX_EVENTS, -1);
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) {
                perror("epoll error");
                close(events[i].data.fd);
                continue;
            }

            else if (sfd == events[i].data.fd) {
                /* We have a notification on the listening socket, which means
                   one or more incoming connections. */
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept(sfd, &in_addr, &in_len);
                    if (infd == -1) {
                        if ((errno == EAGAIN) ||
                            (errno == EWOULDBLOCK)) {
                            /* processed all incoming connections. */
                            break;
                        }
                        else {
                            perror("accept");
                            break;
                        }
                    }

                    s = getnameinfo(&in_addr, in_len,
                                    hbuf, sizeof hbuf,
                                    sbuf, sizeof sbuf,
                                    NI_NUMERICHOST | NI_NUMERICSERV);
                    if (s == 0) {
                        char time_buff[100];
                        time_t now = time(0);
                        strftime(time_buff, 100, "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                        printf("%s - new connection from %s:%s on descriptor %d \n", time_buff,  hbuf, sbuf, infd);
                        if (service == 1) {
                            m_put(peers, hbuf, &infd);
                        }
                    }

                    /* Make the incoming socket non-blocking and add it to the
                       list of fds to monitor. */
                    s = set_socket_non_blocking(infd);
                    if (s == -1)
                        abort();

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1) {
                        perror("epoll_ctl");
                        abort();
                    }
                }
                continue;
            }
            else {
                /* We have data on the fd waiting to be read */
                int done = 0;

                while (1) {
                    ssize_t count;
                    char *buf = malloc(MAX_DATA_SIZE);
                    bzero(buf, MAX_DATA_SIZE);
                    count = read(events[i].data.fd, buf, MAX_DATA_SIZE - 1);
                    if (count == -1) {
                        /* If errno == EAGAIN, that means we have read all data.
                           So go back to the main loop. */
                        if (errno != EAGAIN) {
                            perror("read");
                            done = 1;
                        }
                        free(buf);
                        break;
                    }
                    else if (count == 0) {
                        /* End of file. The remote has closed the connection. */
                        done = 1;
                        free(buf);
                        break;
                    }
                    if (service == 0) {
                        int proc = process_command(buckets, buf, events[i].data.fd);
                        switch(proc) {
                        case MAP_OK:
                            send(events[i].data.fd, "OK\n", 3, 0);
                            break;
                        case MAP_MISSING:
                            send(events[i].data.fd, "NOT FOUND\n", 10, 0);
                            break;
                        case MAP_FULL:
                        case MAP_OMEM:
                            send(events[i].data.fd, "OUT OF MEMORY\n", 14, 0);
                            break;
                        case COMMAND_NOT_FOUND:
                            send(events[i].data.fd, "COMMAND NOT FOUND\n", 18, 0);
                            break;
                        case END:
                            done = 1;
                            break;
                        default:
                            continue;
                            break;
                        }
                        free(buf);
                    }
                }

                if (done) {
                    printf("Closed connection on descriptor %d\n",
                           events[i].data.fd);
                    /* Closing the descriptor will make epoll remove it from the
                       set of descriptors which are monitored. */
                    close(events[i].data.fd);
                }
            }
        }
    }

    /* release all partitions */
    for (int i = 0; i < PARTITION_NUMBER; i++)
        partition_release(buckets[i]);

    free(events);
    close(sfd);

    return;
}
