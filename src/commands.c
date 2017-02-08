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
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "commands.h"
#include "networking.h"
#include "serializer.h"
#include "hashing.h"
#include "cluster.h"
#include "util.h"


/*
 * Array of commands that doesn't need a file descriptor, they exclusively
 * perform side-effects on the keyspace
 */
const char *commands[]   = { "set", "del", "inc", "incf", "dec", "decf",
    "append", "prepend" };
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
const char *services[]   = {"flush"};

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


int check_command(char *buffer) {
    char *command = strtok(buffer, " \r\n");

    // in case of 'QUIT' or 'EXIT' close the connection
    if (strncasecmp(command, "quit", strlen(command)) == 0 || strncasecmp(command, "exit", strlen(command)) == 0)
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


static int hash(char *key) {
    /* uint32_t seed = RANDBETWEEN(0, 65535); // initial seed for murmur hashing */
    uint32_t seed = 65133; // initial seed for murmur hashing
    /* char *command = NULL; */
    /* command = strtok(des_mex, " \r\n"); */
    /* LOG("Command : %s\n", command); */
    /* char *arg_1 = NULL; */
    /* arg_1 = strtok(NULL, " "); */
    if (key) {
        char *holder = (char *) malloc(strlen(key));
        strcpy(holder, key);
        trim(holder);
        int idx = murmur3_32((const uint8_t *) holder, strlen(holder), seed) % PARTITIONS;
        LOG("Destination node: %d for key %s\r\n", idx, holder);
        return idx;
    } else return -1;
    /* char *metadata = deq_mex; */
    /* int cmd_len = *((int*) metadata) + (sizeof(int) * 2); */
    /* struct message mm = deserialize(deq_mex); */
    /* char *des_mex = mm.content; */
    /* char *command = NULL; */
    /* command = strtok(des_mex, " \r\n"); */
    /* LOG("Command : %s\n", command); */
    /* char *arg_1 = NULL; */
    /* arg_1 = strtok(NULL, " "); */
}


int command_handler(int fd, int from_peer) {

    struct message msg;
    char buf[1024];
    memset(buf, 0x00, 1024);

    int ret = read(fd, buf, 1024);

    if (ret == -1) return 1;
    else {

        /* check if cluster mode is enabled */
        if (instance.cluster_mode == 1) {
            /*
             * check if the buffer contains a message from another node or from
             * a client
             */
            if (from_peer == 1) {
                /* message came from a peer node, so it is serialized */
                LOG("Received data from peer node\n");
                char *metadata = buf;
                int cmd_len = *((int*) metadata) + (sizeof(int) * 2);
                struct message m = deserialize(buf);
                LOG("Message received: %s\n", m.content);
                /* message from another node */
                switch (process_command(m.content, fd, fd)) {
                    case MAP_OK:
                        send(m.fd, S_OK, sizeof(S_OK), 0);
                        break;
                    case MAP_ERR:
                        send(fd, S_NIL, sizeof(S_NIL), 0);
                        break;
                    case COMMAND_NOT_FOUND:
                        send(fd, S_UNK, sizeof(S_UNK), 0);
                        break;
                    case END:
                        break;
                    default:
                        break;
                }
            } else {

                /* cluster mode is not enabled, process normally */
                char *command = NULL, *b = strdup(buf); // payload to send
                command = strtok(buf, " \r\n");
                LOG("Command : %s\n", command);
                char *arg_1 = NULL;
                arg_1 = strtok(NULL, " ");
                LOG("Key : %s\n", arg_1);
                int idx = hash(arg_1);

                /*
                 * send the message serialized according to the routing table
                 * cluster
                 */
                list_node *cursor = instance.cluster->head;
                while(cursor) {
                    cluster_node *n = (cluster_node *) cursor->data;
                    LOG("[*] Node: %s:%d - Min: %u Max: %u Name: %s Fd: %d\n",
                            n->addr, n->port, n->range_min, n->range_max, n->name, n->fd);
                    if (idx >= n->range_min && idx <= n->range_max) {
                        /* check if the range is in the current node */
                        if (n->self == 1) {
                            switch (process_command(b, fd, fd)) {
                                case MAP_OK:
                                    send(fd, S_OK, sizeof(S_OK), 0);
                                    break;
                                case MAP_ERR:
                                    send(fd, S_NIL, sizeof(S_NIL), 0);
                                    break;
                                case COMMAND_NOT_FOUND:
                                    send(fd, S_UNK, sizeof(S_UNK), 0);
                                    break;
                                case END:
                                    break;
                                default:
                                    break;
                            }
                            break;
                        } else {
                            msg.content = b;
                            msg.fd = fd;
                            send(n->fd, serialize(msg), strlen(b) + (sizeof(int) * 2), 0);
                            LOG("Found on cluster member %s\n", n->name);
                            break;
                        }
                    }
                    cursor = cursor->next;
                }
            }
        }
        return 0;
    }
}


/*
 * process incoming request from the file descriptor socket represented by
 * sock_fd, map is an array of partition, every partition store an instance of
 * hashmap, keys are instance.cluster_mode through consistent hashing
 * calculated with crc32 % PARTITION_NUMBER
 */
int process_command(char *buffer, int sock_fd, int resp_fd) {
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
            return (*cmds_func[i])(buffer + strlen(command) + 1);
        }
    }
    // check if the buffer contains a query and execute it
    for (int i = 0; i < queries_array_len(); i++) {
        if (strncasecmp(command, queries[i], strlen(command)) == 0) {
            return (*qrs_func[i])(buffer + strlen(command) + 1, sock_fd, resp_fd);
        }
    }
    // check if the buffer contains an enumeration command and execute it
    for (int i = 0; i < enumerates_array_len(); i++) {
        if (strncasecmp(command, enumerates[i], strlen(command)) == 0) {
            return (*enum_func[i])(sock_fd, resp_fd);
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
};

int (*qrs_func[]) (char *, int, int) = {
    &get_command,
    &getp_command,
    &ttl_command
};

int (*enum_func[]) (int, int) = {
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
    char *ret = (char *) malloc(len * sizeof(char) + 1);
    *ret = '\0';
    return strcat(strcat(ret, str), token);
}


/* utility function, remove trailing newline */
static void remove_newline(char *str) {
    str[strcspn(str, "\r\n")] = 0;
}


/* callback function to print all keys inside the hashmap */
static int print_keys(void * t1, void * t2) {
    map_entry *kv = (map_entry *) t2;
    int *fd = (int *) t1;
    char *stringkey = (char *) malloc(strlen(kv->key) + 1);
    bzero(stringkey, strlen(stringkey));
    strcpy(stringkey, kv->key);
    char *key_nl = append_string(stringkey, "\n");
    send(*fd, key_nl, strlen(key_nl), 0);
    return MAP_OK;
}

/* callback function to print all values inside the hashmap */
static int print_values(void * t1, void * t2) {
    map_entry *kv = (map_entry *) t2;
    int *fd = (int *) t1;
    send(*fd, kv->val, strlen(kv->val), 0);
    return MAP_OK;
}

/* SET command handler, calculate in which position of the array of the
 * int instance.cluster_mode, partitions the key-value pair must be stored using CRC32, overwriting in case
 * of a already taken position.
 *
 * Require two arguments:
 *
 *     SET <key> <value>
 */
int set_command(char *command) {
    int ret = 0;
    void *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1)
        arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_2) {
        void *arg_1_holder = malloc(strlen(arg_1));
        void *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        ret = map_put(instance.store, arg_1_holder, arg_2_holder);
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
int get_command(char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    void *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        arg_2 = map_get(instance.store, arg_1);
        if (arg_2) {
            if (instance.cluster_mode == 1) {
                struct message m;
                char response[strlen((char *) arg_2) + 2];
                sprintf(response, "%s%s", "* ", (char *)arg_2);
                m.content = response;
                m.fd = resp_fd;
                send(sock_fd, serialize(m), strlen(response) + (sizeof(int) * 2), 0);
            }
            else
                send(sock_fd, arg_2, strlen((char *) arg_2), 0);
            ret = 1;
        } else ret = MAP_ERR;
    } else ret = MAP_ERR;
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
int getp_command(char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    void *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        map_entry *kv = map_get_entry(instance.store, arg_1);
        if (kv) {
            char *kvstring = (char *) malloc(strlen(kv->key) + strlen((char *) kv->val) + (sizeof(long) * 2) + 40);
            sprintf(kvstring, "key: %s\nvalue: %screation_time: %ld\nexpire_time: %ld\n",
                    (char *) kv->key, (char *) kv->val, kv->creation_time, kv->expire_time);
            if (instance.cluster_mode == 1) {
                struct message m;
                char response[strlen(kvstring) + 2];
                sprintf(response, "%s%s", "* ", kvstring);
                m.content = kvstring;
                m.fd = resp_fd;
                send(sock_fd, serialize(m), strlen(response) + (sizeof(int) * 2), 0);
            } else
                send(sock_fd, kvstring, strlen(kvstring), 0);
            free(kvstring);
            ret = 1;
        } else ret = MAP_ERR;
    } else ret = MAP_ERR;
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
    void *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    while (arg_1 != NULL) {
        trim(arg_1);
        ret = map_del(instance.store, arg_1);
        arg_1 = strtok(NULL, " ");
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
    int ret = 0;
    void *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        arg_2 = map_get(instance.store, arg_1);
        if (arg_2) {
            char *s = (char *) arg_2;
            if (ISINT(s) == 1) {
                int v = GETINT(s);
                char *by = strtok(NULL, " ");
                if (by && ISINT(by)) {
                    v += GETINT(by);
                } else v++;
                sprintf(arg_2, "%d\n", v);
            } else ret = MAP_ERR;
        }
    } else ret = MAP_ERR;
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
    int ret = 0;
    void *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        arg_2 = map_get(instance.store, arg_1);
        if (arg_2) {
            char *s = (char *) arg_2;
            if (is_float(s) == 1) {
                double v = GETDOUBLE(s);
                char *by = strtok(NULL, " ");
                if (by && is_float(by)) {
                    v += GETDOUBLE(by);
                } else v += 1.0;
                sprintf(arg_2, "%lf\n", v);
            } else ret = MAP_ERR;
        }
    } else ret = MAP_ERR;
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
    int ret = 0;
    void *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        arg_2 = map_get(instance.store, arg_1);
        if (arg_2) {
            char *s = (char *) arg_2;
            if(ISINT(s) == 1) {
                int v = GETINT(s);
                char *by = strtok(NULL, " ");
                if (by != NULL && ISINT(by)) {
                    v -= GETINT(by);
                } else v--;
                sprintf(arg_2, "%d\n", v);
            } else ret = MAP_ERR;
        }
    } else ret = MAP_ERR;
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
    int ret = 0;
    void *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        arg_2 = map_get(instance.store, arg_1);
        if (arg_2) {
            char *s = (char *) arg_2;
            if(is_float(s) == 1) {
                double v = GETDOUBLE(s);
                char *by = strtok(NULL, " ");
                if (by != NULL && ISINT(by)) {
                    v -= GETDOUBLE(by);
                } else v -= 1.0;
                sprintf(arg_2, "%lf\n", v);
            } else ret = MAP_ERR;
        }
    } else ret = MAP_ERR;
    return ret;
}

/*
 * COUNT command handler, count all key-value pairs stored in the hashmap and
 * write the result back to the file-descriptor.
 *
 * Doesn't require any argument.
 */
int count_command(int sock_fd, int resp_fd) {
    int len = 0;
    len += instance.store->size;
    char c_len[24];
    sprintf(c_len, "%d\n", len);
    send(sock_fd, c_len, 24, 0);
    return 1;
}

/*
 * KEYS command handler, iterate through all the keyspace and return a list of
 * the present keys.
 *
 * Doesn't require any argument.
 */
int keys_command(int sock_fd, int resp_fd) {
    if (instance.store->size > 0)
        map_iterate(instance.store, print_keys, &sock_fd);
    return 1;
}

/*
 * VALUES command handler, iterate through all the keyspace and return a list of
 * the present values associated to them.
 *
 * Doesn't require any argument.
 */
int values_command(int sock_fd, int resp_fd) {
    if (instance.store->size > 0)
        map_iterate(instance.store, print_values, &sock_fd);
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
    int ret = 0;
    void *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_1 && arg_2) {
        char *arg_1_holder = malloc(strlen(arg_1));
        char *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        void *val = NULL;
        val = map_get(instance.store, arg_1_holder);
        if (val) {
            remove_newline(val);
            char *append = append_string(val, arg_2_holder);
            ret = map_put(instance.store, arg_1_holder, append);
        }
    } else ret = MAP_ERR;
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
    int ret = 0;
    void *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_1 && arg_2) {
        char *arg_1_holder = malloc(strlen(arg_1));
        char *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        void *val = NULL;
        val = map_get(instance.store, arg_1_holder);
        if (val) {
            remove_newline(arg_2_holder);
            char *append = append_string(arg_2_holder, val);
            ret = map_put(instance.store, arg_1_holder, append);
        }
    } else ret = MAP_ERR;
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
int ttl_command(char *command, int sock_fd, int resp_fd) {
    void *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        map_entry *kv = (map_entry *) malloc(sizeof(map_entry));
        kv = map_get_entry(instance.store, arg_1);
        if (kv) {
            char ttl[7];
            if (kv->has_expire_time)
                sprintf(ttl, "%ld\n", kv->expire_time / 1000);
            else
                sprintf(ttl, "%d\n", -1);
            send(sock_fd, ttl, 7, 0);
        }
    } else return MAP_ERR;
    return 0;
}

/*
 * FLUSH command handler, delete the entire keyspace.
 *
 * Doesn't require any argument.
 */
int flush_command(void) {
    if (instance.store != NULL)
        map_release(instance.store);
    return OK;
}

