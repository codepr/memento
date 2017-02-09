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
#include "map.h"
#include "util.h"


/*
 * Hashing function for a string
 */
static unsigned int hashmap_hash_int(map *m, char *keystr) {

    unsigned long key = CRC32((unsigned char *) (keystr), strlen(keystr));

    /* Robert Jenkins' 32 bit Mix Function */
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);

    /* Knuth's Multiplicative Method */
    key = (key >> 3) * 2654435761;

    return key % m->table_size;
}


/*
 * Return the integer of the location in entries to store the point to the item,
 * or MAP_FULL.
 */
static int hashmap_hash(map *in, void *key) {
    /* If full, return immediately */
    if (in->size >= (in->table_size / 2)) return MAP_FULL;
    /* Find the best index */
    int curr = hashmap_hash_int(in, key);
    /* Linear probing */
    for(int i = 0; i < MAX_CHAIN_LENGTH; i++){
        if (in->entries[curr].in_use == 0)
            return curr;

        if (in->entries[curr].in_use == 1 && (strcmp(in->entries[curr].key, key) == 0))
            return curr;

        curr = (curr + 1) % in->table_size;
    }

    return MAP_FULL;
}

/* Auxiliary function to determine wether a descriptor is already inside a
 * subscriber array, return the index position if it is contained, -1 otherwise
 */
/* static int already_in(int *arr, int fd, int size) { */
/*     for(int i = 0; i < size; i++) { */
/*         if (arr[i] == fd) return i; */
/*     } */
/*     return -1; */
/* } */

/*
 * Doubles the size of the hashmap, and rehashes all the elements
 */
static int hashmap_rehash(map *m) {
    int old_size;
    map_entry* curr;

    /* Setup the new elements */
    map_entry* temp = (map_entry *) calloc(2 * m->table_size, sizeof(map_entry));
    if (!temp) return MAP_ERR;

    /* Update the array */
    curr = m->entries;
    m->entries = temp;

    /* Update the size */
    old_size = m->table_size;
    m->table_size = 2 * m->table_size;
    m->size = 0;

    /* Rehash the elements */
    for(int i = 0; i < old_size; i++) {
        int status;

        if (curr[i].in_use == 0)
            continue;

        status = map_put(m, curr[i].key, curr[i].val);
        if (status != MAP_OK)
            return status;
        /* update subscribers */
        /* if (curr[i].last_subscriber > 0) { */
        /*     map_entry *p = (map_entry *) malloc(sizeof(map_entry)); */
        /*     m_get_map_entry(m, curr[i].key, p); */
        /*     for (int k = 0; k < curr[i].last_subscriber; k++) { */
        /*         m_sub(m, curr[i].key, curr[i].subscribers[k]); */
        /*     } */
        /* } */
        /* update data history as well */
        /* iterator it = curr[i].data_history->front; */
        /* while(it) { */
        /*     m_pub(m, curr[i].key, it->data); */
        /*     it = it->next; */
        /* } */
    }
    free(curr);
    return MAP_OK;
}


/*
 * Return an empty hashmap, or NULL on failure. The newly create hashmap is
 * dynamically allocated on the heap memory, so it must be released manually.
 */
map *map_create(void) {
    map *m = (map *) shb_malloc(sizeof(map));
    if(!m) return NULL;

    m->entries = (map_entry *) calloc(INITIAL_SIZE, sizeof(map_entry));
    if(!m->entries) {
        if (m) map_release(m);
        return NULL;
    }

    m->table_size = INITIAL_SIZE;
    m->size = 0;

    return m;
}


/*
 * Add a pointer to the hashmap with some key
 */
int map_put(map *m, void *key, void *val) {
    /* Find a place to put our value */
    int index = hashmap_hash(m, key);
    while(index == MAP_FULL){
        if (hashmap_rehash(m) == MAP_ERR) return MAP_ERR;
        index = hashmap_hash(m, key);
    }
    /* Set the entries */
    m->entries[index].val = val;
    m->entries[index].key = key;
    if (m->entries[index].in_use == 0) {
        m->entries[index].in_use = 1;
        m->entries[index].has_expire_time = 0;
        m->entries[index].expire_time = -1;
        m->entries[index].creation_time = current_timestamp();
        m->size++;
        m->entries[index].last_subscriber = 0;
        m->entries[index].subscribers = (int *) malloc(sizeof(int) * SUBSCRIBER_SIZE);
        m->entries[index].data_history = create_queue();
    }

    return MAP_OK;
}


/*
 * Get your pointer out of the hashmap with a key
 */
void *map_get(map *m, void *key) {
    /* Find data location */
    int curr = hashmap_hash_int(m, key);
    /* Linear probing, if necessary */
    for(int i = 0; i < MAX_CHAIN_LENGTH; i++){
        if (m->entries[curr].in_use == 1) {
            if (strcmp(m->entries[curr].key, key) == 0)
                return (m->entries[curr].val);
        }
        curr = (curr + 1) % m->table_size;
    }
    /* Not found */
    return NULL;
}


/*
 * Return the key-value pair represented by a key in the map
 */
map_entry *map_get_entry(map *m, void *key) {
    /* Find data location */
    int curr = hashmap_hash_int(m, key);

    /* Linear probing, if necessary */
    for(int i = 0; i < MAX_CHAIN_LENGTH; i++) {
        if (m->entries[curr].in_use == 1) {
            if (strcmp(m->entries[curr].key, key) == 0)
                return &m->entries[curr];
        }

        curr = (curr + 1) % m->table_size;
    }
    /* Not found */
    return NULL;
}


/*
 * Remove an element with that key from the map
 */
int map_del(map *m, void *key) {
    /* Find key */
    int curr = hashmap_hash_int(m, key);
    /* Linear probing, if necessary */
    for(int i = 0; i < MAX_CHAIN_LENGTH; i++) {
        // check wether the position in array is in use
        int in_use = m->entries[curr].in_use;
        if (in_use == 1) {
            if (strcmp(m->entries[curr].key, key) == 0) {
                /* Blank out the fields */
                m->entries[curr].in_use = 0;
                m->entries[curr].has_expire_time = 0;
                m->entries[curr].expire_time = -1;
                m->entries[curr].creation_time = -1;
                free(m->entries[curr].subscribers);
                release_queue(m->entries[curr].data_history);
                m->entries[curr].last_subscriber = 0;
                /* Reduce the size */
                m->size--;
                return MAP_OK;
            }
        }
        curr = (curr + 1) % m->table_size;
    }
    /* Data not found */
    return MAP_ERR;
}


/*
 * Iterate the function parameter over each element in the hashmap.  The
 * additional any_t argument is passed to the function as its first
 * argument and the pair is the second.
 */
int map_iterate(map *m, func f, void *item) {
    /* On empty hashmap, return immediately */
    if (m->size <= 0) return MAP_ERR;
    /* Linear probing */
    for(int i = 0; i < m->table_size; i++) {
        if (m->entries[i].in_use != 0) {
            map_entry data = m->entries[i];
            int status = f(item, &data);
            if (status != MAP_OK) return status;
        }
    }
    return MAP_OK;
}


/* callback function used with iterate to clean up the hashmap */
static int destroy(void *t1, void *t2) {

    map_entry *kv = (map_entry *) t2;

    if (kv) {
        // free key field
        if (kv->key)
            free(kv->key);
        // free value field
        if (kv->val)
            free(kv->val);
        // data history queue
        if (kv->data_history)
            release_queue(kv->data_history);
        // free subscribers array
        if (kv->subscribers)
            free(kv->subscribers);
    } else return MAP_ERR;
    return MAP_OK;
}

/* Deallocate the hashmap */
void map_release(map *m){
    map_iterate(m, destroy, NULL);
    if (m) {
        if (m->entries)
            free(m->entries);
        free(m);
    }
}



/* Publish data to a key, these data are sent to all the subscribers already
 * inside in the subscribers array of the pair represented by the key
 */
/* int m_pub(map_t in, char* key, any_t value) { */
/*     int index; */
/*     h_map* m = (h_map *) in; */
/*     #<{(| Find a place to put our value |)}># */
/*     index = hashmap_hash(in, key); */
/*     while(index == MAP_FULL){ */
/*         if (hashmap_rehash(in) == MAP_OMEM) { */
/*             return MAP_OMEM; */
/*         } */
/*         index = hashmap_hash(in, key); */
/*     } */
/*     #<{(| Set the data |)}># */
/*     m->data[index].data = value; */
/*     m->data[index].key = key; */
/*     if (m->data[index].in_use == 0) { */
/*         m->data[index].in_use = 1; */
/*         m->data[index].has_expire_time = 0; */
/*         m->data[index].expire_time = -1; */
/*         m->data[index].creation_time = current_timestamp(); */
/*         m->size++; */
/*         m->data[index].last_subscriber = 0; */
/*         m->data[index].subscribers = (int *) malloc(sizeof(int) * 64); */
/*         m->data[index].data_history = create_queue(); */
/*     } */
/*     enqueue(m->data[index].data_history, value); */
/*     for(int i = 0; i < m->data[index].last_subscriber; i++) { */
/*         send(m->data[index].subscribers[i], m->data[index].data, strlen((char *) m->data[index].data), 0); */
/*     } */
/*     return MAP_OK; */
/* } */

/*
 * Get key-value pair represented by key in the hashmap
 */
/* int m_set_expire_time(map_t in, char *key, long expire_time) { */
/*     int curr; */
/*     h_map* m = (h_map *) in; */
/*     #<{(| Find data location |)}># */
/*     curr = hashmap_hash_int(m, key); */
/*     #<{(| Linear probing, if necessary |)}># */
/*     for(int i = 0; i < MAX_CHAIN_LENGTH; i++){ */
/*         int in_use = m->data[curr].in_use; */
/*         if (in_use == 1) { */
/*             if (strcmp(m->data[curr].key, key) == 0) { */
/*                 if (expire_time == 0) { */
/*                     m->data[curr].expire_time = -1; */
/*                     m->data[curr].has_expire_time = 0; */
/*                 } else { */
/*                     m->data[curr].expire_time = expire_time; */
/*                     m->data[curr].has_expire_time = 1; */
/*                     m->data[curr].creation_time = current_timestamp(); */
/*                 } */
/*                 return MAP_OK; */
/*             } */
/*         } */
/*  */
/*         curr = (curr + 1) % m->table_size; */
/*     } */
/*  */
/*     #<{(| Not found |)}># */
/*     return MAP_MISSING; */
/* } */
/*  */
/* #<{(| */
/*  * subscribe to a key in the keyspace, adding a file descriptor representing a */
/*  * socket to the array of subscribers of the pair identified by key. */
/*  |)}># */
/* int m_sub(map_t in, char *key, int fd) { */
/*     int curr, last; */
/*     h_map *m = (h_map *) in; */
/*     curr = hashmap_hash_int(m, key); */
/*     #<{(| Linear probing, if necessary |)}># */
/*     for(int i = 0; i < MAX_CHAIN_LENGTH; i++){ */
/*         int in_use = m->data[curr].in_use; */
/*         if (in_use == 1) { */
/*             if (strcmp(m->data[curr].key, key) == 0) { */
/*                 last = m->data[curr].last_subscriber; */
/*                 if(already_in(m->data[curr].subscribers, fd, last) == -1) { */
/*                     m->data[curr].subscribers[last] = fd; */
/*                     m->data[curr].last_subscriber++; */
/*                 } */
/*                 return MAP_OK; */
/*             } */
/*         } */
/*         curr = (curr + 1) % m->table_size; */
/*     } */
/*     #<{(| Not found |)}># */
/*     return MAP_MISSING; */
/* } */
/*  */
/* #<{(| */
/*  * unsubscribe from a key removin the file descriptor representing the socket */
/*  * from the array of subscribers of the pair identified by keyspace */
/*  |)}># */
/* int m_unsub(map_t in, char *key, int fd) { */
/*     int curr, last; */
/*     h_map *m = (h_map *) in; */
/*     curr = hashmap_hash_int(m, key); */
/*     #<{(| Linear probing, if necessary |)}># */
/*     for(int i = 0; i < MAX_CHAIN_LENGTH; i++){ */
/*         int in_use = m->data[curr].in_use; */
/*         if (in_use == 1) { */
/*             if (strcmp(m->data[curr].key, key) == 0) { */
/*                 last = m->data[curr].last_subscriber; */
/*                 int index = already_in(m->data[curr].subscribers, fd, last); */
/*                 if(index != -1) { */
/*                     // ordering doesn't matter */
/*                     m->data[curr].subscribers[index] = m->data[curr].subscribers[last - 1]; */
/*                     m->data[curr].last_subscriber--; */
/*                 } */
/*                 return MAP_OK; */
/*             } */
/*         } */
/*         curr = (curr + 1) % m->table_size; */
/*     } */
/*  */
/*     #<{(| Not found |)}># */
/*     return MAP_MISSING; */
/* } */
/*  */
/* #<{(| */
/*  * subscribe to a key in the keyspace, and consume all values previously */
/*  * published to that key according to an offset giving indication on where to */
/*  * start read the depletion. */
/*  |)}># */
/* int m_sub_from(map_t in, char *key, int fd, int index) { */
/*     int curr, last; */
/*     h_map *m = (h_map *) in; */
/*     curr = hashmap_hash_int(m, key); */
/*     #<{(| Linear probing, if necessary |)}># */
/*     for(int i = 0; i < MAX_CHAIN_LENGTH; i++){ */
/*         int in_use = m->data[curr].in_use; */
/*         if (in_use == 1){ */
/*             if (strcmp(m->data[curr].key, key) == 0) { */
/*                 last = m->data[curr].last_subscriber; */
/*                 if (already_in(m->data[curr].subscribers, fd, last) == -1) { */
/*                     m->data[curr].subscribers[last] = fd; */
/*                     m->data[curr].last_subscriber++; */
/*                 } */
/*                 if ((size_t) index < m->data[curr].data_history->last && index >= 0) { */
/*                     iterator it = m->data[curr].data_history->front; */
/*                     while(it) { */
/*                         if (index == 0) { */
/*                             int len = strlen((char *) it->data); */
/*                             send(fd, it->data, len, 0); */
/*                         } */
/*                         else index--; */
/*                         it = it->next; */
/*                     } */
/*                 } */
/*                 return MAP_OK; */
/*             } */
/*         } */
/*         curr = (curr + 1) % m->table_size; */
/*     } */
/*     #<{(| Not found |)}># */
/*     return MAP_MISSING; */
/* } */
/*  */
/*
 * Iterate the function parameter over each element in the hashmap. The
 * additional any_t argument is passed to the function as its first argument and
 * the key of the current pair is the second, used to perform a prefscan
 * operation. A prefscan operation return all keys that starts with a given
 * prefix.
 */
/* int m_prefscan(map_t in, func f, any_t item, int fd) { */
/*     int flag = 0; */
/*     #<{(| Cast the hashmap |)}># */
/*     h_map* m = (h_map*) in; */
/*     #<{(| On empty hashmap, return immediately |)}># */
/*     if (m_length(m) <= 0) */
/*         return MAP_MISSING; */
/*     #<{(| Linear probing |)}># */
/*     for(int i = 0; i < m->table_size; i++) */
/*         if (m->data[i].in_use != 0) { */
/*             map_entry data = m->data[i]; */
/*             int status = f(item, data.key); */
/*             if (status == MAP_OK) { */
/*                 flag = 1; */
/*                 send(fd, data.data, strlen(data.data), 0); */
/*             } else if (flag == 0) return status; */
/*         } */
/*  */
/*     return MAP_OK; */
/* } */
/*  */
/* #<{(| */
/*  * Iterate the function parameter over each element in the hashmap. The */
/*  * additional any_t argument is passed to the function as its first argument and */
/*  * the key of the current pair is the second, used to perform a fuzzyscan */
/*  * operation. A fuzzyscan operation return all keys containing a pattern by */
/*  * fuzzy search. */
/*  |)}># */
/* int m_fuzzyscan(map_t in, func f, any_t item, int fd) { */
/*     int status, flag = 0; */
/*     #<{(| Cast the hashmap |)}># */
/*     h_map* m = (h_map*) in; */
/*     #<{(| On empty hashmap, return immediately |)}># */
/*     if (m_length(m) <= 0) */
/*         return MAP_MISSING; */
/*     #<{(| Linear probing |)}># */
/*     for(int i = 0; i < m->table_size; i++) */
/*         if (m->data[i].in_use != 0) { */
/*             map_entry data = m->data[i]; */
/*             status = f(item, data.key); */
/*             if (status == MAP_OK) { */
/*                 flag = 1; */
/*                 send(fd, data.data, strlen(data.data), 0); */
/*             } */
/*         } */
/*  */
/*     if (flag == 0) return status; */
/*     return MAP_OK; */
/* } */

/* Return the length of the hashmap */
/* int m_length(map_t in){ */
/*     h_map* m = (h_map *) in; */
/*     if (m != NULL) return m->size; */
/*     else return 0; */
/* } */
