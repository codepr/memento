#include <stdio.h>
#include "queue.h"

queue *create_queue() {
    queue *q = (queue*) malloc(sizeof(queue));
    q->last = 0;
    q->front = q->rear = NULL;
    return q;
}

void enqueue(queue *q, void *data) {
    queue_node *new_node = (queue_node*) malloc(sizeof(queue_node));
    new_node->next = NULL;
    new_node->data = data;
    q->last++;
    if(q->front == NULL && q->rear == NULL) {
        q->front = new_node;
        q->rear = new_node;
    }
    else {
        q->rear->next = new_node;
        q->rear = new_node;
    }
}

void *dequeue(queue* q) {
    queue_node *del_node;
    void *del_item;

    if(q->front == NULL && q->rear == NULL) {
        perror("Queue is empty");
        del_item = NULL;
    }
    else {
        del_node = q->front;
        q->front = q->front->next;
        if(!q->front) {
            q->rear = NULL;
        }
        del_item = del_node->data;
        if (del_node) {
            free(del_node);
        }
        q->last--;
    }
    return del_item;
}

void *peek(queue* q) {
    void *peek_item;

    if(q->front == NULL && q->rear == NULL) {
        perror("Queue is empty");
        peek_item = NULL;
    }
    else {
        peek_item = q->front->data;
    }
    return peek_item;
}

unsigned long queue_len(queue *q) {
    if (q != NULL)
        return q->last;
    else return 0;
}

void release_queue(queue *q) {
    if (q != NULL) {
        while(q->last > 0) {
            dequeue(q);
        }
        free(q);
    }
}

iterator queue_next(iterator it) {
    return it->next;
}
