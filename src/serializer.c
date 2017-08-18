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


#include <stdlib.h>
#include <string.h>
#include "serializer.h"


/*
 * Pack a message structure in an array of bytes, following a simple convention:
 *
 * [ 4 bytes ] | [ 4 bytes ] | [ 2 bytes ] | [ content len bytes ]
 * ----------- | ----------- | ----------- | --------------------
 *  cont. len  |  desc. len  |  flag len   |  remanining content
 * ----------- | ----------- | ----------- | --------------------
 *
 *  This way it's easier to pass it around through sockets
 */
char *serialize(struct message msg) {
    int mlen = strlen(msg.content);
    int flen = sizeof(int) + sizeof(unsigned int) + mlen ; // structure whole len
    char *serialized = malloc(sizeof(char) * (flen + sizeof(int))); // serialization whole len
    char *metadata = serialized;
    char *fd = serialized + sizeof(int);        // space for descriptor
    char *fp = fd + sizeof(int);                // space for flag
    char *content = fp + sizeof(unsigned int);  // space for content message
    *((int*) metadata) = mlen;
    *((int*) fd) = msg.fd;
    *((unsigned int*) fp) = msg.ready;
    strncpy(content, msg.content, mlen);
    return serialized;
}


/*
 * Unpack an array of bytes reconstructing the original message structure
 */
struct message deserialize(char *serialized_msg) {
    char *metadata = serialized_msg;
    char *fd = metadata + sizeof(int);
    char *fp = fd + sizeof(int);
    char *content = fp + sizeof(unsigned int);
    struct message msg;
    int mlen = *((int *) metadata);
    msg.fd = *((int *) fd);
    msg.ready = *((unsigned int *) fp);
    msg.content = malloc((mlen + 1) * sizeof(char));
    strncpy(msg.content, content, mlen);
    /* msg.content = content; */
    msg.content[mlen] = '\0';
    return msg;
}
