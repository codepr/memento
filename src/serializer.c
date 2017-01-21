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

#include <stdlib.h>
#include <string.h>
#include "serializer.h"

char *serialize(struct message msg) {
    int s_len  = strlen(msg.content);
    int t_len  = sizeof(int) + s_len;
    char *serialized = malloc(sizeof(char) * (t_len + sizeof(int)));
    char *metadata = serialized;
    char *fd = serialized + sizeof(int);
    char *content = fd + sizeof(int);
    *((int*) metadata) = s_len;
    *((int*) fd) = msg.fd;
    strncpy(content, msg.content, s_len);
    return serialized;
}

struct message deserialize(char *serialized_msg) {
    char* metadata = serialized_msg;
    char* fd = metadata + sizeof(int);
    char* content = fd + sizeof(int);
    struct message msg;
    int s_len = *((int*) metadata);
    msg.fd = *((int*) fd);
    msg.content = malloc((s_len + 1) * sizeof(char));
    strncpy(msg.content, content, s_len);
    msg.content[s_len] = '\0';
    return msg;
}
