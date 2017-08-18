/*
 * Copyright (c) 2016-2017 Andrea Giacomo Baldan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <aio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "persistence.h"


//TODO: still untouched
int async_write(char *str)  {
    int file = open(PERSISTENCE_LOG, O_CREAT|O_RDWR|O_APPEND, 0644);
    if (file == -1) {
        perror("log");
        exit(1);
    }

    struct aiocb cb;

    char *buf = malloc(strlen(str));
    strcpy(buf, str);

    memset(&cb, 0, sizeof(struct aiocb));
    cb.aio_nbytes = strlen(str);
    cb.aio_fildes = file;
    cb.aio_buf = str;
    cb.aio_offset = 0;

    if (aio_write(&cb) != 0) {
        perror("aiocb");
        return -1;
    }

    while (aio_error(&cb) == EINPROGRESS) {}


    close(file);
    return 0;
}
