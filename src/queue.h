#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>

typedef struct queue_node {
    void *data;
    struct queue_node *next;
} queue_node;

typedef struct queue {
    size_t last;
    queue_node *front;
    queue_node *rear;
} queue;

typedef queue_node *iterator;

queue *create_queue(void);
void enqueue(queue *, void *);
void *dequeue(queue *);
void *peek(queue *);
unsigned long queue_len(queue *);
void release_queue(queue *);
iterator queue_next(iterator);

#endif
