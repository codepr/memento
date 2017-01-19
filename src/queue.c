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

#include <stdio.h>
#include "queue.h"

queue *create_queue(void) {
    queue *q = (queue*) malloc(sizeof(queue));
    q->last = 0;
    q->front = q->rear = NULL;
    return q;
}

/* insert data on the rear node */
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

/* remove data from the front node and deallocate it */
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

/* peek for the first element of the queue without removing it */
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

int queue_contains(queue *q, void *data) {
    if (q != NULL)
        for (int i = 0; i < queue_len(q); ++i)
            if (((q->rear)+i)->data == data) return 1;
    return -1;
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
