#ifndef MESSAGEQUEUE_H
#define MESSAGEQUEUE_H

#include "queue.h"

#define MQ_PORT "9898"
#define MAX_SLAVES (32)


struct consume_params {
    int *slaves;
    int *len;
    queue *q;
};


void mq_seed_gateway(queue *);


#endif
