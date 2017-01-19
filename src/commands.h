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

#ifndef COMMANDS_H
#define COMMANDS_H

#include "map.h"

typedef enum {OK, PAYLOAD_OK, ITERATE_OK, MISSING, FULL, OOM, COMMAND_NOT_FOUND, END} reply_code;


int process_command(int, map_t, char *, int, int);

extern const char *commands[];
extern const char *queries[];
extern const char *enumerates[];
extern const char *services[];

int commands_array_len(void);
int queries_array_len(void);
int enumerates_array_len(void);
int services_array_len(void);

// Commands
int set_command(int, map_t, char *);
int del_command(int, map_t, char *);
int pub_command(int, map_t, char *);
int inc_command(int, map_t, char *);
int incf_command(int, map_t, char *);
int dec_command(int, map_t, char *);
int decf_command(int, map_t, char *);
int append_command(int, map_t, char *);
int prepend_command(int, map_t, char *);
int expire_command(int, map_t, char *);
// Queries
int get_command(int, map_t, char *, int, int);
int getp_command(int, map_t, char *, int, int);
int sub_command(int, map_t, char *, int, int);
int unsub_command(int, map_t, char *, int, int);
int tail_command(int, map_t, char *, int, int);
int prefscan_command(int, map_t, char *, int, int);
int fuzzyscan_command(int, map_t, char *, int, int);
int ttl_command(int, map_t, char *, int, int);
// enumerates
int count_command(int, map_t, int, int);
int keys_command(int, map_t, int, int);
int values_command(int, map_t, int, int);
// services
int flush_command(int, map_t);

extern int (*cmds_func[]) (int, map_t, char *);
extern int (*qrs_func[]) (int, map_t, char *, int, int);
extern int (*enum_func[]) (int, map_t, int, int);
extern int (*srvs_func[]) (int, map_t);

#endif
