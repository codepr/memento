#ifndef COMMANDS_H
#define COMMANDS_H

#include "partition.h"

typedef enum {OK, PAYLOAD_OK, ITERATE_OK, MISSING, FULL, OOM, COMMAND_NOT_FOUND, END} reply_code;

extern const char *commands[];
extern const char *queries[];
extern const char *enumerates[];
extern const char *services[];

int commands_array_len(void);
int queries_array_len(void);
int enumerates_array_len(void);
int services_array_len(void);

// Commands
int set_command(partition **, char *);
int del_command(partition **, char *);
int pub_command(partition **, char *);
int inc_command(partition **, char *);
int incf_command(partition **, char *);
int dec_command(partition **, char *);
int decf_command(partition **, char *);
int append_command(partition **, char *);
int prepend_command(partition **, char *);
int expire_command(partition **, char *);
// Queries
int get_command(partition **, char *, int);
int getp_command(partition **, char *, int);
int sub_command(partition **, char *, int);
int unsub_command(partition **, char *, int);
int tail_command(partition **, char *, int);
int prefscan_command(partition **, char *, int);
int fuzzyscan_command(partition **, char *, int);
int ttl_command(partition **, char *, int);
// enumerates
int count_command(partition **, int);
int key_command(partition **, int);
int values_command(partition **, int);
// services
int flush_command(partition **);

extern int (*cmds_func[]) (partition **, char *);
extern int (*qrs_func[]) (partition **, char *, int);
extern int (*enum_func[]) (partition **, int);
extern int (*srvs_func[]) (partition **);

#endif
