#ifndef SERIALIZER_H
#define SERIALIZER_H

struct message {
    char *content;
    int fd;
};

char *serialize(struct message);
struct message deserialize(char *);

#endif
