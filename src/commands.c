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

int count_command(partition **buckets, int sock_fd) {
    int len = 0;
    for (int i = 0; i < PARTITION_NUMBER; i++)
        len += m_length(buckets[i]->map);
    char c_len[24];
    sprintf(c_len, "%d\n", len);
    send(sock_fd, c_len, 24, 0);
    return 1;
}

int key_command(partition **buckets, int sock_fd) {
    for (int i = 0; i < PARTITION_NUMBER; i++)
        if (buckets[i]->map->size > 0)
            m_iterate(buckets[i]->map, print_keys, &sock_fd);
    return 1;
}

int values_command(partition **buckets, int sock_fd) {
    for (int i = 0; i < PARTITION_NUMBER; i++)
        if (buckets[i]->map->size > 0)
            m_iterate(buckets[i]->map, print_values, &sock_fd);
    return 1;
}

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

int flush_command(partition **buckets) {
    for (int i = 0; i < PARTITION_NUMBER; i++) {
        partition_release(buckets[i]);
        buckets[i] = create_partition();
    }
    return OK;
}
