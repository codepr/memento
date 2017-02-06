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

// Server responses
#define S_OK "OK\r\n"
#define S_NIL "(null)\r\n"
#define S_OOM "(Out of memory)\r\n"
#define S_UNK "(Unknown command)\r\n"

// Message bus responses
#define S_D_OK "* OK\r\n"
#define S_D_NIL "* (null)\r\n"
#define S_D_OOM "* (Out of memory)\r\n"
#define S_D_UNK "* (Unknown command)\r\n"


typedef enum { OK, PAYLOAD_OK, ITERATE_OK,
    MISSING, FULL, OOM, COMMAND_NOT_FOUND, END} reply_code;


int check_command(char *);
int command_handler(int, map *);
int process_command(int, map *, char *, int, int);

extern const char *commands[];
extern const char *queries[];
extern const char *enumerates[];
extern const char *services[];

int commands_array_len(void);
int queries_array_len(void);
int enumerates_array_len(void);
int services_array_len(void);

// Commands
int set_command(int, map *, char *);
int del_command(int, map *, char *);
int inc_command(int, map *, char *);
int incf_command(int, map *, char *);
int dec_command(int, map *, char *);
int decf_command(int, map *, char *);
int append_command(int, map *, char *);
int prepend_command(int, map *, char *);
// Queries
int get_command(int, map *, char *, int, int);
int getp_command(int, map *, char *, int, int);
int ttl_command(int, map *, char *, int, int);
// enumerates
int count_command(int, map *, int, int);
int keys_command(int, map *, int, int);
int values_command(int, map *, int, int);
// services
int flush_command(int, map *);

extern int (*cmds_func[]) (int, map *, char *);
extern int (*qrs_func[]) (int, map *, char *, int, int);
extern int (*enum_func[]) (int, map *, int, int);
extern int (*srvs_func[]) (int, map *);

#endif
