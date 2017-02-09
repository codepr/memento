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
#define S_OK    "OK\r\n"
#define S_NIL   "(null)\r\n"
#define S_OOM   "(Out of memory)\r\n"
#define S_UNK   "(Unknown command)\r\n"


typedef enum { OK, PAYLOAD_OK, ITERATE_OK,
    MISSING, FULL, OOM, COMMAND_NOT_FOUND, END} reply_code;


int check_command(char *);
int command_handler(int, int);
int process_command(char *, int, int, unsigned int);

extern const char *commands[];
extern const char *queries[];
extern const char *enumerates[];
extern const char *services[];

int commands_array_len(void);
int queries_array_len(void);
int enumerates_array_len(void);
int services_array_len(void);

// Commands
int set_command(char *);
int del_command(char *);
int inc_command(char *);
int incf_command(char *);
int dec_command(char *);
int decf_command(char *);
int append_command(char *);
int prepend_command(char *);
// Queries
int get_command(char *, int, int, unsigned int);
int getp_command(char *, int, int, unsigned int);
int ttl_command(char *, int, int, unsigned int);
// enumerates
int count_command(int, int, unsigned int);
int keys_command(int, int, unsigned int);
int values_command(int, int, unsigned int);
// services
int flush_command(void);

extern int (*cmds_func[]) (char *);
extern int (*qrs_func[]) (char *, int, int, unsigned int);
extern int (*enum_func[]) (int, int, unsigned int);
extern int (*srvs_func[]) (void);

#endif
