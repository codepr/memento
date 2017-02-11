#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unit.h"
#include "../src/map.h"
#include "../src/list.h"


int tests_run = 0;


/*
 * Tests the creation of a map
 */
static char *test_map_create(void) {
    map *m = map_create();
    ASSERT("[! create]: map not created", m != NULL);
    map_release(m);
    return 0;
}


/*
 * Tests the release of a map
 */
static char *test_map_release(void) {
    map *m = map_create();
    map_release(m);
    ASSERT("[! release]: map not released", m->size == 0);
    return 0;
}


/*
 * Tests the insertion function of the map
 */
static char *test_map_put(void) {
    map *m = map_create();
    char *key = "hello";
    char *val = "world";
    int status = map_put(m, strdup(key), strdup(val));
    ASSERT("[! put]: map size = 0", m->size == 1);
    ASSERT("[! put]: put didn't work as expected", status == MAP_OK);
    char *val1 = "WORLD";
    map_put(m, strdup(key), strdup(val1));
    void *ret = map_get(m, key);
    ASSERT("[! put]: put didn't update the value", strcmp(val1, ret) == 0);
    map_release(m);
    return 0;
}


/*
 * Tests lookup function of the map
 */
static char *test_map_get(void) {
    map *m = map_create();
    char *key = "hello";
    char *val = "world";
    map_put(m, strdup(key), strdup(val));
    char *ret = (char *) map_get(m, key);
    ASSERT("[! get]: get didn't work as expected", strcmp(ret, val) == 0);
    map_release(m);
    return 0;
}


/*
 * Tests the deletion function of the map
 */
static char *test_map_del(void) {
    map *m = map_create();
    char *key = "hello";
    char *val = "world";
    map_put(m, strdup(key), strdup(val));
    int status = map_del(m, key);
    ASSERT("[! del]: map size = 1", m->size == 0);
    ASSERT("[! del]: del didn't work as expected", status == MAP_OK);
    map_release(m);
    return 0;
}


/*
 * Tests the iteration of map_iterate2
 */
static int destroy_map(void *arg1, void *arg2) {
    map_entry *entry = (map_entry *) arg2;
    char *alt = "altered";
    strcpy(entry->val, alt);
    return 0;
}

static char *test_map_iterate2(void) {
    map *m = map_create();
    char *key0 = "hello";
    char *val0 = "world";
    char *key1 = "this";
    char *val1 = "time";
    map_put(m, strdup(key0), strdup(val0));
    map_put(m, strdup(key1), strdup(val1));
    map_iterate2(m, destroy_map, NULL);
    char *v0 = (char *) map_get(m, key0);
    char *v1 = (char *) map_get(m, key1);
    char *alt = "altered";
    ASSERT("[! iterate2]: iteration function is not correctly applied",
            (strncmp(v0, alt, 7) == 0) && (strncmp(v1, alt, 7) == 0));
    map_release(m);
    return 0;
}


/*
 * Tests the creation of a list
 */
static char *test_list_create(void) {
    list *l = list_create();
    ASSERT("[! create]: list not created", l != NULL);
    list_release(l);
    return 0;
}


/*
 * Tests the release of a list
 */
static char *test_list_release(void) {
    list *l = list_create();
    list_release(l);
    ASSERT("[! release]: list not released", l != NULL);
    return 0;
}


/*
 * Tests the insertion function of the list to the head node
 */
static char *test_list_head_insert(void) {
    list *l = list_create();
    char *val = "hello world";
    list *ll = list_head_insert(l, strdup(val));
    ASSERT("[! head insert]: list size = 0", l->len == 1);
    ASSERT("[! head insert]: head insert didn't work as expected", ll != NULL);
    list_release(ll);
    return 0;
}


/*
 * Tests the insertion function of the list to the tail node
 */
static char *test_list_tail_insert(void) {
    list *l = list_create();
    char *val = "hello world";
    list *ll = list_tail_insert(l, strdup(val));
    ASSERT("[! tail insert]: list size = 0", l->len == 1);
    ASSERT("[! tail insert]: tail insert didn't work as expected", ll != NULL);
    list_release(ll);
    return 0;
}


/*
 * All datastructure tests
 */
static char *all_tests() {
    RUN_TEST(test_map_create);
    RUN_TEST(test_map_release);
    RUN_TEST(test_map_put);
    RUN_TEST(test_map_get);
    RUN_TEST(test_map_del);
    RUN_TEST(test_map_iterate2);
    RUN_TEST(test_list_create);
    RUN_TEST(test_list_release);
    RUN_TEST(test_list_head_insert);
    RUN_TEST(test_list_tail_insert);

    return 0;
}

int main(int argc, char **argv) {
    char *result = all_tests();
    if (result != 0) {
        printf(" %s\n", result);
    }
    else {
        printf("\n [*] ALL TESTS PASSED\n");
    }
    printf(" [*] Tests run: %d\n\n", tests_run);

    return result != 0;
}
