#include <stdio.h>
#include <string.h>
#include "unit.h"
#include "../src/map.h"


int tests_run = 0;


/*
 * Tests the creation of a map
 */
static char *test_create(void) {
    map *m = map_create();
    ASSERT("[! create]: map not created", m != NULL);
    map_release(m);
    return 0;
}


/*
 * Tests the release of a map
 */
static char *test_release(void) {
    map *m = map_create();
    map_release(m);
    ASSERT("[! release]: map not released", m->size == 0);
    return 0;
}


/*
 * Tests the insertion function of the map
 */
static char *test_put(void) {
    map *m = map_create();
    char *key = "hello";
    char *val = "world";
    int status = map_put(m, strdup(key), strdup(val));
    ASSERT("[! put]: map size = 0", m->size == 1);
    ASSERT("[! put]: put didn't work as expected", status == MAP_OK);
    map_release(m);
    return 0;
}


/*
 * Tests lookup function of the map
 */
static char *test_get(void) {
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
static char *test_del(void) {
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


static char *all_tests() {
    RUN_TEST(test_create);
    RUN_TEST(test_release);
    RUN_TEST(test_put);
    RUN_TEST(test_get);
    RUN_TEST(test_del);
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
