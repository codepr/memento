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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include "commands.h"
#include "networking.h"
#include "serializer.h"
#include "hashing.h"
#include "cluster.h"
#include "util.h"


/* Private functions declarations */
static void get_clusterinfo(int sfd);

/*
 * Array of commands that doesn't need a file descriptor, they exclusively
 * perform side-effects on the keyspace
 */
const char *commands[]   = { "set", "del", "inc", "incf", "dec", "decf",
    "append", "prepend", "expire" };
/*
 * Array of query-commands, they do not perform side-effects directly on the
 * keyspace (except for sub/unsub, that add/remove file-descriptor as subscriber
 * to a 'topic' key), but they need a file-descriptor in order to respond to the
 * client requesting
 */
const char *queries[]    = { "get", "getp", "ttl" };
/*
 * Array of enumeration-commands, they perform action based on iteration through
 * the whole keyspace and respond with results directly to the client
 */
const char *enumerates[] = { "count", "keys", "values" };

/*
 * Array of service-commands, currently there's just one command resident here
 * flush the whole keyspace by deleting all contents and freeing up space.
 */
const char *services[]   = { "flush", "clusterinfo" };

/*
 * Length of the commands-arrays, cannot be retrieved with a generic function by
 * using pointers because the real definition, and ofc the size of the arrays
 * resides in the source file directly and can mutate during future revisions
 */
int commands_array_len(void) {
    return sizeof(commands) / sizeof(char *);
}

int queries_array_len(void) {
    return sizeof(queries) / sizeof(char *);
}

int enumerates_array_len(void) {
    return sizeof(enumerates) / sizeof(char *);
}

int services_array_len(void) {
    return sizeof(services) / sizeof(char *);
}


/*
 * Auxiliary function used to check the command coming from clients
 * FIXME: repeated code
 */
int check_command(char *buffer) {

    char *command = strtok(buffer, " \r\n");

    if (command) {
        // in case of 'QUIT' or 'EXIT' close the connection
        if (strncasecmp(command, "quit", strlen(command)) == 0
                || strncasecmp(command, "exit", strlen(command)) == 0)
            return END;

        // check if the buffer contains a command and execute it
        for (int i = 0; i < commands_array_len(); i++) {
            if (strncasecmp(command, commands[i], strlen(command)) == 0) {
                return 1;
            }
        }
        // check if the buffer contains a query and execute it
        for (int i = 0; i < queries_array_len(); i++) {
            if (strncasecmp(command, queries[i], strlen(command)) == 0) {
                return 1;
            }
        }
        // check if the buffer contains an enumeration command and execute it
        for (int i = 0; i < enumerates_array_len(); i++) {
            if (strncasecmp(command, enumerates[i], strlen(command)) == 0) {
                return 1;
            }
        }
        // check if the buffer contains a service command and execute it
        for (int i = 0; i < services_array_len(); i++) {
            if (strncasecmp(command, services[i], strlen(command)) == 0) {
                return 1;
            }
        }
        return COMMAND_NOT_FOUND;
    }
    return 0;
}


/*
 * Utility function, get a valid index in the range of the cluster buckets, so
 * it is possible to route command to the correct node
 */
static int hash(char *key) {
    uint32_t seed = 65133; // initial seed for murmur hashing
    if (key) {
        char *holder = malloc(strlen(key));
        strcpy(holder, key);
        trim(holder);
        /* retrieve an index in the bucket range */
        int idx = murmur3_32((const uint8_t *) holder,
                strlen(holder), seed) % PARTITIONS;

        LOG(DEBUG, "Destination node: %d for key %s\r\n", idx, holder);
        free(holder);
        return idx;
    } else return -1;
}


/*
 * Utility function used to respond to clients with one of the common answers
 * based on the outcome of the command call
 */
static int answer(int fd, int resp) {
    switch (resp) {
        case MAP_OK:
            schedule_write(fd, S_OK, sizeof(S_OK));
            break;
        case MAP_ERR:
            schedule_write(fd, S_NIL, sizeof(S_NIL));
            break;
        case COMMAND_NOT_FOUND:
            schedule_write(fd, S_UNK, sizeof(S_UNK));
            break;
        case END:
            return END;
            break;
        default:
            break;
    }
    return 0;
}


/*
 * Helper function to route command to the node into which keyspace contains
 * idx
 */
static void route_command(int idx, int fd, char *b, struct message *msg) {
    /*
     * send the message serialized according to the routing table
     * cluster
     */
    list_node *cursor = instance.cluster->head;
    while(cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        LOG(DEBUG, "[*] Node: %s:%d - Min: %u Max: %u Name: %s Fd: %d\n",
                n->addr, n->port, n->range_min, n->range_max, n->name, n->fd);
        if (idx >= n->range_min && idx <= n->range_max) {
            /* check if the range is in the current node */
            if (n->self == 1) {
                answer(fd, process_command(b, fd, fd, 0));
                break;
            } else {
                msg->content = b;
                msg->fd = fd;
                msg->from_peer = 0;
                char *payload = serialize(*msg);
                schedule_write(n->fd, payload, strlen(b) + S_OFFSET);
                /* free(payload); */
                LOG(DEBUG, "Redirect toward cluster member %s\n", n->name);
                break;
            }
        }
        cursor = cursor->next;
    }

}


/*
 * Handle commands incoming from clients, they either be external clients
 * querying the store or other peer nodes of the cluster
 */
int command_handler(int fd, int from_peer) {

    struct message msg;
    char buf[2048];
    memset(buf, 0x00, 2048);
    int ret = 0;

    /* read data from file descriptor socket */
    int bytes = recv(fd, buf, 2048, 0);

    if (bytes == -1) {
        if (errno != EAGAIN) ret = 1;
    } else if (bytes == 0) ret = 1;
    else {
        /* check if cluster mode is enabled */
        if (instance.cluster_mode == 1) {

            /*
             * check if the buffer contains a message from another node or from
             * a client
             */
            if (from_peer == 1) {

                /* LOG(DEBUG, "Received data from peer node, testing pre deserialization: %s\n", buf); */
                /* message came from a peer node, so it is serialized */
                struct message m = deserialize(buf); // deserialize into message structure
                LOG(DEBUG, "Received data from peer node, message: %s\n", m.content);

                if (strcmp(m.content, S_OK) == 0
                        || strcmp(m.content, S_NIL) == 0
                        || strcmp(m.content, S_UNK) == 0) {

                    /* answer to the original client */
                    if (strcmp(m.content, S_OK) == 0)
                        schedule_write(m.fd, S_OK, strlen(S_OK));
                    else if (strcmp(m.content, S_NIL) == 0)
                        schedule_write(m.fd, S_NIL, strlen(S_NIL));
                    else if (strcmp(m.content, S_UNK) == 0)
                        schedule_write(m.fd, S_UNK, strlen(S_UNK));
                } else if (m.from_peer == 1) {
                    /* answer to a query operations to the original client */
                    schedule_write(m.fd, m.content, strlen(m.content));

                } else {
                    /* message from another node */
                    size_t len = 0;
                    char *payload = NULL;
                    switch (process_command(m.content, fd, m.fd, 1)) {
                        case MAP_OK:
                            msg.content = S_OK;
                            msg.fd = m.fd;
                            msg.from_peer = 0;
                            len = strlen(S_OK) + S_OFFSET;
                            payload = serialize(msg);
                            break;
                        case MAP_ERR:
                            msg.content = S_NIL;
                            msg.fd = m.fd;
                            msg.from_peer = 0;
                            len = strlen(S_NIL) + S_OFFSET;
                            payload = serialize(msg);
                            break;
                        case COMMAND_NOT_FOUND:
                            msg.content = S_UNK;
                            msg.fd = m.fd;
                            msg.from_peer = 0;
                            len = strlen(S_UNK) + S_OFFSET;
                            payload = serialize(msg);
                            break;
                        case END:
                            ret = END;
                            break;
                        default:
                            break;
                    }
                    schedule_write(fd, payload, len);
                    LOG(DEBUG, "Response to peer node\n");
                    /* if (payload) free(payload); */
                }
            } else {
                /* check the if the command is genuine */
                int check = check_command(strdup(buf));

                if (check == 1) {

                    /* message came directly from a client */
                    char *command = NULL, *b = strdup(buf); // payload to send
                    command = strtok(buf, " \r\n");
                    LOG(DEBUG, "Command : %s\n", command);

                    if (strncasecmp(command, "clusterinfo", 11) == 0) {
                        /* it is an informative command, no need to route it */
                        get_clusterinfo(fd);

                    } else if (strncasecmp(command, "del", 3) == 0) {
                        /* it is a multi key command (the only one currently is 'DEL')
                         * must handle it differently
                         * TODO: implement a better way to handle this case, this one can
                         * be considered as an ugly patch
                         */
                        char *key = strtok(NULL, " ");
                        while (key) {
                            int idx = hash(key);
                            char del[5 + strlen(key)];
                            snprintf(del, 5 + strlen(key), "del %s\r\n", key);
                            route_command(idx, fd, del, &msg);
                            key = strtok(NULL, " ");
                        }
                    } else if (strncasecmp(command, "count", 5) == 0 ||
                            strncasecmp(command, "keys", 4) == 0 ||
                            strncasecmp(command, "values", 6) == 0 ||
                            strncasecmp(command, "flush", 5) == 0) {
                        /* it is a global command, must handle this differently
                         * TODO: implement a better way to handle this case
                         */
                        list_node *cursor = instance.cluster->head;
                        while(cursor) {
                            cluster_node *n = (cluster_node *) cursor->data;
                            if (n->self == 1) answer(fd, process_command(b, fd, fd, 0));
                            else {
                                msg.content = b;
                                msg.fd = fd;
                                msg.from_peer = 0;
                                char *payload = serialize(msg);
                                schedule_write(n->fd, payload, strlen(b) + S_OFFSET);
                                /* free(payload); */
                                LOG(DEBUG, "Redirect toward cluster member %s\n", n->name);
                            }
                            cursor = cursor->next;
                        }
                    } else {
                        /* command is handled and it isn't an informative one */
                        char *arg_1 = strtok(NULL, " ");
                        LOG(DEBUG, "Key : %s\n", arg_1);
                        int idx = hash(arg_1);
                        /* route the command to the correct node */
                        route_command(idx, fd, b, &msg);
                    }
                } else {
                    /* command received is not recognized or is a quit command */
                    switch (check) {
                        case COMMAND_NOT_FOUND:
                            schedule_write(fd, S_UNK, strlen(S_UNK));
                            break;
                        case END:
                            ret = END;
                            break;
                        default:
                            break;
                    }
                }
            }
        } else {
            /* Single node instance, cluster is not enabled */
            ret = answer(fd, process_command(buf, fd, fd, 0));
        }
    }
    return ret;
}


/*
 * process incoming request from the file descriptor socket represented by
 * sfd, map is an array of partition, every partition store an instance of
 * hashmap, keys are instance.cluster_mode through consistent hashing
 * calculated with crc32 % PARTITION_NUMBER
 */
int process_command(char *buffer, int sfd, int rfd, unsigned int from_peer) {
    char *command = NULL;
    command = strtok(buffer, " \r\n");
    /*
     * In case of empty command return nothing, next additions will be awaiting
     * for incoming chunks
     */
    if (!command)
        return 1;
    // in case of 'QUIT' or 'EXIT' close the connection
    if (strncasecmp(command, "quit", strlen(command)) == 0
            || strncasecmp(command, "exit", strlen(command)) == 0)
        return END;

    // check if the buffer contains a command and execute it
    for (int i = 0; i < commands_array_len(); i++) {
        if (strncasecmp(command, commands[i], strlen(command)) == 0) {
            return (*cmds_func[i])(buffer + strlen(command) + 1);
        }
    }
    // check if the buffer contains a query and execute it
    for (int i = 0; i < queries_array_len(); i++) {
        if (strncasecmp(command, queries[i], strlen(command)) == 0) {
            return (*qrs_func[i])(buffer + strlen(command) + 1,
                    sfd, rfd, from_peer);
        }
    }
    // check if the buffer contains an enumeration command and execute it
    for (int i = 0; i < enumerates_array_len(); i++) {
        if (strncasecmp(command, enumerates[i], strlen(command)) == 0) {
            return (*enum_func[i])(sfd, rfd, from_peer);
        }
    }
    // check if the buffer contains a service command and execute it
    for (int i = 0; i < services_array_len(); i++) {
        if (strncasecmp(command, services[i], strlen(command)) == 0) {
            return (*srvs_func[i])();
        }
    }
    return COMMAND_NOT_FOUND;
}


/* Mapping tables to the commands handlers, maintaining the order defined by
 * arrays of commands
 */
int (*cmds_func[]) (char *) = {
    &set_command,
    &del_command,
    &inc_command,
    &incf_command,
    &dec_command,
    &decf_command,
    &append_command,
    &prepend_command,
    &expire_command
};

int (*qrs_func[]) (char *, int, int, unsigned int) = {
    &get_command,
    &getp_command,
    &ttl_command
};

int (*enum_func[]) (int, int, unsigned int) = {
    &count_command,
    &keys_command,
    &values_command
};

int (*srvs_func[]) (void) = {
    &flush_command
};

/* utility function, concat two strings togheter */
static char *append_string(const char *str, const char *token) {
    size_t len = strlen(str) + strlen(token);
    char *ret = malloc(len * sizeof(char) + 1);
    *ret = '\0';
    return strcat(strcat(ret, str), token);
}


/* utility function, remove trailing newline */
static void remove_newline(char *str) {
    str[strcspn(str, "\r\n")] = 0;
}


/* callback function to print all keys inside the hashmap */
static int print_keys(void *t1, void *t2) {
    map_entry *kv = (map_entry *) t2;
    int *fd = (int *) t1;
    char *stringkey = calloc(strlen(kv->key) + 1, sizeof(*stringkey));
    strcpy(stringkey, kv->key);
    char *key_nl = append_string(stringkey, "\n");
    schedule_write(*fd, key_nl, strlen(key_nl));
    /* free(key_nl); */
    /* free(stringkey); */
    return MAP_OK;
}


/* callback function to print all keys inside the hashmap in a cluster context */
static int cluster_print_keys(void *t1, void *t2, void *t3) {
    map_entry *entry = (map_entry *) t3;
    int *sfd = (int *) t1;
    int *rfd = (int *) t2;
    struct message msg;
    msg.from_peer = 1;
    msg.fd = *rfd;
    char *key = malloc(strlen((char *) entry->key) + 2);
    snprintf(key, strlen((char *) entry->key) + 2, "%s\n\n", (char *) entry->key);
    key[strlen((char *) entry->key) + 2] = '\0';
    msg.content = key;
    char *payload = serialize(msg);
    schedule_write(*sfd, payload, strlen(entry->key) + 2 + S_OFFSET);
    /* free(payload); */
    return MAP_OK;
}


/* callback function to print all values inside the hashmap */
static int print_values(void *t1, void *t2) {
    map_entry *entry = (map_entry *) t2;
    int *fd = (int *) t1;
    schedule_write(*fd, entry->val, strlen(entry->val));
    return MAP_OK;
}


/* callback function to print all values inside the hashmap in a cluster context */
static int cluster_print_values(void *t1, void *t2, void *t3) {
    map_entry *entry = (map_entry *) t3;
    int *sfd = (int *) t1;
    int *rfd = (int *) t2;
    struct message msg;
    msg.from_peer = 1;
    msg.fd = *rfd;
    char *val = malloc(strlen((char *) entry->val) + 1);
    snprintf(val, strlen((char *) entry->val) + 1, "%s\n", (char *) entry->val);
    msg.content = val;
    char *payload = serialize(msg);
    schedule_write(*sfd, payload, strlen(entry->val) + 2 + S_OFFSET);
    /* free(payload); */
    return MAP_OK;
}


/*
 * SET command handler, calculate in which position of the array of the int
 * instance.cluster_mode, partitions the key-value pair must be stored using
 * CRC32, overwriting in case of a already taken position.
 *
 * Require two arguments:
 *
 *     SET <key> <value>
 */
int set_command(char *command) {
    int ret = 0;
    void *key = strtok(command, " ");
    if (key) {
        void *val = (char *) key + strlen(key) + 1;
        if (val) ret = map_put(instance.store, strdup(key), strdup(val));
    }
    return ret;
}


/*
 * GET command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32, write back the value associated to
 * it if present, MISSING reply code otherwise.
 *
 * Require one argument:
 *
 *     GET <key>
 */
int get_command(char *command, int sfd, int rfd, unsigned int from_peer) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        if (key) {
            trim(key);
            void *val = map_get(instance.store, key);
            if (val) {
                if (instance.cluster_mode == 1 && from_peer == 1) {
                    struct message m;
                    /* adding some informations about the node host */
                    char response[strlen((char *) val) + strlen(self.addr) + strlen(self.name) + 9];
                    sprintf(response, "%s:%s:%d> %s", self.name, self.addr, self.port, (char *) val);
                    m.content = response;
                    m.fd = rfd;
                    m.from_peer = 1;
                    char *payload = serialize(m);
                    schedule_write(sfd, payload, strlen(response) + S_OFFSET);
                    /* free(payload); */
                } else {
                    schedule_write(sfd, val, strlen((char *) val));
                }
                ret = 1;
            }
        }
    }
    return ret;
}


/*
 * GETP command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32, write back all the informations
 * associated to the key-value pair if present, including expire time and
 * creation time, MISSING reply code otherwise.
 *
 * Require one argument:
 *
 *     GETP <key>
 */
int getp_command(char *command, int sfd, int rfd, unsigned int from_peer) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        if (key) {
            trim(key);
            map_entry *kv = map_get_entry(instance.store, key);
            if (kv) {
                size_t kvstrsize = strlen(kv->key)
                    + strlen((char *) kv->val) + (sizeof(long) * 2) + 128;
                char *kvstring = malloc(kvstrsize); // long numbers

                /* check if expire time is set */
                char expire_time[7];
                if (kv->has_expire_time)
                    sprintf(expire_time, "%ld\n", kv->expire_time / 1000);
                else
                    sprintf(expire_time, "%d\n", -1);

                /* format answer */
                sprintf(kvstring, "key: %s\nvalue: %screation_time: %ld\nexpire_time: %s\n",
                        (char *) kv->key, (char *) kv->val, kv->creation_time, expire_time);
                if (instance.cluster_mode == 1 && from_peer == 1) {
                    struct message m;
                    /* adding some informations about the node host */
                    char response[strlen(kvstring) + strlen(self.addr) + strlen(self.name) + 18];
                    sprintf(response, "Node: %s:%s:%d\n%s\n", self.name, self.addr, self.port, kvstring);
                    m.content = response;
                    m.fd = rfd;
                    m.from_peer = 1;
                    char *payload = serialize(m);
                    schedule_write(sfd, payload, strlen(response) + S_OFFSET);
                    /* free(payload); */
                } else {
                    schedule_write(sfd, kvstring, kvstrsize);
                }
                /* free(kvstring); */
                ret = 1;
            }
        }
    }
    return ret;
}


/*
 * DEL command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and delete it if present, return
 * MISSING reply code otherwise.
 *
 * Require one argument:
 *
 *     DEL <key>
 */
int del_command(char *command) {
    int ret = 0;
    if (instance.store) {
        void *key = strtok(command, " ");
        while (key) {
            trim(key);
            ret = map_del(instance.store, key);
            key = strtok(NULL, " ");
        }
    }
    return ret;
}


/*
 * INC command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and increment (add 1) to the integer
 * value to it if present, return MISSING reply code otherwise.
 *
 * Requires at least one arguments, but also accept optionally the amount to be
 * added to the specified key:
 *
 *     INC <key>   // +1 to <key>
 *     INC <key> 5 // +5 to <key>
 */
int inc_command(char *command) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        if (key) {
            trim(key);
            void *val = map_get(instance.store, key);
            if (val) {
                char *s = (char *) val;
                if (ISINT(s) == 1) {
                    int v = GETINT(s);
                    char *by = strtok(NULL, " ");
                    if (by && ISINT(by)) {
                        v += GETINT(by);
                    } else v++;
                    sprintf(val, "%d\n", v);
                    ret = 0;
                }
            }
        }
    }
    return ret;
}

/*
 * INCF command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and increment (add 1.00) to the
 * float value to it if present, return MISSING reply code otherwise.
 *
 * Requires at least one arguments, but also accept optionally the amount to be
 * added to the specified key:
 *
 *     INCF <key>     // +1.0 to <key>
 *     INCF <key> 5.0 // +5.0 to <key>
 */
int incf_command(char *command) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        if (key) {
            trim(key);
            void *val = map_get(instance.store, key);
            if (val) {
                char *s = (char *) val;
                if (is_float(s) == 1) {
                    double v = GETDOUBLE(s);
                    char *by = strtok(NULL, " ");
                    if (by && is_float(by)) {
                        v += GETDOUBLE(by);
                    } else v += 1.0;
                    sprintf(val, "%lf\n", v);
                    ret = 0;
                }
            }
        }
    }
    return ret;
}

/*
 * DEC command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and decrement (subtract 1) to the
 * integer value to it if present, return MISSING reply code otherwise.
 *
 * Requires at least one arguments, but also accept optionally the amount to be
 * subtracted to the specified key:
 *
 *     DEC <key>   // -1 to <key>
 *     DEC <key> 5 // -5 to <key>
 */
int dec_command(char *command) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        if (key) {
            trim(key);
            void *val = map_get(instance.store, key);
            if (val) {
                char *s = (char *) val;
                if(ISINT(s) == 1) {
                    int v = GETINT(s);
                    char *by = strtok(NULL, " ");
                    if (by != NULL && ISINT(by)) {
                        v -= GETINT(by);
                    } else v--;
                    sprintf(val, "%d\n", v);
                    ret = 0;
                }
            }
        }
    }
    return ret;
}

/*
 * DECF command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and decrement (subtract 1.00) to the
 * foat value to it if present, return MISSING reply code otherwise.
 *
 * Requires at least one arguments, but also accept optionally the amount to be
 * subtracted to the specified key:
 *
 *     DECF <key>     // -1.0 to <key>
 *     DECF <key> 5.0 // -5.0 to <key>
 */
int decf_command(char *command) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        if (key) {
            trim(key);
            void *val = map_get(instance.store, key);
            if (val) {
                char *s = (char *) val;
                if(is_float(s) == 1) {
                    double v = GETDOUBLE(s);
                    char *by = strtok(NULL, " ");
                    if (by != NULL && ISINT(by)) {
                        v -= GETDOUBLE(by);
                    } else v -= 1.0;
                    sprintf(val, "%lf\n", v);
                    ret = 0;
                }
            }
        }
    }
    return ret;
}

/*
 * COUNT command handler, count all key-value pairs stored in the hashmap and
 * write the result back to the file-descriptor.
 *
 * Doesn't require any argument.
 */
int count_command(int sfd, int rfd, unsigned int from_peer) {
    if (instance.store) {
        unsigned long len = instance.store->size;
        char c_len[16];
        memset(c_len, 0x00, 16);
        snprintf(c_len, 16, "%lu\n", len);
        if (instance.cluster_mode == 1 && from_peer == 1) {
            struct message msg;
            msg.fd = rfd;
            msg.from_peer = 1;
            char response[strlen(c_len) + strlen(self.addr) + strlen(self.name) + 9];
            memset(response, 0x00, strlen(c_len) + strlen(self.addr) + strlen(self.name) + 9);
            sprintf(response, "%s:%s:%d> %s", self.name, self.addr, self.port, c_len);
            msg.content = response;
            char *payload = serialize(msg);
            schedule_write(sfd, payload, strlen(response) + S_OFFSET);
            /* free(payload); */
        } else {
            schedule_write(sfd, c_len, 16);
        }
    }
    return 1;
}

/*
 * KEYS command handler, iterate through all the keyspace and return a list of
 * the present keys.
 *
 * Doesn't require any argument.
 */
int keys_command(int sfd, int rfd, unsigned int from_peer) {
    if (instance.store) {
        if (instance.store->size > 0) {
            if (instance.cluster_mode == 1 && from_peer == 1) {
                map_iterate3(instance.store, cluster_print_keys, &sfd, &rfd);
            } else {
                map_iterate2(instance.store, print_keys, &sfd);
            }
        }
    }
    return 1;
}

/*
 * VALUES command handler, iterate through all the keyspace and return a list of
 * the present values associated to them.
 *
 * Doesn't require any argument.
 */
int values_command(int sfd, int rfd, unsigned int from_peer) {
    if (instance.store) {
        if (instance.store->size > 0) {
            if (instance.cluster_mode == 1 && from_peer == 1) {
                map_iterate3(instance.store, cluster_print_values, &sfd, &rfd);
            } else {
                map_iterate2(instance.store, print_values, &sfd);
            }
        }
    }
    return 1;
}


/*
 * APPEND command handler, finds the map *inside the partitions array using
 * CRC32 and append a suffix to the value associated to the found key, returning
 * MISSING reply code if no key were found.
 *
 * Require two arguments:
 *
 *     APPEND <key> <value>
 */
int append_command(char *command) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        void *val = (char *) key + strlen(key) + 1;
        if (key && val) {
            char *key_holder = strdup(key);
            char *val_holder = strdup(val);
            void *_val = map_get(instance.store, key_holder);
            if (_val) {
                remove_newline(_val);
                char *append = append_string(_val, val_holder);
                ret = map_put(instance.store, key_holder, append);
            }
        }
    }
    return ret;
}

/*
 * PREPEND command handler, finds the map *inside the partitions array using
 * CRC32 and prepend a prefix to the value associated to the found key, returning
 * MISSING reply code if no key were found.
 *
 * Require two arguments:
 *
 *     PREPEND <key> <value>
 */
int prepend_command(char *command) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        void *val = (char *) key + strlen(key) + 1;
        if (key && val) {
            char *key_holder = strdup(key);
            char *val_holder = strdup(val);
            void *_val = map_get(instance.store, key_holder);
            if (_val) {
                remove_newline(val_holder);
                char *append = append_string(val_holder, _val);
                ret = map_put(instance.store, key_holder, append);
            }
        }
    }
    return ret;
}


/*
 * EXPIRE command handler, get the entry identified by the key and set a ttl
 * after wchich the key will be deleted.
 */
int expire_command(char *command) {
    int ret = MAP_ERR;
    if (instance.store) {
        void *key = strtok(command, " ");
        if (key) {
            trim(key);
            map_entry *entry = malloc(sizeof(map_entry));
            entry = map_get_entry(instance.store, key);
            void *val = (char *) key + strlen(key) + 1;
            if (val) {
                long ts = current_timestamp();
                int intval = GETINT(val);
                entry->has_expire_time = 1;
                entry->creation_time = ts;
                entry->expire_time = ts + (long) intval;
                ret = MAP_OK;
            }
        }
    }
    return ret;
}


/*
 * TTL command handler, finds the partitions containing the keys using CRC32 and
 * get the expire time specified for that key, after which it will be deleted.
 *
 * Require one argument:
 *
 *     TTL <key>
 */
int ttl_command(char *command, int sfd, int rfd, unsigned int from_peer) {
    if (instance.store) {
        void *key = strtok(command, " ");
        if (key) {
            trim(key);
            map_entry *kv = malloc(sizeof(map_entry));
            kv = map_get_entry(instance.store, key);
            if (kv) {
                char ttl[7];
                if (kv->has_expire_time)
                    sprintf(ttl, "%ld\n", kv->expire_time / 1000);
                else
                    sprintf(ttl, "%d\n", -1);
                if (instance.cluster_mode == 1 && from_peer == 1) {
                    struct message msg;
                    msg.fd = rfd;
                    msg.content = ttl;
                    msg.from_peer = 1;
                    char *payload = serialize(msg);
                    schedule_write(sfd, payload, strlen(ttl) + S_OFFSET);
                    /* free(payload); */
                } else {
                    schedule_write(sfd, ttl, 7);
                }
            }
        } else return MAP_ERR;
    }
    return 0;
}


/*
 * FLUSH command handler, delete the entire keyspace.
 *
 * Doesn't require any argument.
 */
int flush_command(void) {
    if (instance.store != NULL) {
        map_release(instance.store);
        instance.store = NULL;
    }
    return OK;
}


/*
 * CLUSTERINFO command handler, collect some informations of the cluster, used
 * as a private function.
 *
 */
static void get_clusterinfo(int sfd) {
    if (instance.cluster_mode == 1) {
        char info[1024 * instance.cluster->len];
        int pos = 0;
        list_node *cursor = instance.cluster->head;
        while (cursor) {
            cluster_node *n = (cluster_node *) cursor->data;
            int size = strlen(n->name) + strlen(n->addr) + 47; // 47 for literal string
            char *status = "reachable";
            if (n->state == UNREACHABLE) status = "unreachable";
            snprintf(info + pos, size, "%s - %s:%d - Key range: %d - %d %s\n", n->name,
                    n->addr, n->port, n->range_min, n->range_max, status);
            pos += size;
            cursor = cursor->next;
        }
        schedule_write(sfd, info, pos);
    }
}
