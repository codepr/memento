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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include "commands.h"
#include "networking.h"
#include "serializer.h"
#include "hashing.h"
#include "cluster.h"
#include "util.h"


/* Private functions declarations */
// static void get_clusterinfo(int sfd);


command command_entries[] = {
	{"set", set_command, reply_default},
	{"get", get_command, reply_data},
	{"del", del_command, reply_default},
	{"inc", inc_command, reply_default},
	{"dec", dec_command, reply_default},
	{"incf", incf_command, reply_default},
	{"decf", decf_command, reply_default},
	{"append", append_command, reply_default},
	{"prepend", prepend_command, reply_default},
	{"getp", getp_command, reply_data},
	{"flush", flush_command, reply_default}
};


int commands_len(void) {
    return sizeof(command_entries) / sizeof(command);
}


void *reply_data(reply *rep) {
	peer_t *p = (peer_t *) malloc(sizeof(peer_t));
	p->fd = rep->sfd;
	p->alloc = 0;
	if (rep->data == NULL)
		rep->data = S_NIL;

	if (instance.cluster_mode == 1 && rep->fp == 1) {
	    /* adding some informations about the node host */
	    char response[strlen((char *) rep->data) + strlen(self.addr) + strlen(self.name) + 9];
	    sprintf(response, "%s:%s:%d> %s", self.name, self.addr, self.port, (char *) rep->data);
	    struct message m = { response, rep->rfd, 1 };
	    char *payload = serialize(m);
		p->data = payload;
		p->size = strlen(response) + S_OFFSET;
		p->tocli = 0;
		DEBUG("Reply data to peer\n");
		schedule_write(p);
	}
	else {
		p->data = rep->data;
	 	p->size = strlen(rep->data);
	 	p->tocli = 1;
	 	DEBUG("Reply data to client %s\n", rep->data);
	    schedule_write(p);
	}
	return NULL;
}


void *reply_default(reply *rep) {
	peer_t *p = (peer_t *) malloc(sizeof(peer_t));
	int resp = *((int *) rep->data);

	if (rep->fp == 1) {
		int len = 0;
		char *payload = NULL;
		struct message msg;
		switch (resp) {
			case MAP_OK:
				msg = (struct message) { S_OK, rep->rfd, 1 };
				len = strlen(S_OK) + S_OFFSET;
				break;
			case MAP_ERR:
				msg = (struct message) { S_NIL, rep->rfd, 1 };
				len = strlen(S_NIL) + S_OFFSET;
				break;
			case COMMAND_NOT_FOUND:
				msg = (struct message) { S_UNK, rep->rfd, 1 };
				len = strlen(S_UNK) + S_OFFSET;
				break;
			case END:
				return NULL;
				break;
			default:
				break;
		}
		payload = serialize(msg);
		p->fd = rep->sfd;
		p->data = payload;
		p->size = len;
		p->alloc = 1;
		p->tocli = 0;
		DEBUG("Response to peer node at %d with %s\n", rep->sfd, msg.content);
	}
	else {
		switch (resp) {
			case MAP_OK:
				p->data = S_OK;
				break;
			case MAP_ERR:
				p->data = S_NIL;
				break;
			case COMMAND_NOT_FOUND:
				p->data = S_UNK;
				break;
			case END:
				return NULL;
				break;
			default:
				return 0;
				break;
		}
		p->size = strlen(p->data);
		p->fd = rep->sfd;
		p->alloc = 0;
		p->tocli = 1;
	}
	schedule_write(p);
	free(rep->data);
	return NULL;
}


void execute(char *buffer, int sfd, int rfd, int fp) {
	reply *rep = malloc(sizeof(reply));
	rep->sfd = sfd;
	rep->rfd = rfd;
	rep->fp = fp;
	char *command = strtok(buffer, " \r\n");
    if (command) {
        /* check if the buffer contains a valid command */
        for (int i = 0; i < commands_len(); i++) {
            if (strncasecmp(command,
						command_entries[i].name, strlen(command_entries[i].name)) == 0
                    && strlen(command_entries[i].name) == strlen(command)) {
				rep->data = (*command_entries[i].func)(buffer + strlen(command) + 1);
				(*command_entries[i].callback)(rep);
            }
        }
	}
	free(rep);
}


/*
 * Auxiliary function used to check the command coming from clients
 * FIXME: repeated code
 */
int check_command(char *buffer) {

    char *command = strtok(buffer, " \r\n");

    if (command) {
        // in case of 'QUIT' or 'EXIT' close the connection
        if (strlen(command) == 4
                && (strncasecmp(command, "quit", 4) == 0
                    || strncasecmp(command, "exit", 4) == 0))
            return END;

		for (int i = 0; i < commands_len(); i++) {
			if (strncasecmp(command,
						command_entries[i].name, strlen(command_entries[i].name)) == 0
					&& strlen(command_entries[i].name) == strlen(command)) {
				return 1;
			}
		}
		return COMMAND_NOT_FOUND;
    }
    return 0;
}


/*
 * Utility function, get a valid index in the range of the cluster buckets, so
 * it is possible to route command to the correct node
 */
static int hash(char *key) {
    uint32_t seed = 65133; // initial seed for murmur hashing
    if (key) {
        char *holder = malloc(strlen(key));
        strcpy(holder, key);
        trim(holder);
        /* retrieve an index in the bucket range */
        int idx = murmur3_32((const uint8_t *) holder,
                strlen(holder), seed) % PARTITIONS;

        DEBUG("Destination node: %d for key %s\r\n", idx, holder);
        free(holder);
        return idx;
    } else return -1;
}

/*
 * Helper function to route command to the node into which keyspace contains
 * idx, find the node by performing a simple linear search
 */
static void route_command(int idx, int fd, char *b, struct message *msg) {
    /* Send the message serialized to the right node according the routing
       table cluster */
    list_node *cursor = instance.cluster->head;
    while(cursor) {
        cluster_node *n = (cluster_node *) cursor->data;
        if (idx >= n->range_min && idx <= n->range_max) {
            /* check if the range is in the current node */
            if (n->self == 1) {
                /* commit command directly if the range is handled by the
                   current node */
				execute(b, fd, fd, 0);
                break;
            } else {
                msg->content = b;
                msg->fd = fd;
                msg->ready = 0;  // comes from client, i'm peer
                char *payload = serialize(*msg);
				peer_t *p = (peer_t *) malloc(sizeof(peer_t));
				p->fd = n->fd;  // dest fd
				p->alloc = 1;
				p->data = payload;
				p->size = strlen(b) + S_OFFSET;
				p->tocli = 0;
                DEBUG("Redirect toward cluster member %s\n", n->name);
                schedule_write(p);
                break;
            }
        }
        cursor = cursor->next;
    }
}


int peer_command_handler(int fd) {
    char buf[instance.el.bufsize];
    memset(buf, 0x00, instance.el.bufsize);
    int ret = 0;

    /* read data from file descriptor socket */
	// TODO: need a circular buffer or something similar to handle all incoming
	// data in a loop
    int bytes = recv(fd, buf, instance.el.bufsize, 0);

    if (bytes == -1) {
        if (errno != EAGAIN) ret = 1;
    }
	else if (bytes == 0) {
		ret = 1;
	}
    else {
		peer_t *p = (peer_t *) malloc(sizeof(peer_t));
		/* message came from a peer node, so it is serialized */
		struct message m = deserialize(buf); // deserialize into message structure
		DEBUG("Received data from peer node, message: %s\n", m.content);

		if (strcmp(m.content, S_OK) == 0
				|| strcmp(m.content, S_NIL) == 0
				|| strcmp(m.content, S_UNK) == 0) {
			DEBUG("Answer to client\n");
			p->fd = m.fd;
			p->alloc = 0;
			/* answer to the original client */
			if (strcmp(m.content, S_OK) == 0) {
				p->data = S_OK;
			} else if (strcmp(m.content, S_NIL) == 0) {
				p->data = S_NIL;
			} else if (strcmp(m.content, S_UNK) == 0) {
				p->data = S_UNK;
			}
			p->size = strlen(p->data);
			p->alloc = 0;
			p->tocli = 1;
			schedule_write(p);
		} else if (m.ready == 1) {
			DEBUG("Answer to client from a query\n");
			/* answer to a query operations to the original client */
			p->fd = m.fd;
			p->data = m.content;
			p->size = strlen(m.content);
			p->alloc = 1;
			p->tocli = 1;
			schedule_write(p);
		} else {
			/* message from another node */
			DEBUG("Answer to another node, processing inst: %s\n", m.content);
			execute(m.content, fd, m.fd, 1);
		}
	}
	return ret;
}


int client_command_handler(int fd) {
    struct message msg;
    char buf[instance.el.bufsize];
    memset(buf, 0x00, instance.el.bufsize);
    int ret = 0;

    /* read data from file descriptor socket */
	// TODO: need a circular buffer or something similar to handle all incoming
	// data in a loop
    int bytes = recv(fd, buf, instance.el.bufsize, 0);

    if (bytes == -1) {
        if (errno != EAGAIN) ret = 1;
    }
	else if (bytes == 0) {
		ret = 1;
	}
    else {
		/* check the if the command is genuine */
		char *dupstr = strdup(buf);
		int check = check_command(dupstr);
		free(dupstr);

		if (check == 1) {
			if (instance.cluster_mode == 1) {
				/* message came directly from a client */
				char *command = NULL, *b = strdup(buf); // payload to send
				command = strtok(buf, " \r\n");
				DEBUG("Command : %s\n", command);
				/* command is handled and it isn't an informative one */
				char *arg_1 = strtok(NULL, " ");
				DEBUG("Key : %s\n", arg_1);
				int idx = hash(arg_1);
				/* route the command to the correct node */
				route_command(idx, fd, b, &msg);
				free(b);
			}
			else {
				/* Single node instance, cluster is not enabled */
				execute(buf, fd, fd, 0);
			}
		} else {
			/* command received is not recognized or is a quit command */
			DEBUG("Unrecognized command\n");
			peer_t *p = (peer_t *) malloc(sizeof(peer_t));
			switch (check) {
				case COMMAND_NOT_FOUND:
					p->fd = fd;
					p->alloc = 0;
					p->tocli = 1;
					p->data = S_UNK;
					p->size = strlen(S_UNK);
					schedule_write(p);
					break;
				case END:
					ret = END;
					break;
				default:
					break;
			}
		}
	}
	return ret;
}


/*************************** -- COMMANDS -- ***************************/


void *set_command(char *cmd) {
	int *ret = malloc(sizeof(int));
    void *key = strtok(cmd, " ");
    if (key) {
        void *val = (char *) key + strlen(key) + 1;
        if (val) *ret = map_put(instance.store, strdup(key), strdup(val));
    }
    return ret;
}


void *get_command(char *cmd) {
	void *key = strtok(cmd, " ");
	if (key) {
		trim(key);
		void *val = map_get(instance.store, key);
		if (val) {
			return val;
		}
	}
	return NULL;
}


/*
 * DEL command handler delete a key-value pair if present, return
 * MAP_ERR reply code otherwise.
 *
 * Require one argument:
 *
 *     DEL <key>
 */
void *del_command(char *cmd) {
	int *ret = malloc(sizeof(int));
	*ret = MAP_ERR;
	void *key = strtok(cmd, " ");
	while (key) {
		trim(key);
		*ret = map_del(instance.store, key);
		key = strtok(NULL, " ");
	}
    return ret;
}


/*
 * INC command handler, increment (add 1) to the integer value to it if
 * present, return MAP_ERR reply code otherwise.
 *
 * Requires at least one arguments, but also accept optionally the amount to be
 * added to the specified key:
 *
 *     INC <key>   // +1 to <key>
 *     INC <key> 5 // +5 to <key>
 */
void *inc_command(char *cmd) {
	int *ret = malloc(sizeof(int));
	*ret = MAP_ERR;
	void *key = strtok(cmd, " ");
	if (key) {
		trim(key);
		void *val = map_get(instance.store, key);
		if (val) {
			char *s = (char *) val;
			if (ISINT(s) == 1) {
				int v = GETINT(s);
				char *by = strtok(NULL, " ");
				if (by && ISINT(by)) {
					v += GETINT(by);
				} else v++;
				sprintf(val, "%d\n", v);
				*ret = MAP_OK;
			}
		}
	}
    return ret;
}


/*
 * INCF command handler, increment (add 1.00) to the float value to it if
 * present, return MAP_ERR reply code otherwise.
 *
 * Requires at least one arguments, but also accept optionally the amount to be
 * added to the specified key:
 *
 *     INCF <key>     // +1.0 to <key>
 *     INCF <key> 5.0 // +5.0 to <key>
 */
void *incf_command(char *cmd) {
	int *ret = malloc(sizeof(int));
	*ret = MAP_ERR;
	void *key = strtok(cmd, " ");
	if (key) {
		trim(key);
		void *val = map_get(instance.store, key);
		if (val) {
			char *s = (char *) val;
			if (is_float(s) == 1) {
				double v = GETDOUBLE(s);
				char *by = strtok(NULL, " ");
				if (by && is_float(by)) {
					v += GETDOUBLE(by);
				} else v += 1.0;
				sprintf(val, "%lf\n", v);
				*ret = MAP_OK;
			}
		}
	}
    return ret;
}


/*
 * DEC command handler, decrement (subtract 1) to the integer value to it if
 * present, return MAP_ERR reply code otherwise.
 *
 * Requires at least one arguments, but also accept optionally the amount to be
 * subtracted to the specified key:
 *
 *     DEC <key>   // -1 to <key>
 *     DEC <key> 5 // -5 to <key>
 */
void *dec_command(char *cmd) {
	int *ret = malloc(sizeof(int));
	*ret = MAP_ERR;
	void *key = strtok(cmd, " ");
	if (key) {
		trim(key);
		void *val = map_get(instance.store, key);
		if (val) {
			char *s = (char *) val;
			if(ISINT(s) == 1) {
				int v = GETINT(s);
				char *by = strtok(NULL, " ");
				if (by != NULL && ISINT(by)) {
					v -= GETINT(by);
				} else v--;
				sprintf(val, "%d\n", v);
				*ret = MAP_OK;
			}
		}
    }
    return ret;
}


/*
 * DECF command handler, decrement (subtract 1.00) to the foat value to it if
 * present, return MAP_ERR reply code otherwise.
 *
 * Requires at least one arguments, but also accept optionally the amount to be
 * subtracted to the specified key:
 *
 *     DECF <key>     // -1.0 to <key>
 *     DECF <key> 5.0 // -5.0 to <key>
 */
void *decf_command(char *cmd) {
	int *ret = malloc(sizeof(int));
	*ret = MAP_ERR;
	void *key = strtok(cmd, " ");
	if (key) {
		trim(key);
		void *val = map_get(instance.store, key);
		if (val) {
			char *s = (char *) val;
			if(is_float(s) == 1) {
				double v = GETDOUBLE(s);
				char *by = strtok(NULL, " ");
				if (by != NULL && ISINT(by)) {
					v -= GETDOUBLE(by);
				} else v -= 1.0;
				sprintf(val, "%lf\n", v);
				*ret = MAP_OK;
			}
        }
    }
    return ret;
}


/*
 * APPEND command handler, append a suffix to the value associated to the found
 * key, returning MISSING reply code if no key were found.
 *
 * Require two arguments:
 *
 *     APPEND <key> <value>
 */
void *append_command(char *cmd) {
	int *ret = malloc(sizeof(int));
	*ret = MAP_ERR;
	void *key = strtok(cmd, " ");
	void *val = (char *) key + strlen(key) + 1;
	if (key && val) {
		char *key_holder = strdup(key);
		char *val_holder = strdup(val);
		void *_val = map_get(instance.store, key_holder);
		if (_val) {
			remove_newline(_val);
			char *append = append_string(_val, val_holder);
			*ret = map_put(instance.store, key_holder, append);
		}
	}
    return ret;
}



/*
 * PREPEND command handler, prepend a prefix to the value associated to the
 * found key, returning MISSING reply code if no key were found.
 *
 * Require two arguments:
 *
 *     PREPEND <key> <value>
 */
void *prepend_command(char *cmd) {
	int *ret = malloc(sizeof(int));
	*ret = MAP_ERR;
	void *key = strtok(cmd, " ");
	void *val = (char *) key + strlen(key) + 1;
	if (key && val) {
		char *key_holder = strdup(key);
		char *val_holder = strdup(val);
		void *_val = map_get(instance.store, key_holder);
		if (_val) {
			remove_newline(val_holder);
			char *append = append_string(val_holder, _val);
			*ret = map_put(instance.store, key_holder, append);
		}
	}
    return ret;
}


/*
 * GETP command handler gather all the informations associated to the key-value
 * pair if present, including expire time and creation time, NULL reply
 * otherwise.
 *
 * Require one argument:
 *
 *     GETP <key>
 */
void *getp_command(char *cmd) {
	void *key = strtok(cmd, " ");
	if (key) {
		trim(key);
		map_entry *kv = map_get_entry(instance.store, key);
		if (kv) {
			size_t kvstrsize = strlen(kv->key)
				+ strlen((char *) kv->val) + (sizeof(long) * 2) + 128;
			char *kvstring = malloc(kvstrsize); // long numbers
			/* check if expire time is set */
			char expire_time[19];
			if (kv->has_expire_time)
				sprintf(expire_time, "%ld\n", kv->expire_time / 1000);
			else
				sprintf(expire_time, "%d\n", -1);

			/* format answer */
			sprintf(kvstring, "key: %s\nvalue: %screation_time: %ld\nexpire_time: %s",
					(char *) kv->key, (char *) kv->val, kv->creation_time, expire_time);
			return kvstring;
		}
    }
    return NULL;
}


/*
 * FLUSH command handler, delete the entire keyspace.
 *
 * Doesn't require any argument.
 */
void *flush_command(char *cmd) {
	int *ret = malloc(sizeof(int));
    if (instance.store != NULL) {
        map_release(instance.store);
        instance.store = NULL;
        instance.store = map_create();
    }
	*ret = OK;
    return ret;
}


/*
 * CLUSTERINFO command handler, collect some informations of the cluster, used
 * as a private function.
 *
 */
// static void get_clusterinfo(int sfd) {
//     if (instance.cluster_mode == 1) {
//         char info[1024 * instance.cluster->len];
//         int pos = 0;
//         list_node *cursor = instance.cluster->head;
//         while (cursor) {
//             cluster_node *n = (cluster_node *) cursor->data;
//             int size = strlen(n->name) + strlen(n->addr) + 47; // 47 for literal string
//             char *status = "reachable";
//             if (n->state == UNREACHABLE) status = "unreachable";
//             snprintf(info + pos, size, "%s - %s:%d - Key range: %d - %d %s\n", n->name,
//                     n->addr, n->port, n->range_min, n->range_max, status);
//             pos += size;
//             cursor = cursor->next;
//         }
//         schedule_write(sfd, info, pos, 0);
//     }
// }
