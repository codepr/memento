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

#ifndef COMMANDS_H
#define COMMANDS_H

//#include "map.h"
#include "networking.h"

/* Server base responses */
#define S_OK    "OK\r\n"
#define S_NIL   "(null)\r\n"
#define S_OOM   "(Out of memory)\r\n"
#define S_UNK   "(Unknown command)\r\n"


typedef enum { OK, PAYLOAD_OK, ITERATE_OK,
    MISSING, FULL, OOM, COMMAND_NOT_FOUND, END} reply_code;


typedef struct {
	void *data;				/* data payload */
	int sfd;				/* file descriptor sender, can also be a node */
	int rfd;				/* file descriptor original client sender */
	int size;				/* size of the payload */
	unsigned int fp : 1;	/* message from another node flag */
} reply;


typedef struct {
	char *name;						/* name of the command */
	void *(*func)(char *);			/* command implementation function */
	void *(*callback)(reply *);		/* callback handler function for result */
} command;


void *reply_default(reply *);
void *reply_data(reply *);
int check_command(char *);
int peer_command_handler(int);
int client_command_handler(int);

void *set_command(char *);
void *get_command(char *);
void *del_command(char *);
void *inc_command(char *);
void *dec_command(char *);
void *incf_command(char *);
void *decf_command(char *);
void *getp_command(char *);
void *append_command(char *);
void *prepend_command(char *);
void *flush_command(char *);

#endif
