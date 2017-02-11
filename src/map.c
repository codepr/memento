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
int map_iterate2(map *m, func f, void *arg1) {
    /* On empty hashmap, return immediately */
    if (m->size <= 0) return MAP_ERR;
    /* Linear probing */
    for(int i = 0; i < m->table_size; i++) {
        if (m->entries[i].in_use != 0) {
            map_entry data = m->entries[i];
            int status = f(arg1, &data);
            if (status != MAP_OK) return status;
        }
    }
    return MAP_OK;
}


/*
 * Iterate the function parameter over each element in the hashmap.  The
 * additional any_t argument is passed to the function as its first
 * argument and the pair is the second.
 */
int map_iterate3(map *m, func3 f, void *arg1, void *arg2) {
    /* On empty hashmap, return immediately */
    if (m->size <= 0) return MAP_ERR;
    /* Linear probing */
    for(int i = 0; i < m->table_size; i++) {
        if (m->entries[i].in_use != 0) {
            map_entry data = m->entries[i];
            int status = f(arg1, arg2, &data);
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
    } else return MAP_ERR;

    return MAP_OK;
}

/* Deallocate the hashmap */
void map_release(map *m){
    map_iterate2(m, destroy, NULL);
    if (m) {
        if (m->entries)
            free(m->entries);
        free(m);
    }
}
