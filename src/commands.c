#include "commands.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

const char *commands[]   = { "set", "del", "pub", "inc", "incf", "dec", "decf",
                             "append", "prepend", "expire" };

const char *queries[]    = { "get", "getp", "sub", "unsub", "tail", "prefscan",
                             "fuzzyscan", "ttl" };

const char *enumerates[] = { "count", "key", "values" };
const char *services[]   = {"flush"};

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

int (*cmds_func[]) (partition **, char *) = {
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

int (*qrs_func[]) (partition **, char *, int) = {
    &get_command,
    &getp_command,
    &sub_command,
    &unsub_command,
    &tail_command,
    &prefscan_command,
    &fuzzyscan_command,
    &ttl_command
};

int (*enum_func[]) (partition **, int) = {
    &count_command,
    &key_command,
    &values_command
};

int (*srvs_func[]) (partition **) = {
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
 * partitions the key-value pair must be stored using CRC32, overwriting in case
 * of a already taken position
 */
int set_command(partition **buckets, char *command) {
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
        int p_index = partition_hash(arg_1_holder);
        ret = m_put(buckets[p_index]->map, arg_1_holder, arg_2_holder);
    }
    return ret;
}

/*
 * GET command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32, write back the value associated to
 * it if present, MISSING reply code otherwise
 */
int get_command(partition **buckets, char *command, int sock_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        int get = m_get(buckets[p_index]->map, arg_1, &arg_2);
        if (get == MAP_OK && arg_2) {
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
 * creation time, MISSING reply code otherwise
 */
int getp_command(partition **buckets, char *command, int sock_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        kv_pair *kv = (kv_pair *) malloc(sizeof(kv_pair));
        int get = m_get_kv_pair(buckets[p_index]->map, arg_1, kv);
        if (get == MAP_OK && kv) {
            char *kvstring = (char *) malloc(strlen(kv->key) + strlen((char *) kv->data) + (sizeof(long) * 2) + 40);
            sprintf(kvstring, "key: %s\nvalue: %screation_time: %ld\nexpire_time: %ld\n",
                    kv->key, (char *) kv->data, kv->creation_time, kv->expire_time);
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
 * MISSING reply code otherwise
 */
int del_command(partition **buckets, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    while (arg_1 != NULL) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        ret = m_remove(buckets[p_index]->map, arg_1);
        arg_1 = strtok(NULL, " ");
    }
    return ret;
}

/*
 * SUB command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and subscribe the file descriptor to
 * it if present, return MISSING reply code otherwise
 */
int sub_command(partition **buckets, char *command, int sock_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    while (arg_1 != NULL) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        ret = m_sub(buckets[p_index]->map, arg_1, sock_fd);
        arg_1 = strtok(NULL, " ");
    }
    return ret;
}

/*
 * UNSUB command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and unsubscribe the file descriptor
 * to it if present, return MISSING reply code otherwise
 */
int unsub_command(partition **buckets, char *command, int sock_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    while (arg_1 != NULL) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        ret = m_unsub(buckets[p_index]->map, arg_1, sock_fd);
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
 */
int pub_command(partition **buckets, char *command) {
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
        int p_index = partition_hash(arg_1_holder);
        ret = m_pub(buckets[p_index]->map, arg_1_holder, arg_2_holder);
    }
    return ret;
}

/*
 * INC command handler, calculate in which position of the array of the
 * partitions the key is stored using CRC32 and increment (add 1) to the integer
 * value to it if present, return MISSING reply code otherwise.
 */
int inc_command(partition **buckets, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        ret = m_get(buckets[p_index]->map, arg_1, &arg_2);
        if (ret == MAP_OK && arg_2) {
            char *s = (char *) arg_2;
            if (is_integer(s) == 1) {
                int v = to_int(s);
                char *by = strtok(NULL, " ");
                if (by && is_integer(by)) {
                    v += to_int(by);
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
 */
int incf_command(partition **buckets, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        ret = m_get(buckets[p_index]->map, arg_1, &arg_2);
        if (ret == MAP_OK && arg_2) {
            char *s = (char *) arg_2;
            if (is_float(s) == 1) {
                double v = to_double(s);
                char *by = strtok(NULL, " ");
                if (by && is_float(by)) {
                    v += to_double(by);
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
 */
int dec_command(partition **buckets, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        ret = m_get(buckets[p_index]->map, arg_1, &arg_2);
        if (ret == MAP_OK && arg_2) {
            char *s = (char *) arg_2;
            if(is_integer(s) == 1) {
                int v = to_int(s);
                char *by = strtok(NULL, " ");
                if (by != NULL && is_integer(by)) {
                    v -= to_int(by);
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
 */
int decf_command(partition **buckets, char *command) {
    int ret = 0;
    char *arg_1 = NULL;
    void *arg_2 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        ret = m_get(buckets[p_index]->map, arg_1, &arg_2);
        if (ret == MAP_OK && arg_2) {
            char *s = (char *) arg_2;
            if(is_float(s) == 1) {
                double v = to_double(s);
                char *by = strtok(NULL, " ");
                if (by != NULL && is_integer(by)) {
                    v -= to_double(by);
                } else v -= 1.0;
                sprintf(arg_2, "%lf\n", v);
            } else ret = MAP_MISSING;
        }
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * COUNT command handler, count all key-value pairs stored in the hashmap and
 * write the result back to the file-descriptor
 */
int count_command(partition **buckets, int sock_fd) {
    int len = 0;
    for (int i = 0; i < PARTITION_NUMBER; i++)
        len += m_length(buckets[i]->map);
    char c_len[24];
    sprintf(c_len, "%d\n", len);
    send(sock_fd, c_len, 24, 0);
    return 1;
}

/*
 * KEYS command handler, iterate through all the keyspace and return a list of
 * the present keys.
 */
int key_command(partition **buckets, int sock_fd) {
    for (int i = 0; i < PARTITION_NUMBER; i++)
        if (buckets[i]->map->size > 0)
            m_iterate(buckets[i]->map, print_keys, &sock_fd);
    return 1;
}

/*
 * VALUES command handler, iterate through all the keyspace and return a list of
 * the present values associated to them.
 */
int values_command(partition **buckets, int sock_fd) {
    for (int i = 0; i < PARTITION_NUMBER; i++)
        if (buckets[i]->map->size > 0)
            m_iterate(buckets[i]->map, print_values, &sock_fd);
    return 1;
}

/*
 * TAIL command handler, works like SUB but using an index as a cursor on the
 * queue of the previous published values associated to a key, it allow to
 * iterate through the full history of the pubblications at will.
 */
int tail_command(partition **buckets, char *command, int sock_fd) {
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
        if (is_integer(arg_2_holder)) {
            int i = to_int(arg_2_holder);
            int p_index = partition_hash(arg_1_holder);
            m_sub_from(buckets[p_index]->map, arg_1, sock_fd, i);
            ret = 1;
        } else ret = -1;
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * PREFSCAN command handler, scans the entire keyspace and build a list with all
 * the keys that match the prefix passed directly writing it back to the
 * file-descriptor
 */
int prefscan_command(partition **buckets, char *command, int sock_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int flag = 0;
        for (int i = 0; i < PARTITION_NUMBER; i++) {
            if (buckets[i]->map->size > 0) {
                ret = m_prefscan(buckets[i]->map, find_prefix, arg_1, sock_fd);
                if (ret == MAP_OK) flag = 1;
            }
        }
        if (flag == 1) ret = 1;
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * FUZZYSCAN command handler, scans the entire keyspace and build a list with
 * all the keys that match the prefix according to a basic fuzzy-search
 * algorithm passed directly writing it back to the file-descriptor
 */
int fuzzyscan_command(partition **buckets, char *command, int sock_fd) {
    int ret = 0;
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int flag = 0;
        for (int i = 0; i < PARTITION_NUMBER; i++) {
            if (buckets[i]->map->size > 0) {
                ret = m_fuzzyscan(buckets[i]->map, find_fuzzy_pattern, arg_1, sock_fd);
                if (ret == MAP_OK) flag = 1;
            }
        }
        if (flag == 1) ret = 1;
    }
    return ret;
}

/*
 * APPEND command handler, finds the partition inside the partitions array using
 * CRC32 and append a suffix to the value associated to the found key, returning
 * MISSING reply code if no key were found.
 */
int append_command(partition **buckets, char *command) {
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
        int p_index = partition_hash(arg_1_holder);
        void *val = NULL;
        ret = m_get(buckets[p_index]->map, arg_1_holder, &val);
        if (ret == MAP_OK) {
            remove_newline(val);
            char *append = append_string(val, arg_2_holder);
            ret = m_put(buckets[p_index]->map, arg_1_holder, append);
        }
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * PREPEND command handler, finds the partition inside the partitions array using
 * CRC32 and prepend a prefix to the value associated to the found key, returning
 * MISSING reply code if no key were found.
 */
int prepend_command(partition **buckets, char *command) {
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
        int p_index = partition_hash(arg_1_holder);
        void *val = NULL;
        ret = m_get(buckets[p_index]->map, arg_1_holder, &val);
        if (ret == MAP_OK) {
            remove_newline(arg_2_holder);
            char *append = append_string(arg_2_holder, val);
            ret = m_put(buckets[p_index]->map, arg_1_holder, append);
        }
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * EXPIRE command handler, finds the partitions containing the keys using CRC32
 * and set the expire time specified to that key, after which it will be deleted
 */
int expire_command(partition **buckets, char *command) {
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
        int p_index = partition_hash(arg_1_holder);
        ret = m_set_expire_time(buckets[p_index]->map, arg_1_holder, (long) to_int(arg_2_holder));
    } else ret = MAP_MISSING;
    return ret;
}

/*
 * TTL command handler, finds the partitions containing the keys using CRC32 and
 * get the expire time specified for that key, after which it will be deleted
 */
int ttl_command(partition **buckets, char *command, int sock_fd) {
    char *arg_1 = NULL;
    arg_1 = strtok(command, " ");
    if (arg_1) {
        trim(arg_1);
        int p_index = partition_hash(arg_1);
        kv_pair *kv = (kv_pair *) malloc(sizeof(kv_pair));
        int get = m_get_kv_pair(buckets[p_index]->map, arg_1, kv);
        if (get == MAP_OK && kv) {
            char ttl[7];
            if (kv->has_expire_time)
                sprintf(ttl, "%ld\n", kv->expire_time / 1000);
            else
                sprintf(ttl, "%d\n", -1);
            send(sock_fd, ttl, 7, 0);
        }
    }
    return 0;
}

/*
 * FLUSH command handler, delete the entire keyspace
 */
int flush_command(partition **buckets) {
    for (int i = 0; i < PARTITION_NUMBER; i++) {
        partition_release(buckets[i]);
        buckets[i] = create_partition();
    }
    return OK;
}
