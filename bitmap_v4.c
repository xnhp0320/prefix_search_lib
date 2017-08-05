#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bitmap.h"
#include "bitmap_v4.h"
#include "mb_node.h"
#include <arpa/inet.h>


void bitmap_destroy_trie(struct mb_node *root,
        struct mm *m, void (*destroy_nhi)(void *nhi))
{
    destroy_subtrie(root, m, destroy_nhi, 0);
}


int bitmap_traverse_branch(struct mb_node *node, 
        uint32_t ip, int cidr, 
        traverse_func func, 
        void *user_data)
{

    uint8_t pos;
    uint8_t stride;
    int ret;

    for (;;) {

        if (unlikely(cidr < STRIDE)) {
            stride = ip >> (LENGTH - cidr);
            pos = count_inl_bitmap(stride, cidr);
            ret = func(node, stride, pos, LEAF_NODE, user_data);
            break;
        }
        else {
            stride = ip >> (LENGTH - STRIDE);
            pos = count_enl_bitmap(stride);
            ret = func(node, stride, pos, MID_NODE, user_data);

            if(ret != TRAVERSE_CONT)
                break;

            node = next_child(node, pos);

    
            cidr -= STRIDE;
            ip <<= STRIDE;
        }
    }

    return ret;

}

/*
 * Return Value:
 * 0  successfully inserted
 * 1  a same prefix exist, updated 
 * -1 ERROR 
 *
 */

int bitmap_insert_prefix(
        struct mb_node *node, 
        struct mm *m, 
        uint32_t ip, int cidr, 
        void *nhi)
{
    uint8_t pos;
    uint8_t stride;
    uint8_t level = 0;
    void **i;
    int ret;
    struct rollback_stash stash = {.stack = 0};

    for (;;) {

        if (unlikely(cidr < STRIDE)) {
            stride = ip >> (LENGTH - cidr);
            pos = count_inl_bitmap(stride, cidr);
            if (test_bitmap(node->internal, pos)) {
                //already has the rule, need to update the rule
                i = pointer_to_nhi(node, pos);
                *i = nhi;
                rollback_stash_clear(&stash);
                return 1;
            }
            else {
                //update_inl_bitmap(node, pos);
                //rules pos starting at 1, so add 1 to offset
                ret = extend_rule(m, node, pos, level, nhi, &stash);
                if(ret == -1) {
                    rollback_stash_rollback(&stash);
                    return -1;
                }
                break;
            }
        }
        else {
            //push the "cidr == stride" node into the next child
            stride = ip >> (LENGTH - STRIDE);
            pos = count_enl_bitmap(stride);

            if (test_bitmap(node->external, pos)) {
                node = next_child(node, pos);
            } 
            else {
                //update_enl_bitmap(node, pos);
                node = extend_child(m, node, level, pos, &stash);
                if(!node) {
                    rollback_stash_rollback(&stash);
                    return -1;
                }
            }
            cidr -= STRIDE;
            ip <<= STRIDE;
            level ++;
        }
    }

    rollback_stash_clear(&stash);
    return 0;
}

/*
 * Return Value:
 * 0  the root node has not been emptied
 * 1  the root node has been emptied
 * -1 ERROR 
 */
int bitmap_delete_prefix(struct mb_node *node, struct mm *m, 
        uint32_t ip, int cidr, void (*destroy_nhi)(void *nhi))
{
    uint8_t pos;
    uint8_t stride;
    struct trace trace_node[UPDATE_LEVEL];
    int i = 0;
    int ret;
    struct rollback_stash stash = {.stack = 0};

    for (;;) {

        if (unlikely(cidr < STRIDE)) {
            stride = ip >> (LENGTH - cidr);
            pos = count_inl_bitmap(stride, cidr);
            if (destroy_nhi) {
                void ** nhi;
                nhi = pointer_to_nhi(node, pos);
                destroy_nhi(*nhi);
            }

            trace_node[i].node = node;
            trace_node[i].pos =  pos; 

            break;
        }
        else {
            stride = ip >> (LENGTH - STRIDE);
            pos = count_enl_bitmap(stride);

            
            trace_node[i].node = node;
            trace_node[i].pos  = pos; 

            node = next_child(node, pos);

            cidr -= STRIDE;
            ip <<= STRIDE;
        }
        i++;
    }

    ret = update_nodes(m, trace_node, i, &stash);
    if(ret != -1) {
        rollback_stash_clear(&stash);
    }
    return ret;
}

int bitmap_prefix_exist(struct mb_node *n,  uint32_t ip, uint8_t cidr)
{
    int ret;
    ret = bitmap_traverse_branch(n, ip, cidr, prefix_exist_func, NULL); 
    return ret;
}

   
void * bitmap_do_search(struct mb_node *n, uint32_t ip)
{
    uint8_t stride;
    int pos;
    void **longest = NULL;

    for (;;) {
        stride = ip >> (LENGTH - STRIDE);
        pos = tree_function(n->internal, stride);

        if (pos != -1){
            longest = pointer_to_nhi(n, pos);
        }
        if (test_bitmap(n->external, count_enl_bitmap(stride))) {
            n = next_child(n, count_enl_bitmap(stride));
            ip = (uint32_t)(ip << STRIDE);
        }
        else {
            break;
        }
    }

    return (longest == NULL)?NULL:*longest;
}



static __thread struct lazy_travel lazy_mark[LEVEL]; 

void *bitmap_do_search_lazy(struct mb_node *n, uint32_t ip)
{
    uint8_t stride;
    int pos;
    void **longest = NULL;
    int travel_depth = -1;

    for (;;) {
        stride = ip >> (LENGTH - STRIDE);

        if (likely(test_bitmap(n->external, count_enl_bitmap(stride)))) {
            travel_depth++;
            lazy_mark[travel_depth].lazy_p = n;
            lazy_mark[travel_depth].stride = stride;
            n = next_child(n, count_enl_bitmap(stride));
            //__builtin_prefetch(pointer_to_nhi(n, 0)); 
            ip = (uint32_t)(ip << STRIDE);
        }
        else {

            pos = tree_function(n->internal, stride);
            if (pos != -1) {
                longest = pointer_to_nhi(n, pos);
                goto out;
            }

            for(;travel_depth>=0;travel_depth --) {
                n = lazy_mark[travel_depth].lazy_p;
                stride = lazy_mark[travel_depth].stride;
                pos = tree_function(n->internal, stride);
                if (pos != -1) {
                    longest = pointer_to_nhi(n, pos);
                    goto out;
                }
            }
            break;
        }
    }
out:
    return (longest == NULL)?NULL:*longest;
}

static __thread struct lazy_travel lazy_mark_batch[BATCH][LEVEL];

void bitmap_do_search_lazy_batch(struct mb_node *n[BATCH], 
        uint32_t ip[BATCH], void *ret[BATCH], int cnt)
{
    uint8_t stride[BATCH];
    int pos[BATCH];
    //max batch is 64
    uint64_t endcnt = 0;

    void **longest[BATCH];
    memset(longest, 0, sizeof(void**) * cnt);

    int i = 0;
    int travel_depth[BATCH];
    memset(travel_depth, 0, sizeof(int) * cnt);

    //stage 0 
    for(i = 0; i < cnt; i++)  {
        stride[i] = ip[i] >> (LENGTH - STRIDE);
    }

    //stage 1 
    for(;endcnt != ((1ULL << cnt) -1);)  {
        for(i = 0; i < cnt; i++) {
            if (likely(test_bitmap(n[i]->external, count_enl_bitmap(stride[i])))) {
                lazy_mark_batch[i][travel_depth[i]].lazy_p = n[i];
                lazy_mark_batch[i][travel_depth[i]].stride = stride[i];
                travel_depth[i]++;

                n[i] = next_child(n[i], count_enl_bitmap(stride[i]));
                ip[i] = (uint32_t)(ip[i] << STRIDE);
                stride[i] = ip[i] >> (LENGTH - STRIDE);
                __builtin_prefetch(n[i], 0); 
            }
            else {
                endcnt |= (1ULL << i);
                //__builtin_prefetch(pointer_to_nhi(n[i], 0));
            }
        }
    }
    //stage 2 
    for(i = 0; i < cnt; i++) {
        pos[i] = tree_function(n[i]->internal, stride[i]);
        if (pos[i] != -1) {
            longest[i] = pointer_to_nhi(n[i], pos[i]);
            ret[i] = (longest[i] == NULL)? NULL: *longest[i];
            continue;
        }

        for(travel_depth[i] -- ; travel_depth[i]>=0;travel_depth[i] --) {
            n[i] = lazy_mark_batch[i][travel_depth[i]].lazy_p;
            stride[i] = lazy_mark_batch[i][travel_depth[i]].stride;
            pos[i] = tree_function(n[i]->internal, stride[i]);
            if (pos[i] != -1) {
                longest[i] = pointer_to_nhi(n[i], pos[i]);
                goto out;
            }
        }
out:
        ret[i] = (longest[i] == NULL)? NULL: *longest[i];
    }
}


/*
 * find prefixes which has prefix length smaller than *bits_limit* that overlap with 
 * the prefix ip/cidr. 
 *
 */
uint8_t bitmap_detect_overlap_generic(struct mb_node *n, 
        uint32_t ip, uint8_t cidr, 
        uint32_t bits_limit, void **nhi_over)
{
    uint8_t stride;
    uint8_t mask = 0;
    uint8_t final_mask = 0;
    uint8_t curr_mask = 0;

    uint8_t step = 0;
    int pos;
    void **longest = NULL;


    //limit the bits to detect
    int limit;
    int limit_inside;
    int org_limit;
    
    limit = (cidr > bits_limit) ? bits_limit : cidr;
    org_limit = limit;
    while(limit>0) {

        stride = ip >> (LENGTH - STRIDE);
        limit_inside = (limit > STRIDE) ? STRIDE: limit;
        pos = find_overlap_in_node(n->internal, stride, &mask, limit_inside);

        if (pos != -1){
            curr_mask = step * STRIDE + mask;
            if (curr_mask < org_limit) {  
                final_mask = curr_mask;
                longest = pointer_to_nhi(n, pos);
            }
        }

        limit -= STRIDE;
        step ++;

        if (test_bitmap(n->external, count_enl_bitmap(stride))) {
            //printf("%d %p\n", depth, n);
            n = next_child(n, count_enl_bitmap(stride));
            ip = (uint32_t)(ip << STRIDE);
        }
        else {
            break;
        }
    }
    //printf("limit %d, total_mask %d\n", limit, final_mask);

    if(final_mask != 0) {
        *nhi_over = *longest;
    }
    
    //printf("detect_prefix error\n");
    return final_mask;
}


uint8_t bitmap_detect_overlap(struct mb_node *n, uint32_t ip, uint8_t cidr, void **nhi_over)
{
    return bitmap_detect_overlap_generic(n, ip, cidr, LENGTH, nhi_over);
}


struct print_key {
    uint32_t ip;
    uint32_t cidr;
};

static void print_ptr(struct print_key *key, 
        void (*print_next_hop)(void *nhi), void *nhi)
{
    struct in_addr addr;
    addr.s_addr = htonl(key->ip);
    printf("%s/%d ", inet_ntoa(addr), key->cidr);

    if (print_next_hop)
        print_next_hop(nhi);

    printf("\n");
}

void bitmap_mb_node_iter(struct mb_node *node, uint32_t ip, uint32_t left_bits, 
        uint32_t cur_cidr, void (*trie_traverse_func)(uint32_t ip, uint32_t cidr, void *nhi, void *user),
        void *userdata)
{
    int bit=0;
    int cidr=0;
    int stride = 0;
    int pos;
    uint32_t iptmp;

    void **nhi;
    struct mb_node *next;

    //internal prefix first
    for (cidr=0;cidr<= STRIDE -1;cidr ++ ){
        for (bit=0;bit< (1<<cidr);bit++) {
            pos = count_inl_bitmap(bit,cidr);
            if (test_bitmap(node->internal, pos)) {
                nhi = pointer_to_nhi(node, pos);
                iptmp = ip;
                iptmp |= bit << (left_bits - cidr);
                trie_traverse_func(iptmp, cur_cidr + cidr, *nhi, userdata);
            }
        }
    }

    for (stride = 0; stride < (1<<STRIDE); stride ++ ){
        pos = count_enl_bitmap(stride);
        if (test_bitmap(node->external, pos)) {
            next = next_child(node, pos);

            bitmap_mb_node_iter(next, 
                    ip | (stride << (left_bits - STRIDE)), 
                    left_bits - STRIDE, cur_cidr + STRIDE, 
                    trie_traverse_func, userdata); 
        }
    }

}

static void print_with_func(uint32_t ip, uint32_t cidr, void *nhi, void *userdata)
{
    struct print_key key;
    key.ip = ip;
    key.cidr = cidr;
    void (*print_next_hop)(void* nhi); 
    print_next_hop = userdata;

    print_ptr(&key, print_next_hop, nhi);
}


void bitmap_print_all_prefix(struct mb_node *root, 
        void (*print_next_hop)(void *nhi)) 
{
    bitmap_mb_node_iter(root, 0, LENGTH, 0, print_with_func, print_next_hop); 
}

static struct mb_node *copy_parent(struct mb_node **dst, struct mb_node *n)
{
    struct mb_node *copy;
    **dst = *n;
    copy = *dst;
    *dst = *dst + 1;
    return copy; 
}


static struct mb_node* copy_children(struct mb_node **dst, struct mb_node *n)
{
    struct mb_node *first_child;

    int child_num = count_children(n->external);
    int rule_num = count_children(n->internal);
    struct mb_node *first = POINT(n->child_ptr) - UP_RULE(rule_num);
    memcpy(*dst, first, (child_num + rule_num) * NODE_SIZE);
    first_child = *dst + UP_RULE(rule_num);
    *dst = *dst + (child_num + rule_num);
    return first_child; 
}


int bitmap_compact(struct mb_node *root, struct mm *m, struct mb_node **compact)
{
    aux_queue_t q;
    struct mb_node *out = NULL;
    int i;

    int max_node = 0;
    for (i = 0; i < MAX_LEVEL; i ++) {
        if(m->ms.lnode[i] == 0)
            break;
        if(m->ms.lnode[i] > max_node)
            max_node = m->ms.lnode[i];
    }

    if(aux_queue_init(&q, max_node) < 0) {
        return -1;
    }

    out = (struct mb_node *)calloc(sizeof(struct mb_node), m->ms.node);
    if(!out) {
        aux_queue_dctor(&q);
        return -1;
    }

    struct mb_node *curr = out;
    struct mb_node *first_child;

    aux_elem_t aux;
    aux_elem_t *top;

    /* push the first elem */
    aux.n = copy_parent(&curr, root);
    aux.len = count_children((aux.n)->external);
    aux_queue_put(&q, &aux);
    
    while(!aux_queue_len(&q)) { 
        aux_queue_get(&q, &top);

        first_child = copy_children(&curr, top->n);
        top->n->child_ptr = first_child;

        int i;
        for(i = 0; i < top->len; i ++) {
            aux.n = first_child + i;
            aux.len = count_children(aux.n->external); 
            aux_queue_put(&q, &aux);
        }
    }

    *compact = out;
    aux_queue_dctor(&q);

    return 0;
}
