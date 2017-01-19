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
#include <sys/socket.h>
#include "commands.h"
#include "cluster.h"
#include "serializer.h"
#include "util.h"

/*
 * Array of commands that doesn't need a file descriptor, they exclusively
 * perform side-effects on the keyspace
 */
const char *commands[]   = { "set", "del", "pub", "inc", "incf", "dec", "decf",
    "append", "prepend", "expire" };
/*
 * Array of query-commands, they do not perform side-effects directly on the
 * keyspace (except for sub/unsub, that add/remove file-descriptor as subscriber
 * to a 'topic' key), but they need a file-descriptor in order to respond to the
 * client requesting
 */
const char *queries[]    = { "get", "getp", "sub", "unsub", "tail", "prefscan",
    "fuzzyscan", "ttl" };
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

/*
 * process incoming request from the file descriptor socket represented by
 * sock_fd, map is an array of partition, every partition store an instance
 * of hashmap, keys are distributed through consistent hashing calculated with
 * crc32 % PARTITION_NUMBER
 */
int process_command(int distributed, map_t map, char *buffer, int sock_fd, int resp_fd) {
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
            return (*cmds_func[i])(distributed, map, buffer + strlen(command) + 1);
        }
    }
    // check if the buffer contains a query and execute it
    for (int i = 0; i < queries_array_len(); i++) {
        if (strncasecmp(command, queries[i], strlen(command)) == 0) {
            return (*qrs_func[i])(distributed, map, buffer + strlen(command) + 1, sock_fd, resp_fd);
        }
    }
    // check if the buffer contains an enumeration command and execute it
    for (int i = 0; i < enumerates_array_len(); i++) {
        if (strncasecmp(command, enumerates[i], strlen(command)) == 0) {
            return (*enum_func[i])(distributed, map, sock_fd, resp_fd);
        }
    }
    // check if the buffer contains a service command and execute it
    for (int i = 0; i < services_array_len(); i++) {
        if (strncasecmp(command, services[i], strlen(command)) == 0) {
            return (*srvs_func[i])(distributed, map);
        }
    }
    return COMMAND_NOT_FOUND;
}


/* Mapping tables to the commands handlers, maintaining the order defined by
 * arrays of commands
 */
int (*cmds_func[]) (int, map_t, char *) = {
    &set_command,
    &del_command,
    &pub_command,
    &inc_command,
    &incf_command,
    &dec_command,
    &decf_command,
    &append_command,
    &prepend_command,
    &expire_command
};

int (*qrs_func[]) (int, map_t, char *, int, int) = {
    &get_command,
    &getp_command,
    &sub_command,
    &unsub_command,
    &tail_command,
    &prefscan_command,
    &fuzzyscan_command,
    &ttl_command
};

int (*enum_func[]) (int, map_t, int, int) = {
    &count_command,
    &keys_command,
    &values_command
};

int (*srvs_func[]) (int, map_t) = {
    &flush_command
};

/* utility function, concat two strings togheter */
static char *append_string(const char *str, const char *token) {
    size_t len = strlen(str) + strlen(token);
    char *ret = (char *) malloc(len * sizeof(char) + 1);
    *ret = '\0';
    return strcat(strcat(ret, str), token);
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

/* utility function, remove trailing newline */
static void remove_newline(char *str) {
    str[strcspn(str, "\r\n")] = 0;
}

/* callback function to match a given pattern by fuzzy searching it */
static int find_fuzzy_pattern(any_t t1, any_t t2) {
    char *key = (char *) t1;
    char *item_key = (char *) t2;
    int i = 0;
    int k = 0;
    int key_size = strlen(key);
    int item_size = strlen(item_key);

    while (i < key_size) {
        if(k < item_size && i < key_size) {
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
    char *stringkey = (char *) malloc(strlen(kv->key) + 1);
    bzero(stringkey, strlen(stringkey));
    strcpy(stringkey, kv->key);
    char *key_nl = append_string(stringkey, "\n");
    send(*fd, key_nl, strlen(key_nl), 0);
    return MAP_OK;
}

/* callback function to print all values inside the hashmap */
static int print_values(any_t t1, any_t t2) {
    kv_pair *kv = (kv_pair *) t2;
    int *fd = (int *) t1;
    send(*fd, kv->data, strlen(kv->data), 0);
    return MAP_OK;
}

/* SET command handler, calculate in which position of the array of the
 * int distributed, partitions the key-value pair must be stored using CRC32, overwriting in case
 * of a already taken position.
 *
 * Require two arguments:
 *
 *     SET <key> <value>
 */
int set_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1)
        arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_2) {
        char *arg_1_holder = malloc(strlen(arg_1));
        char *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        ret = m_put(map, arg_1_holder, arg_2_holder);
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
int get_command(int distributed, map_t map, char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int get = m_get(map, arg_1, &arg_2);
        if (get == MAP_OK && arg_2) {
            if (distributed == 1) {
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
        } else ret = MAP_MISSING;
    } else ret = MAP_MISSING;
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
int getp_command(int distributed, map_t map, char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        kv_pair *kv = (kv_pair *) malloc(sizeof(kv_pair));
        int get = m_get_kv_pair(map, arg_1, kv);
        if (get == MAP_OK && kv) {
            char *kvstring = (char *) malloc(strlen(kv->key) + strlen((char *) kv->data) + (sizeof(long) * 2) + 40);
            sprintf(kvstring, "key: %s\nvalue: %screation_time: %ld\nexpire_time: %ld\n",
                    kv->key, (char *) kv->data, kv->creation_time, kv->expire_time);
            if (distributed == 1) {
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
        } else ret = MAP_MISSING;
    } else ret = MAP_MISSING;
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
int del_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    while (arg_1 != NULL) {
        trim(arg_1);
        ret = m_remove(map, arg_1);
        arg_1 = strtok(NULL, " ");
    }
    return ret;
}

/*
 * SUB command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and subscribe the file descriptor to
 * it if present, return MISSING reply code otherwise.
 *
 * Require at least one argument, but accept a list as well:
 *
 *     SUB <key1> <key2> .. <keyN>
 */
int sub_command(int distributed, map_t map, char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    while (arg_1 != NULL) {
        trim(arg_1);
        ret = m_sub(map, arg_1, sock_fd);
        arg_1 = strtok(NULL, " ");
    }
    return ret;
}

/*
 * UNSUB command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and unsubscribe the file descriptor
 * to it if present, return MISSING reply code otherwise.
 *
 * Require at least one argument, but accept a list as well:
 *
 *     UNSUB <key1> <key2> .. <keyN>
 */
int unsub_command(int distributed, map_t map, char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    while (arg_1 != NULL) {
        trim(arg_1);
        ret = m_unsub(map, arg_1, sock_fd);
        arg_1 = strtok(NULL, " ");
    }
    return ret;
}

/*
 * PUB command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and publish (e.g. SET) a new value
 * to it if present, return MISSING reply code otherwise.
 * All subscribed file descriptor will receive the value as a message
 * themselves.
 *
 * Require two arguments:
 *
 *     PUB <key> <value>
 */
int pub_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_1 && arg_2) {
        char *arg_1_holder = malloc(strlen(arg_1));
        char *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        ret = m_pub(map, arg_1_holder, arg_2_holder);
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
int inc_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        ret = m_get(map, arg_1, &arg_2);
        if (ret == MAP_OK && arg_2) {
            char *s = (char *) arg_2;
            if (ISINT(s) == 1) {
                int v = GETINT(s);
                char *by = strtok(NULL, " ");
                if (by && ISINT(by)) {
                    v += GETINT(by);
                } else v++;
                sprintf(arg_2, "%d\n", v);
            } else ret = MAP_MISSING;
        }
    } else ret = MAP_MISSING;
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
int incf_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        ret = m_get(map, arg_1, &arg_2);
        if (ret == MAP_OK && arg_2) {
            char *s = (char *) arg_2;
            if (is_float(s) == 1) {
                double v = GETDOUBLE(s);
                char *by = strtok(NULL, " ");
                if (by && is_float(by)) {
                    v += GETDOUBLE(by);
                } else v += 1.0;
                sprintf(arg_2, "%lf\n", v);
            } else ret = MAP_MISSING;
        }
    } else ret = MAP_MISSING;
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
int dec_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        ret = m_get(map, arg_1, &arg_2);
        if (ret == MAP_OK && arg_2) {
            char *s = (char *) arg_2;
            if(ISINT(s) == 1) {
                int v = GETINT(s);
                char *by = strtok(NULL, " ");
                if (by != NULL && ISINT(by)) {
                    v -= GETINT(by);
                } else v--;
                sprintf(arg_2, "%d\n", v);
            } else ret = MAP_MISSING;
        }
    } else ret = MAP_MISSING;
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
int decf_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        ret = m_get(map, arg_1, &arg_2);
        if (ret == MAP_OK && arg_2) {
            char *s = (char *) arg_2;
            if(is_float(s) == 1) {
                double v = GETDOUBLE(s);
                char *by = strtok(NULL, " ");
                if (by != NULL && ISINT(by)) {
                    v -= GETDOUBLE(by);
                } else v -= 1.0;
                sprintf(arg_2, "%lf\n", v);
            } else ret = MAP_MISSING;
        }
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * COUNT command handler, count all key-value pairs stored in the hashmap and
 * write the result back to the file-descriptor.
 *
 * Doesn't require any argument.
 */
int count_command(int distributed, map_t map, int sock_fd, int resp_fd) {
    int len = 0;
    len += m_length(map);
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
int keys_command(int distributed, map_t map, int sock_fd, int resp_fd) {
    h_map *m = (h_map *) map;
    if (m->size > 0)
        m_iterate(map, print_keys, &sock_fd);
    return 1;
}

/*
 * VALUES command handler, iterate through all the keyspace and return a list of
 * the present values associated to them.
 *
 * Doesn't require any argument.
 */
int values_command(int distributed, map_t map, int sock_fd, int resp_fd) {
    h_map *m = (h_map *) map;
    if (m->size > 0)
        m_iterate(map, print_values, &sock_fd);
    return 1;
}

/*
 * TAIL command handler, works like SUB but using an index as a cursor on the
 * queue of the previous published values associated to a key, it allow to
 * iterate through the full history of the pubblications at will.
 *
 * Require at least one argument, but accept also an optional cursor that define
 * from where to start in the queue of messages associated to the 'topic' key:
 *
 *     TAIL <key>
 *     TAIL <key> 5 // leap through the first 5 messages
 */
int tail_command(int distributed, map_t map, char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_1 && arg_2) {
        char *arg_1_holder = malloc(strlen(arg_1));
        char *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        if (ISINT(arg_2_holder)) {
            int i = GETINT(arg_2_holder);
            m_sub_from(map, arg_1, sock_fd, i);
            ret = 1;
        } else ret = -1;
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * PREFSCAN command handler, scans the entire keyspace and build a list with all
 * the keys that match the prefix passed directly writing it back to the
 * file-descriptor.
 *
 * Require one argument:
 *
 *     PREFSCAN <keyprefix>
 */
int prefscan_command(int distributed, map_t map, char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int flag = 0;
        h_map *m = (h_map *) map;
        if (m->size > 0) {
            ret = m_prefscan(map, find_prefix, arg_1, sock_fd);
            if (ret == MAP_OK) flag = 1;
        }
        if (flag == 1) ret = 1;
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * FUZZYSCAN command handler, scans the entire keyspace and build a list with
 * all the keys that match the prefix according to a basic fuzzy-search
 * algorithm passed directly writing it back to the file-descriptor.
 *
 * Require one argument:
 *
 *     FUZZYSCAN <keyprefix>
 */
int fuzzyscan_command(int distributed, map_t map, char *command, int sock_fd, int resp_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int flag = 0;
        h_map *m = (h_map *) map;
        if (m->size > 0) {
            ret = m_fuzzyscan(map, find_fuzzy_pattern, arg_1, sock_fd);
            if (ret == MAP_OK) flag = 1;
        }
        /* } */
        if (flag == 1) ret = 1;
}
return ret;
}

/*
 * APPEND command handler, finds the map_tinside the partitions array using
 * CRC32 and append a suffix to the value associated to the found key, returning
 * MISSING reply code if no key were found.
 *
 * Require two arguments:
 *
 *     APPEND <key> <value>
 */
int append_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_1 && arg_2) {
        char *arg_1_holder = malloc(strlen(arg_1));
        char *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        void *val = NULL;
        ret = m_get(map, arg_1_holder, &val);
        if (ret == MAP_OK) {
            remove_newline(val);
            char *append = append_string(val, arg_2_holder);
            ret = m_put(map, arg_1_holder, append);
        }
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * PREPEND command handler, finds the map_tinside the partitions array using
 * CRC32 and prepend a prefix to the value associated to the found key, returning
 * MISSING reply code if no key were found.
 *
 * Require two arguments:
 *
 *     PREPEND <key> <value>
 */
int prepend_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_1 && arg_2) {
        char *arg_1_holder = malloc(strlen(arg_1));
        char *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        void *val = NULL;
        ret = m_get(map, arg_1_holder, &val);
        if (ret == MAP_OK) {
            remove_newline(arg_2_holder);
            char *append = append_string(arg_2_holder, val);
            ret = m_put(map, arg_1_holder, append);
        }
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * EXPIRE command handler, finds the partitions containing the keys using CRC32
 * and set the expire time specified to that key, after which it will be deleted.
 *
 * Require two arguments:
 *
 *     EXPIRE <key> <ms>
 */
int expire_command(int distributed, map_t map, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    arg_2 = arg_1 + strlen(arg_1) + 1;
    if (arg_1 && arg_2) {
        char *arg_1_holder = malloc(strlen(arg_1));
        char *arg_2_holder = malloc(strlen((char *) arg_2));
        strcpy(arg_1_holder, arg_1);
        strcpy(arg_2_holder, arg_2);
        ret = m_set_expire_time(map, arg_1_holder, (long) GETINT(arg_2_holder));
    } else ret = MAP_MISSING;
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
int ttl_command(int distributed, map_t map, char *command, int sock_fd, int resp_fd) {
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        kv_pair *kv = (kv_pair *) malloc(sizeof(kv_pair));
        int get = m_get_kv_pair(map, arg_1, kv);
        if (get == MAP_OK && kv) {
            char ttl[7];
            if (kv->has_expire_time)
                sprintf(ttl, "%ld\n", kv->expire_time / 1000);
            else
                sprintf(ttl, "%d\n", -1);
            send(sock_fd, ttl, 7, 0);
        }
    } else return MAP_MISSING;
    return 0;
}

/*
 * FLUSH command handler, delete the entire keyspace.
 *
 * Doesn't require any argument.
 */
int flush_command(int distributed, map_t map) {
    if (map != NULL)
        m_release(map);
    return OK;
}

