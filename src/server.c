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
#include <sys/types.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <time.h>
#include <aio.h>
#include <pthread.h>
#include "util.h"
#include "server.h"
#include "partition.h"

#define COMMAND_NOT_FOUND -4

static unsigned int is_checking = 0;
static pthread_t t;

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

static int check_expire_time(any_t t1, any_t t2) {
    long current_ms = current_timestamp();
    h_map *m = (h_map *) t1;
    kv_pair *kv = (kv_pair *) t2;
    if (m && kv) {
        printf("%s -> %d\n", kv->key, kv->has_expire_time);
        if (kv->has_expire_time != 0) {
            long delta = current_ms - kv->creation_time;
            printf("%ld <=> %ld\n", delta, kv->expire_time);
            if (delta >= kv->expire_time) {
                printf("deleting..\n");
                kv->expire_time = -1;
                kv->in_use = 0;
                m_remove(m, kv->key);
            }
        }
    }
    return 1;
}

static void *expire_control_pthread(void *arg) {
    partition **buckets = (partition **) arg;
    if (buckets) {
        while(1) {
            for (int i = 0; i < PARTITION_NUMBER; i++) {
                if (buckets[i]->map->size > 0) {
                    m_iterate(buckets[i]->map, check_expire_time, buckets[1]->map);
                }
            }
            sleep(5);
        }
    }
    return NULL;
}

/* callback function to find all keys with a given prefix */
static int find_prefix(any_t t1, any_t t2) {
    char *key = (char *) t1;
    char *item_key = (char *) t2;

    if (strncmp(key, item_key, strlen(key)) == 0) {
        return MAP_OK;
    }
    return MAP_MISSING;
}

/* utility function, concat two strings togheter */
static char *append_string(const char *str, const char *token) {
    size_t len = strlen(str) + strlen(token);
    char *ret = (char *) malloc(len * sizeof(char) + 1);
    *ret = '\0';
    return strcat(strcat(ret, str), token);
}

/* utility function, remove trailing newline */
static void remove_newline(char *str) {
    str[strcspn(str, "\r\n")] = 0;
}

/* auxiliary function to convert integer contained inside string into int */
static int to_int(char *p) {
    int len = 0;
    while(isdigit(*p)) {
        len = (len * 10) + (*p - '0');
        p++;
    }
    return len;
}

/* callback function to match a given pattern by fuzzy searching it */
static int find_fuzzy_pattern(any_t t1, any_t t2) {
    char *key = (char *) t1;
    char *item_key = (char *) t2;
    int i = 0;
    int k = 0;
    int size = strlen(key);
    int item_size = strlen(item_key);

    while (i < size) {
        if(k < item_size && i < size) {
            if (item_key[k] == key[i]) {
                k++;
                i++;
            } else k++;
        } else return MAP_MISSING;
    }
    return MAP_OK;
}

/* callback function to print all keys inside the hashmap */
static int print_keys(any_t t1, any_t t2) {
    kv_pair *kv = (kv_pair *) t2;
    int *fd = (int *) t1;

    send(*fd, kv->key, strlen(kv->key), 0);
    return MAP_OK;
}

/* callback function to print all values inside the hashmap */
static int print_values(any_t t1, any_t t2) {
    kv_pair *kv = (kv_pair *) t2;
    int *fd = (int *) t1;

    send(*fd, kv->data, strlen(kv->data), 0);
    return MAP_OK;
}

/* static int async_write(char *str)  { */
/*     int file = open(PERSISTENCE_LOG, O_CREAT|O_RDWR|O_APPEND, 0644); */
/*     if (file == -1) { */
/*         perror("log"); */
/*         exit(1); */
/*     } */

/*     struct aiocb cb; */

/*     char *buf = (char *) malloc(strlen(str)); */
/*     strcpy(buf, str); */

/*     memset(&cb, 0, sizeof(struct aiocb)); */
/*     cb.aio_nbytes = strlen(str); */
/*     cb.aio_fildes = file; */
/*     cb.aio_buf = str; */
/*     cb.aio_offset = 0; */

/*     if (aio_write(&cb) != 0) { */
/*         perror("aiocb"); */
/*         return -1; */
/*     } */

/*     while (aio_error(&cb) == EINPROGRESS) {} */


/*     close(file); */
/*     return 0; */
/* } */

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

// start server instance, by setting hostname
void start_server(const char *host, const char* port) {
    // partiton buckets, every bucket can contain a variable number of
    // key-value pair
    partition **buckets = (partition **) malloc(sizeof(partition) * PARTITION_NUMBER);
    for (int i = 0; i < PARTITION_NUMBER; i++)
        buckets[i] = create_partition();
    // initialize log for persistence
    struct stat st;
    if (stat(PERSISTENCE_LOG, &st) == -1)
        mkdir(PERSISTENCE_LOG, 0644);
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

                    printf("Command: %s\n", buf);
                    int proc = process_command(buckets, buf, events[i].data.fd);
                    switch(proc) {
                    case MAP_OK:
                        send(events[i].data.fd, "OK\n", 3, 0);
                        break;
                    case MAP_MISSING:
                        send(events[i].data.fd, "NOT FOUND\n", 10, 0);
                        break;
                    case MAP_FULL:
                        send(events[i].data.fd, "OUT OF MEMORY\n", 14, 0);
                        break;
                    case MAP_OMEM:
                        send(events[i].data.fd, "OUT OF MEMORY\n", 14, 0);
                        break;
                    case COMMAND_NOT_FOUND:
                        send(events[i].data.fd, "COMMAND NOT FOUND\n", 18, 0);
                        break;
                    case CONNECTION_END:
                        done = 1;
                        break;
                    default:
                        break;
                    }
                    free(buf);
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
    /* m_release(hashmap); */
    for (int i = 0; i < PARTITION_NUMBER; i++)
        partition_release(buckets[i]);
    free(events);
    close(sfd);

    return;
}

/*
 * trim string, removing leading and trailing spaces
 */

static void trim(char *str) {
    int i;
    int begin = 0;
    int end = strlen(str) - 1;

    while (isspace(str[begin]))
        begin++;

    while ((end >= begin) && isspace(str[end]))
        end--;

    // Shift all characters back to the start of the string array.
    for (i = begin; i <= end; i++)
        str[i - begin] = str[i];

    str[i - begin] = '\0'; // Null terminate string.
}

/* auxiliary function to check wether a string is an integer */
static int is_number(char *s) {
    int num;
    if(sscanf(s, "%d", &num) == 0) return 0;
    return 1;
}

/*
 * process incoming request from the file descriptor socket represented by
 * sock_fd, buckets is an array of partition, every partition store an instance
 * of hashmap, keys are distributed through consistent hashing calculated with
 * crc32 % PARTITION_NUMBER
 */
int process_command(partition **buckets, char *buffer, int sock_fd) {
    char *command = NULL;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    int ret = 0;
    /* async_write(buffer); */
    command = strtok(buffer, " \r\n");
    // print nothing on an empty command
    if (!command)
        return 1;
    if (strcasecmp(command, "SET") == 0) {
        arg_1 = strtok(NULL, " ");
        if (arg_1)
            arg_2 = arg_1 + strlen(arg_1) + 1;
        if (arg_2) {
            char *arg_1_holder = malloc(strlen(arg_1));
            char *arg_2_holder = malloc(strlen((char *) arg_2));
            strcpy(arg_1_holder, arg_1);
            strcpy(arg_2_holder, arg_2);
            int p_index = partition_hash(arg_1_holder);
            ret = m_put(buckets[p_index]->map, arg_1_holder, arg_2_holder);
        }
    } else if (strcasecmp(command, "GET") == 0) {
        arg_1 = strtok(NULL, " ");
        if (arg_1) {
            trim(arg_1);
            int p_index = partition_hash(arg_1);
            int get = m_get(buckets[p_index]->map, arg_1, &arg_2);
            if (get == MAP_OK && arg_2) {
                send(sock_fd, arg_2, strlen((char *) arg_2), 0);
                return 1;
            } else ret = MAP_MISSING;
        } else ret = MAP_MISSING;
    } else if (strcasecmp(command, "DEL") == 0) {
        arg_1 = strtok(NULL, " ");
        while (arg_1 != NULL) {
            if (arg_1) {
                trim(arg_1);
                int p_index = partition_hash(arg_1);
                ret = m_remove(buckets[p_index]->map, arg_1);
            }
            arg_1 = strtok(NULL, " ");
        }
    } else if (strcasecmp(command, "SUB") == 0) {
        arg_1 = strtok(NULL, " ");
        while (arg_1 != NULL) {
            if (arg_1) {
                trim(arg_1);
                int p_index = partition_hash(arg_1);
                ret = m_sub(buckets[p_index]->map, arg_1, sock_fd);
            }
            arg_1 = strtok(NULL, " ");
        }
    } else if (strcasecmp(command, "UNSUB") == 0) {
        arg_1 = strtok(NULL, " ");
        while (arg_1 != NULL) {
            if (arg_1) {
                trim(arg_1);
                int p_index = partition_hash(arg_1);
                ret = m_unsub(buckets[p_index]->map, arg_1, sock_fd);
            }
            arg_1 = strtok(NULL, " ");
        }
    } else if (strcasecmp(command, "PUB") == 0) {
        arg_1 = strtok(NULL, " ");
        arg_2 = arg_1 + strlen(arg_1) + 1;
        if (arg_1 && arg_2) {
            char *arg_1_holder = malloc(strlen(arg_1));
            char *arg_2_holder = malloc(strlen((char *) arg_2));
            strcpy(arg_1_holder, arg_1);
            strcpy(arg_2_holder, arg_2);
            int p_index = partition_hash(arg_1_holder);
            ret = m_pub(buckets[p_index]->map, arg_1_holder, arg_2_holder);
        }
    } else if (strcasecmp(command, "INC") == 0) {
        arg_1 = strtok(NULL, " ");
        if (arg_1) {
            trim(arg_1);
            int p_index = partition_hash(arg_1);
            ret = m_get(buckets[p_index]->map, arg_1, &arg_2);
            if (ret == MAP_OK && arg_2) {
                char *s = (char *) arg_2;
                if (is_number(s) == 1) {
                    int v = to_int(s);
                    char *by = strtok(NULL, " ");
                    if (by != NULL && is_number(by)) {
                        v += to_int(by);
                    } else v++;
                    sprintf(arg_2, "%d\n", v);
                    ret = m_put(buckets[p_index]->map, arg_1, arg_2);
                } else return MAP_MISSING;
            } else return ret;
        } else return MAP_MISSING;
    } else if (strcasecmp(command, "DEC") == 0) {
        arg_1 = strtok(NULL, " ");
        if (arg_1) {
            trim(arg_1);
            int p_index = partition_hash(arg_1);
            ret = m_get(buckets[p_index]->map, arg_1, &arg_2);
            if (ret == MAP_OK && arg_2) {
                char *s = (char *) arg_2;
                if(is_number(s) == 1) {
                    int v = to_int(s);
                    char *by = strtok(NULL, " ");
                    if (by != NULL && is_number(by)) {
                        v -= to_int(by);
                    } else v--;
                    sprintf(arg_2, "%d\n", v);
                    ret = m_put(buckets[p_index]->map, arg_1, arg_2);
                } else return MAP_MISSING;
            } else return ret;
        } else return MAP_MISSING;
    } else if (strcasecmp(command, "COUNT") == 0) {
        int len = 0;
        for (int i = 0; i < PARTITION_NUMBER; i++)
            len += m_length(buckets[i]->map);
        char c_len[24];
        sprintf(c_len, "%d\n", len);
        send(sock_fd, c_len, 24, 0);
        return 1;
    } else if (strcasecmp(command, "KEYS") == 0) {
        for (int i = 0; i < PARTITION_NUMBER; i++)
            if (buckets[i]->map->size > 0)
                m_iterate(buckets[i]->map, print_keys, &sock_fd);
        ret = 1;
    } else if (strcasecmp(command, "VALUES") == 0) {
        for (int i = 0; i < PARTITION_NUMBER; i++)
            if (buckets[i]->map->size > 0)
                m_iterate(buckets[i]->map, print_values, &sock_fd);
        ret = 1;
    } else if (strcasecmp(command, "TAIL") == 0) {
        arg_1 = strtok(NULL, " ");
        arg_2 = arg_1 + strlen(arg_1) + 1;
        if (arg_1 && arg_2) {
            char *arg_1_holder = malloc(strlen(arg_1));
            char *arg_2_holder = malloc(strlen((char *) arg_2));
            strcpy(arg_1_holder, arg_1);
            strcpy(arg_2_holder, arg_2);
            if (is_number(arg_2_holder)) {
                int i = to_int(arg_2_holder);
                int p_index = partition_hash(arg_1_holder);
                m_sub_from(buckets[p_index]->map, arg_1, sock_fd, i);
                ret = 1;
            } else return -1;
        } else return MAP_MISSING;
    } else if (strcasecmp(command, "PREFSCAN") == 0) {
        arg_1 = strtok(NULL, " ");
        if (arg_1) {
            trim(arg_1);
            int flag = 0;
            for (int i = 0; i < PARTITION_NUMBER; i++) {
                if (buckets[i]->map->size > 0) {
                    ret = m_prefscan(buckets[i]->map, find_prefix, arg_1, sock_fd);
                    if (ret == MAP_OK) flag = 1;
                }
            }
            if (flag == 1) return 1;
        } else return MAP_MISSING;
    } else if (strcasecmp(command, "FUZZYSCAN") == 0) {
        arg_1 = strtok(NULL, " ");
        if (arg_1) {
            trim(arg_1);
            int flag = 0;
            for (int i = 0; i < PARTITION_NUMBER; i++) {
                if (buckets[i]->map->size > 0) {
                    ret = m_fuzzyscan(buckets[i]->map, find_fuzzy_pattern, arg_1, sock_fd);
                    if (ret == MAP_OK) flag = 1;
                }
            }
            if (flag == 1) return 1;
        }
    } else if (strcasecmp(command, "APPEND") == 0) {
        arg_1 = strtok(NULL, " ");
        arg_2 = arg_1 + strlen(arg_1) + 1;
        if (arg_1 && arg_2) {
            char *arg_1_holder = malloc(strlen(arg_1));
            char *arg_2_holder = malloc(strlen((char *) arg_2));
            strcpy(arg_1_holder, arg_1);
            strcpy(arg_2_holder, arg_2);
            int p_index = partition_hash(arg_1_holder);
            void *val = NULL;
            ret = m_get(buckets[p_index]->map, arg_1_holder, &val);
            if (ret == MAP_OK) {
                remove_newline(val);
                char *append = append_string(val, arg_2_holder);
                ret = m_put(buckets[p_index]->map, arg_1_holder, append);
            }
        } else return MAP_MISSING;
    } else if (strcasecmp(command, "PREPEND") == 0) {
        arg_1 = strtok(NULL, " ");
        arg_2 = arg_1 + strlen(arg_1) + 1;
        if (arg_1 && arg_2) {
            char *arg_1_holder = malloc(strlen(arg_1));
            char *arg_2_holder = malloc(strlen((char *) arg_2));
            strcpy(arg_1_holder, arg_1);
            strcpy(arg_2_holder, arg_2);
            int p_index = partition_hash(arg_1_holder);
            void *val = NULL;
            ret = m_get(buckets[p_index]->map, arg_1_holder, &val);
            if (ret == MAP_OK) {
                remove_newline(arg_2_holder);
                char *append = append_string(arg_2_holder, val);
                ret = m_put(buckets[p_index]->map, arg_1_holder, append);
            }
        } else return MAP_MISSING;
    } else if (strcasecmp(command, "EXPIRE") == 0) {
        arg_1 = strtok(NULL, " ");
        arg_2 = arg_1 + strlen(arg_1) + 1;
        if (arg_1 && arg_2) {
            char *arg_1_holder = malloc(strlen(arg_1));
            char *arg_2_holder = malloc(strlen((char *) arg_2));
            strcpy(arg_1_holder, arg_1);
            strcpy(arg_2_holder, arg_2);
            int p_index = partition_hash(arg_1_holder);
            ret = m_set_expire_time(buckets[p_index]->map, arg_1_holder, (long) to_int(arg_2_holder));
            if (ret == MAP_OK) {
                if (is_checking == 0) {
                    if (pthread_create(&t, NULL, &expire_control_pthread, buckets) != 0)
                        perror("ERROR pthread");
                    is_checking = 1;
                }
            }
        } else return MAP_MISSING;
    } else if (strcasecmp(command, "FLUSH") == 0) {
        for (int i = 0; i < PARTITION_NUMBER; i++) {
            partition_release(buckets[i]);
            buckets[i] = create_partition();
        }
        ret = MAP_OK;
    } else if (strcasecmp(command, "QUIT") == 0 || strcasecmp(command, "EXIT") == 0) {
        ret = CONNECTION_END;
    } else ret = COMMAND_NOT_FOUND;
    return ret;
}
