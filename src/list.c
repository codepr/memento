/*
 * Copyright (c) 2017 Andrea Giacomo Baldan
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
 * Copyright (c) 2016-2017 Andrea Giacomo Baldan
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "cluster.h"


/*
 * Create a list, initializing all fields
 */
list *list_create(void) {
    list *l = shb_malloc(sizeof(list));
    // set default values to the list structure fields
    l->head = l->tail = NULL;
    l->len = 0L;
    return l;
}


/*
 * Attach a node to the head of a new list
 */
list *list_attach(list *l, list_node *head, unsigned long len) {
    // set default values to the list structure fields
    l->head = head;
    l->len = len;
    return l;
}

/*
 * Destroy a list, releasing all allocated memory
 */
void list_release(list *l) {
    if (!l) return;
    list_node *h = l->head;
    list_node *tmp;
    // free all nodes
    while (l->len--) {
        tmp = h->next;
        if (h) {
            if (h->data) free(h->data);
            free(h);
        }
        h = tmp;
    }
    // free list structure pointer
    free(l);
}


/*
 * Insert value at the front of the list
 * Complexity: O(1)
 */
list *list_head_insert(list *l, void *val) {
    list_node *new_node = shb_malloc(sizeof(list_node));
    new_node->data = val;
    if (l->len == 0) {
        l->head = l->tail = new_node;
        new_node->next = NULL;
    } else {
        new_node->next = l->head;
        l->head = new_node;
    }
    l->len++;
    return l;
}


/*
 * Insert value at the back of the list
 * Complexity: O(1)
 */
list *list_tail_insert(list *l, void *val) {
    list_node *new_node = shb_malloc(sizeof(list_node));
    new_node->data = val;
    new_node->next = NULL;
    if (l->len == 0) l->head = l->tail = new_node;
    else {
        l->tail->next = new_node;
        l->tail = new_node;
    }
    l->len++;
    return l;
}


/*
 * Returns a pointer to a node near the middle of the list,
 * after having truncated the original list before that point.
 */
static list_node *bisect_list(list_node *head) {
    /* The fast pointer moves twice as fast as the slow pointer. */
    /* The prev pointer points to the node preceding the slow pointer. */
    list_node *fast = head, *slow = head, *prev = NULL;

    while (fast != NULL && fast->next != NULL) {
        fast = fast->next->next;
        prev = slow;
        slow = slow->next;
    }

    if (prev != NULL)
        prev->next = NULL;

    return slow;
}


/*
 * Merges two list by using the head node of the two, sorting them according to
 * lexigraphical ordering of the node names.
 */
static list_node *merge_list(list_node *list1, list_node *list2) {
    list_node dummy_head = { NULL, NULL }, *tail = &dummy_head;

    while ((list1 != NULL) && (list2 != NULL)) {

        /* cast to cluster_node */
        cluster_node *n1 =
            (cluster_node *) list1->data;
        cluster_node *n2 =
            (cluster_node *) list2->data;

        list_node **min = ((strcmp(n1->name, n2->name)) < 0) ? &list1 : &list2;
        list_node *next = (*min)->next;
        tail = tail->next = *min;
        *min = next;
    }

    tail->next = list1 ? list1 : list2;
    return dummy_head.next;
}


/*
 * Merge sort for cluster nodes list, based on the name field of every node
 */
list_node *merge_sort(list_node *head) {
    list_node *list1 = head;
    if ( (list1 == NULL) || (list1->next == NULL) )
        return list1;
    /* find the middle */
    list_node *list2 = bisect_list(list1);

    return merge_list(merge_sort(list1), merge_sort(list2));
}
