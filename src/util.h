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

#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>


#define GETINT(x) to_int(x)
#define GETFLOAT(x) to_float(x)
#define GETDOUBLE(x) to_double(x)
#define ISINT(x) is_integer(x)
#define CRC32(c, x) crc32(c, x)
#define RANDBETWEEN(A,B) A + rand()/(RAND_MAX/(B - A))


/* Log level */
typedef enum { INFO, ERR, DEBUG } loglevel;

void *shb_malloc(size_t);
unsigned long crc32(const unsigned char *, unsigned int);
long long current_timestamp(void);
void trim(char *);
int is_integer(const char *);
int is_float(const char *);
int to_int(const char *);
double to_double(const char *);
const char *node_name(unsigned int);
const char *get_homedir(void);
char *append_string(const char *, const char *);
void remove_newline(char *);


void s_log(loglevel, const char *, ...);

#define LOG(...) s_log( __VA_ARGS__ )
#define DEBUG(...) LOG(DEBUG, __VA_ARGS__)
#define ERROR(...) LOG(ERR, __VA_ARGS__)
#define INFO(...) LOG(INFO, __VA_ARGS__)

#endif
