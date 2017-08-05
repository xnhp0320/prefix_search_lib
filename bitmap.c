#include "bitmap.h"

int prefix_exist_func(struct mb_node *node, 
        uint8_t stride, uint8_t pos, uint8_t type, void *data)
{
    if(type == LEAF_NODE) {
        if (!(test_bitmap(node->internal , pos))) {
            return 0;
        }
    }
    else {
        if(!test_bitmap(node->external,pos)) {
            return 0;
        }
    }

    return TRAVERSE_CONT;
}

int update_nodes(struct mm *mm, struct trace *t, int total, struct rollback_stash *stash)
{
    int i;
    int node_need_to_del = 0;
    int ret;
    for(i=total;i >= 0;i--){
        if(i==total){

            ret = reduce_rule(mm, t[i].node, t[i].pos, i, stash);
            if(ret == -1) {
                rollback_stash_rollback(stash);
                return -1;
            }

            if((t[i].node)->internal == 0 && (t[i].node)->external == 0)
            {
                node_need_to_del = 1;
            }
        }
        else{
            if(node_need_to_del){
                ret = reduce_child(mm, t[i].node, t[i].pos, i, stash);
                if(ret == -1) {
                    rollback_stash_rollback(stash);
                    return -1;
                }
            }
            if((t[i].node)->internal == 0 && (t[i].node)->external == 0){
                node_need_to_del = 1;
            }
            else{
                node_need_to_del = 0;
            }
        }
    }
    return node_need_to_del;
}


int aux_queue_init(aux_queue_t *q, int cap)
{
    q->ptrs = (aux_elem_t *)calloc(sizeof(aux_elem_t), cap);
    if(!q->ptrs) {
        return -1;
    }
    q->head = 0;
    q->tail = 0;
    q->cap = cap; 
    return 0;
}

int aux_queue_len(aux_queue_t *q)
{
    return q->tail - q->head;
}

void aux_queue_shrink(aux_queue_t *q)
{
    int len = aux_queue_len(q);
    if(q->head > 0 && len) {
        memmove(q->ptrs, q->ptrs + q->head, len * sizeof(aux_elem_t));
        q->tail -= q->head;
        q->head = 0;
    }
}

int aux_queue_full(aux_queue_t *q)
{
    return aux_queue_len(q) >= q->cap;
}

int aux_queue_put(aux_queue_t *q, aux_elem_t *p)
{
    if(q->tail >= q->cap) {
        aux_queue_shrink(q);
    }
    if(!aux_queue_full(q)) {
        q->ptrs[q->tail++] = *p; 
        return 0;
    }
    return -1;
}

int aux_queue_empty(aux_queue_t *q)
{
    return q->head == q->tail;
}

void aux_queue_get(aux_queue_t *q, aux_elem_t **p)
{
    if(!aux_queue_empty(q)) {
        *p = &(q->ptrs[q->head++]);
    } else {
        *p = NULL;
    }
}

aux_elem_t *aux_queue_head(aux_queue_t *q)
{
    if(!aux_queue_empty(q))
        return &(q->ptrs[q->head]);
    else
        return NULL;
}

int aux_queue_dctor(aux_queue_t *q)
{
    if(q->ptrs){
        free(q->ptrs);
        q->ptrs = NULL;
    }
    q->head = 0;
    q->tail = 0;
    q->cap = 0;
    return 0;
}
