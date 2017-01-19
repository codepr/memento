#include <stdlib.h>
#include <string.h>
#include "serializer.h"

char *serialize(struct message msg) {
    int s_len  = strlen(msg.content);
    int t_len  = sizeof(int) + s_len;
    char *serialized = malloc(sizeof(char) * (t_len + sizeof(int)));
    char *metadata = serialized;
    char *fd = serialized + sizeof(int);
    char *content = fd + sizeof(int);
    *((int*) metadata) = s_len;
    *((int*) fd) = msg.fd;
    strncpy(content, msg.content, s_len);
    return serialized;
}

struct message deserialize(char *serialized_msg) {
    char* metadata = serialized_msg;
    char* fd = metadata + sizeof(int);
    char* content = fd + sizeof(int);
    struct message msg;
    int s_len = *((int*)metadata);
    msg.fd = *((int*) fd);
    msg.content = malloc((s_len + 1) * sizeof(char));
    strncpy(msg.content, content, s_len);
    msg.content[s_len] = '\0';
    return msg;
}
