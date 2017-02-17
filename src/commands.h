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

/* Server base responses */
#define S_OK    "OK\r\n"
#define S_NIL   "(null)\r\n"
#define S_OOM   "(Out of memory)\r\n"
#define S_UNK   "(Unknown command)\r\n"


typedef enum { OK, PAYLOAD_OK, ITERATE_OK,
    MISSING, FULL, OOM, COMMAND_NOT_FOUND, END} reply_code;


typedef struct {
    char *cmd;              /* command string */
    int sfd;                /* file descriptor sender, can also be a node*/
    int rfd;                /* file descriptor original sender, strictly a client */
    unsigned int fp : 1;    /* message from another node flag */
} arguments;


int check_command(char *);
int command_handler(int, int);
int process_command(char *, int, int, unsigned int);

extern const char *commands[];

int commands_array_len(void);

int set_command(arguments);
int get_command(arguments);
int del_command(arguments);
int inc_command(arguments);
int dec_command(arguments);
int ttl_command(arguments);
int incf_command(arguments);
int decf_command(arguments);
int getp_command(arguments);
int keys_command(arguments);
int count_command(arguments);
int values_command(arguments);
int expire_command(arguments);
int append_command(arguments);
int prepend_command(arguments);
int keyspace_command(arguments args);

/* Services */
int flush_command(void);

extern int (*cmds_func[]) (arguments);
extern int (*srvs_func[]) (void);

#endif
