#ifndef UNIT_H
#define UNIT_H

#define ASSERT(message, test) do { if (!(test)) return message; } while (0)

#define RUN_TEST(test) do { char *message = test(); tests_run++; \
        if (message) return message; } while (0)

extern int tests_run;

#endif
