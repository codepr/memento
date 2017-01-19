#ifndef COMMANDS_H
#define COMMANDS_H

#include "partition.h"

typedef enum {OK, PAYLOAD_OK, ITERATE_OK, MISSING, FULL, OOM, COMMAND_NOT_FOUND, END} reply_code;


int process_command(int, partition **, char *, int, int);

extern const char *commands[];
extern const char *queries[];
extern const char *enumerates[];
extern const char *services[];

int commands_array_len(void);
int queries_array_len(void);
int enumerates_array_len(void);
int services_array_len(void);

// Commands
int set_command(int, partition **, char *);
int del_command(int, partition **, char *);
int pub_command(int, partition **, char *);
int inc_command(int, partition **, char *);
int incf_command(int, partition **, char *);
int dec_command(int, partition **, char *);
int decf_command(int, partition **, char *);
int append_command(int, partition **, char *);
int prepend_command(int, partition **, char *);
int expire_command(int, partition **, char *);
// Queries
int get_command(int, partition **, char *, int, int);
int getp_command(int, partition **, char *, int, int);
int sub_command(int, partition **, char *, int, int);
int unsub_command(int, partition **, char *, int, int);
int tail_command(int, partition **, char *, int, int);
int prefscan_command(int, partition **, char *, int, int);
int fuzzyscan_command(int, partition **, char *, int, int);
int ttl_command(int, partition **, char *, int, int);
// enumerates
int count_command(int, partition **, int, int);
int keys_command(int, partition **, int, int);
int values_command(int, partition **, int, int);
// services
int flush_command(int, partition **);
// cluster
int addnode_command(map_t, int, int);

extern int (*cmds_func[]) (int, partition **, char *);
extern int (*qrs_func[]) (int, partition **, char *, int, int);
extern int (*enum_func[]) (int, partition **, int, int);
extern int (*srvs_func[]) (int, partition **);

#endif
