#include <stdio.h>
#include "mb_node.h"


void * new_node(struct mm* m, int mb_cnt, int nh_cnt, int level)
{
    // one for result table
    // one for child table
    int r_c = UP_RULE(nh_cnt); 
    void *ret;

    ret = alloc_node(m, (UP_CHILD(mb_cnt) + r_c), level);
    if(!ret)
        return NULL;

    ret = POINT(ret) + r_c;
    return ret;
}


void free_node(struct mm *m, void *ptr, uint32_t cnt, int level)
{
    if (ptr){ 
        dealloc_node(m, cnt, level, ptr);
    }
}

void rollback_stash_push(struct rollback_stash *stash,
                         struct mb_node *node, 
                         struct mm *m, int level)
{
    assert(stash->stack < MAX_LEVEL);
    stash->val[stash->stack].node = node;
    stash->val[stash->stack].old_val = *node;
    stash->val[stash->stack].m = m;
    stash->val[stash->stack].level = level;
    stash->stack ++;
}

void rollback_stash_rollback(struct rollback_stash *stash)
{
    struct mb_node_val *val;
    int child_num;
    int rule_num;

    for(stash->stack--; stash->stack >=0; stash->stack --) {
        val = stash->val + stash->stack; 
        if(val->node->child_ptr) {
            /*free the new child_ptr */
            child_num = count_children(val->node->external);
            rule_num  = count_children(val->node->internal);
            free_node(val->m, POINT(val->node->child_ptr) - UP_RULE(rule_num), UP_CHILD(child_num) + UP_RULE(rule_num), val->level);
        }
        *val->node = val->old_val; 
    }
    stash->stack = 0;
}

void rollback_stash_clear(struct rollback_stash *stash)
{
    struct mb_node_val *val;
    int child_num;
    int rule_num;

    for(stash->stack--; stash->stack >=0; stash->stack --) {
        val = stash->val + stash->stack; 
        if(val->old_val.child_ptr){
            child_num = count_children(val->old_val.external);
            rule_num = count_children(val->old_val.internal);
            free_node(val->m, POINT(val->old_val.child_ptr) - UP_RULE(rule_num), UP_CHILD(child_num) + UP_RULE(rule_num), val->level);
        }
    }
    stash->stack = 0;
}

//pos start from 1
//
int extend_rule(struct mm *m, struct mb_node *node, uint32_t pos, int level, void *nhi, struct rollback_stash *stash)
{
    int child_num = count_children(node->external);
    int rule_num = count_children(node->internal);

    void **i;
    void **j;

    void *n = new_node(m, child_num, rule_num + 1, level);
    if(!n)
        return -1;

    rollback_stash_push(stash, node, m, level);

    update_inl_bitmap(node, pos);
    pos = count_ones(node->internal, pos) + 1; 


    if (child_num != 0){
        //copy the child
        memcpy(n,node->child_ptr,sizeof(struct mb_node)*UP_CHILD(child_num));
    }
    

    //insert the rule at the pos position

    if (rule_num != 0) {
        //copy the 1~pos-1 part
        i = (void**)node->child_ptr - pos + 1;
        j = (void**)n - pos + 1;
        
        memcpy(j,i,(pos-1)*sizeof(void*));

        //copy the pos+1~rule_num part
        i = (void**)node->child_ptr - rule_num;
        j = (void**)n - rule_num - 1;

        memcpy(j,i,(rule_num - pos + 1)*sizeof(void*));
    }
    


    i = (void**)n - pos;
    *i = nhi;
    
    //need to be atomic

    //if (node->child_ptr) {
    //    free_node(m, POINT(node->child_ptr) - UP_RULE(rule_num), UP_CHILD(child_num) + UP_RULE(rule_num), level);
    //}

    node->child_ptr = n; 
    return 0;
}

//return the extended node
//
struct mb_node * extend_child(struct mm *m, struct mb_node *node, int level,
        uint32_t pos, struct rollback_stash *stash)
{
    int child_num = count_children(node->external);
    int rule_num = count_children(node->internal);

    void *n = new_node(m, child_num +1 ,rule_num, level);
    if(!n) 
        return NULL;

    rollback_stash_push(stash, node, m, level);
    update_enl_bitmap(node, pos);
    pos = count_ones(node->external, pos);
    
    void **i;
    void **j;
    
    if (rule_num != 0) {
        //copy the rules
        i = (void **)node->child_ptr - rule_num;
        j = (void **)n - rule_num;
        memcpy(j,i,(rule_num)*sizeof(void*));
    }

    if (child_num != 0) {
        //copy the  0~pos-1 part
        memcpy(n,node->child_ptr,(pos)*sizeof(struct mb_node));
        //copy the pos~child_num-1 part to pos+1~child_num 
        memcpy((struct mb_node*)n+pos+1,(struct mb_node*)node->child_ptr + pos, (child_num - pos) * sizeof(struct mb_node));
    }


    //need to be atomic
    //if (node->child_ptr) {
    //    free_node(m, POINT(node->child_ptr) - UP_RULE(rule_num), UP_CHILD(child_num) + UP_RULE(rule_num), level);
    //}

    node->child_ptr = n;


    return (struct mb_node*)n + pos;
}

int reduce_child(struct mm *m, struct mb_node *node, int pos, int level, struct rollback_stash *stash)
{
    int child_num = count_children(node->external);
    int rule_num = count_children(node->internal);

    assert(child_num >= 1); 
    void *n = new_node(m, child_num -1 ,rule_num, level);
    if(!n && child_num + rule_num > 1)
        return -1;

    rollback_stash_push(stash, node, m, level);
    int clear_bit_idx = pos;
    pos = count_ones(node->external, pos);
    clear_bitmap(&node->external, clear_bit_idx);

    void **i;
    void **j;
    
    if (rule_num != 0) {
        //copy the rules
        i = (void **)node->child_ptr - rule_num;
        j = (void **)n - rule_num;
        memcpy(j,i,(rule_num)*sizeof(void*));
    }

    if (child_num > 1) {
        //copy the 0~pos-1 part
        memcpy(n,node->child_ptr,(pos)*sizeof(struct mb_node));
        //copy the pos+1~child_num part to pos~child_num-1 
        memcpy((struct mb_node*)n+pos,(struct mb_node*)node->child_ptr + pos + 1, (child_num - pos -1) * sizeof(struct mb_node));
    }


    //need to be atomic
    //if (node->child_ptr) {
    //    free_node(m, POINT(node->child_ptr) - UP_RULE(rule_num), UP_CHILD(child_num) + UP_RULE(rule_num), level);
    //}

    node->child_ptr = n;
    return 0;
}

int reduce_rule(struct mm *m, struct mb_node *node, uint32_t pos, int level, struct rollback_stash *stash)
{
    int child_num = count_children(node->external);
    int rule_num = count_children(node->internal);

    void **i;
    void **j;

    void *n = new_node(m, child_num, rule_num - 1, level);
    if(!n && rule_num + child_num > 1)
        return -1;

    rollback_stash_push(stash, node, m, level);
    int clear_bit_idx = pos;
    pos = count_ones(node->internal, pos) + 1;
    clear_bitmap(&node->internal, clear_bit_idx);
   
    assert(rule_num >= 1);

    if (child_num != 0){
        //copy the child
        memcpy(n,node->child_ptr,sizeof(struct mb_node)*UP_CHILD(child_num));
    }

    //delete the rule at the pos position
    if (rule_num > 1) {
        //copy the 1~pos-1 part
        i = (void**)node->child_ptr - pos + 1;
        j = (void**)n - pos + 1;
        
        memcpy(j,i,(pos-1)*sizeof(void*));

        //copy the pos~rule_num part
        i = (void**)node->child_ptr - rule_num;
        j = (void**)n - rule_num + 1;

        memcpy(j,i,(rule_num - pos)*sizeof(void*));
    }

    //need to be atomic
    //if (node->child_ptr) {
    //    free_node(m, POINT(node->child_ptr) - UP_RULE(rule_num), UP_CHILD(child_num) + UP_RULE(rule_num), level);
    //}

    node->child_ptr = n; 
    return 0;
}


#ifdef FAST_TREE_FUNCTION
static BITMAP_TYPE fct[1<<(STRIDE - 1)];

__attribute__((constructor)) void fast_lookup_init()
{
    uint32_t i;
    int j;
    int pos;

    uint32_t stride;
    for(i=0;i<(1<<(STRIDE-1));i++) {
        stride = i;
        for(j = STRIDE -1; j>=0; j--) {
            pos = count_inl_bitmap(stride, j);
            set_bitmap(fct+i, pos);
            stride >>= 1;
        }
    }
}

int tree_function(BITMAP_TYPE bitmap, uint8_t stride)
{
    BITMAP_TYPE ret;
    int pos;

    ret = fct[(stride>>1)] & bitmap;
    if(ret){
        pos = __builtin_clzll(ret);
        return BITMAP_BITS - 1 - pos;
    }
    else
        return -1;
}

#else

int tree_function(BITMAP_TYPE bitmap, uint8_t stride)
{
    int i;
    int pos;
    if (bitmap == 0ULL)
        return -1;
    for(i=STRIDE-1;i>=0;i--){
        stride >>= 1;
        pos = count_inl_bitmap(stride, i); 
        if (test_bitmap(bitmap, pos)){
            return pos;
        }
    }
    return -1;
}
#endif


int find_overlap_in_node(BITMAP_TYPE bitmap, uint8_t stride, uint8_t *mask, int limit_inside)
{
    int i;
    int pos;
    if (bitmap == 0ULL)
        return -1;
    //calulate the beginning bits
    stride >>= (STRIDE - limit_inside);

    for(i=limit_inside-1;i>=0;i--){
        stride >>= 1;
        *mask = i;
        pos = count_inl_bitmap(stride, i); 
        if (test_bitmap(bitmap, pos)){
            return pos;
        }
    }

    return -1;
}


void destroy_subtrie(struct mb_node *node, struct mm *m, void (*destroy_nhi)(void *nhi), int depth)
{
    int bit;
    int cidr;
    int pos;
    void ** nhi = NULL;
    int stride;
    struct mb_node *next = NULL;

    
    int cnt_rules;
    struct mb_node *first = NULL;

    for (cidr=0;cidr<= STRIDE -1;cidr ++ ){
        for (bit=0;bit< (1<<cidr);bit++) {
            pos = count_inl_bitmap(bit,cidr);
            if (test_bitmap(node->internal, pos)) {
                //nhi = (struct next_hop_info**)node->child_ptr - count_ones(node->internal, pos) - 1;
                nhi = pointer_to_nhi(node, pos);
                if (destroy_nhi && *nhi != NULL) {
                    destroy_nhi(*nhi);
                }
                *nhi = NULL;
            }
        }

    }


    for (stride = 0; stride < (1<<STRIDE); stride ++ ){
        pos = count_enl_bitmap(stride);
        if (test_bitmap(node->external, pos)) {
            //next = (struct mb_node *)node->child_ptr + count_ones(node->external, pos);
            next = next_child(node, pos);
            destroy_subtrie(next, m, destroy_nhi, depth + 1);
        }
    }

    cnt_rules = count_children(node->internal);
    first = POINT(node->child_ptr) - UP_RULE(cnt_rules);

    node->internal = 0;
    node->external = 0;
    node->child_ptr = NULL;

    int cnt_children = count_children(node->external);
    free_node(m, first, UP_RULE(cnt_rules) + UP_CHILD(cnt_children), depth);
}

