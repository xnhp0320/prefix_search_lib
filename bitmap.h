#ifndef _BITMAP_H__
#define _BITMAP_H__

#include "mb_node.h"
#include "mm.h"

//Node Type

#define LEAF_NODE 0 
#define MID_NODE 1

//Traverse continue
#define TRAVERSE_CONT 1 

struct trace{
    struct mb_node *node;
    uint32_t pos;
};

struct lazy_travel {
    struct mb_node *lazy_p;
    uint32_t stride;
};

int update_nodes(struct mm *mm, struct trace *t, int total, struct rollback_stash *stash);
typedef int (*traverse_func) (struct mb_node *node, 
        uint8_t stride, uint8_t pos, uint8_t type, void *data);

int prefix_exist_func(struct mb_node *node, 
        uint8_t stride, uint8_t pos, uint8_t type, void *data);

typedef struct aux_queue_elem{
    struct mb_node *n;
    int len;
} aux_elem_t;

typedef struct aux_queue {
    int head;
    int tail;
    int cap;
    aux_elem_t *ptrs;
} aux_queue_t;



int aux_queue_init(aux_queue_t *q, int cap);
int aux_queue_len(aux_queue_t *q);
void aux_queue_shrink(aux_queue_t *q);
int aux_queue_full(aux_queue_t *q);
int aux_queue_put(aux_queue_t *q, aux_elem_t *p);
int aux_queue_empty(aux_queue_t *q);
void aux_queue_get(aux_queue_t *q, aux_elem_t **p);
aux_elem_t *aux_queue_head(aux_queue_t *q);
int aux_queue_dctor(aux_queue_t *q);
#endif
