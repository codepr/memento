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
#include <time.h>
#include <sys/socket.h>
#include "networking.h"
#include "util.h"


int main(int argc, char **argv) {

    char *host = "127.0.0.1";
    char *port = "8082";

    if (argc > 2) {
        host = argv[1];
        port = argv[2];
    }

    int fd = connectto(host, port);
    char real_keys[100000][16];
    char buf[5];
    float time_elapsed, start_time, end_time, result;

    for (int j = 0; j < 100000; ++j)
        snprintf(real_keys[j], 16, "set %d val\r\n", j);

    memset(buf, 0x00, 5);

    /* `SET keyone valueone` benchmark */

    char *cmd = "set keyone valueone\r\n";
    start_time = (float) clock()/CLOCKS_PER_SEC;
    for (int i = 0; i < 100000; ++i) {
        if (send(fd, cmd, strlen(cmd), 0) < 0)
            perror("send");
        if (read(fd, buf, 5) < 0)
            perror("read");
    }

    end_time = (float)clock()/CLOCKS_PER_SEC;
    time_elapsed = end_time - start_time;

    printf("* Elapsed time: %f\n", time_elapsed);
    result = 100000 / time_elapsed;
    printf("* SET: %.2f ops\n", result);

    sleep(1);

    /* `GET keyone` benchmark */

    char getr[10];
    memset(getr, 0x00, 10);
    char *get = "get keyone\r\n";
    start_time = (float) clock()/CLOCKS_PER_SEC;
    for (int i = 0; i < 100000; ++i) {
        if (send(fd, get, strlen(get), 0) < 0)
            perror("send");
        if (read(fd, getr, 10) < 0)
            perror("read");
    }

    end_time = (float)clock()/CLOCKS_PER_SEC;
    time_elapsed = end_time - start_time;
    printf("* Elapsed time: %f\n", time_elapsed);
    result = 100000 / time_elapsed;
    printf("* GET: %.2f ops\n", result);

    sleep(1);

    /* `SET xxxxx val` benchmark */

    start_time = (float) clock()/CLOCKS_PER_SEC;
    for (int i = 0; i < 100000; ++i) {
        if (send(fd, real_keys[i], 16, 0) < 0)
            perror("send");
        if (read(fd, buf, 5) < 0)
            perror("read");
    }

    end_time = (float)clock()/CLOCKS_PER_SEC;
    time_elapsed = end_time - start_time;
    printf("* Elapsed time: %f\n", time_elapsed);
    result = 100000 / time_elapsed;
    printf("* SET: %.2f ops\n", result);

    return 0;
}
