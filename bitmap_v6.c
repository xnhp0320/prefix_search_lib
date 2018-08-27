#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bitmap.h"
#include "bitmap_v6.h"
#include "mb_node.h"
#include <arpa/inet.h>


//1~127
void lshift_ipv6(struct ip_v6 *ip, uint8_t bits)
{
    uint64_t head;
    if (likely(bits < 64)) {
        head = ip->iplo >> (64 - bits);
        ip->iphi <<= bits;
        ip->iplo <<= bits;
        ip->iphi |= head;
    }
    else if (bits > 64) {
        head = ip->iplo << (bits - 64);
        ip->iphi = head;
        ip->iplo = 0;

    }
    else {
        ip->iphi = ip->iplo;
        ip->iplo = 0;
    }

}

void rshift_ipv6(struct ip_v6 *ip, uint8_t bits)
{
    uint64_t tail;
    if (bits < 64) {
        tail = ip->iphi << (64 -bits);
        ip->iphi >>= bits;
        ip->iplo >>= bits;
        ip->iplo |= tail;
    }
    else if (likely(bits > 64)) {
        tail = ip->iphi >> (bits - 64);
        ip->iplo = tail;
        ip->iphi = 0;
    }
    else {
        ip->iplo = ip->iphi;
        ip->iphi = 0;
    }

}


//notes: cidr > 0 
//if cidr == 0, then the ip must be 0
//or something terrible will happan
//this is due to a strange implementation
//that ip32<<32 == ip32 ip64 << 64 == ip64
int bitmapv6_insert_prefix(
        struct mb_node *node,
        struct mm *m, 
        struct ip_v6 ip, int cidr, 
        void *nhi
        )
{
    uint8_t pos;
    uint8_t stride;
    uint8_t level=0;
    struct ip_v6 iptmp;
    void **i;
    int ret;
    struct rollback_stash stash = {.stack = 0};

    for (;;) {
        iptmp = ip;
        if (cidr < STRIDE) {
            // if the node has the prefix already
            //need to be atomic
            //
            rshift_ipv6(&iptmp, LENGTH_v6 - cidr);
            stride = iptmp.iplo;
            pos = count_inl_bitmap(stride, cidr);
            if(test_bitmap(node->internal, pos)) {
                //already has the rule, need to update the rule
                //i = (void**)node->child_ptr - count_ones(node->internal, pos) -1;
                i = pointer_to_nhi(node, pos);
                *i = nhi;
                return 1;
            }
            else {
                //update_inl_bitmap(node, pos);
                //rules pos starting at 1, so add 1 to offset
                //pos = count_ones(node->internal, pos) + 1;
                ret = extend_rule(m, node, pos, level, nhi, &stash);
                if(ret == -1) {
                    rollback_stash_rollback(&stash);
                    return -1;
                }
                break;
            }
        }
        //push the "cidr == stride" node into the next child
        else {
            rshift_ipv6(&iptmp, LENGTH_v6 - STRIDE);
            stride = iptmp.iplo;
            pos = count_enl_bitmap(stride);

            if (test_bitmap(node->external, pos)){
                //node = (struct mb_node*) node->child_ptr + count_ones(node->external, pos); 
                node = next_child(node, pos);
            } 
            else {

                //update_enl_bitmap(node, pos);
                //iteration
                //child pos starting at 0, so add 0 to offset
                //pos = count_ones(node->external, pos);
                node = extend_child(m, node, level, pos, &stash);
                if(!node) {
                    rollback_stash_rollback(&stash);
                    return -1;
                }
            }
            cidr -= STRIDE;
            lshift_ipv6(&ip, STRIDE);
            level ++;
            //ip <<= STRIDE;
        }
    }

    rollback_stash_clear(&stash);
    return 0;
}

int bitmapv6_traverse_branch(struct mb_node *node, struct ip_v6 ip, int cidr,
        traverse_func func, void *user_data)
{

    uint8_t pos;
    uint8_t stride;
    struct ip_v6 iptmp;
    int ret;

    for (;;) {

        iptmp = ip;
        if (cidr < STRIDE) {
            rshift_ipv6(&iptmp, LENGTH_v6 - cidr);
            stride = iptmp.iplo;
            pos = count_inl_bitmap(stride, cidr);

            ret = func(node, stride, pos, LEAF_NODE, user_data);

            break;
        }
        //push the "cidr == stride" node into the next child
        else {
            rshift_ipv6(&iptmp, LENGTH_v6 - STRIDE);
            stride = iptmp.iplo;
            pos = count_enl_bitmap(stride);

            ret = func(node, stride, pos, MID_NODE, user_data);
            if(ret != TRAVERSE_CONT)
                break;

            //node = (struct mb_node*) node->child_ptr + count_ones(node->external, pos); 
            node = next_child(node, pos);

            cidr -= STRIDE;
            //ip <<= STRIDE;
            lshift_ipv6(&ip, STRIDE);
        }
    }

    return ret;
}

int bitmapv6_delete_prefix(struct mb_node *node, struct mm *m, 
        struct ip_v6 ip, int cidr,
        void (*destroy_nhi)(void *nhi))
{
    uint8_t pos;
    uint8_t stride;
    struct ip_v6 iptmp;
    struct trace trace_node[UPDATE_LEVEL_v6];
    int i = 0;
    int ret;
    struct rollback_stash stash = {.stack = 0};

    for (;;) {

        iptmp = ip;
        if (cidr < STRIDE) {
            // if the node has the prefix already
            //need to be atomic
            //
            rshift_ipv6(&iptmp, LENGTH_v6 - cidr);
            stride = iptmp.iplo;
            pos = count_inl_bitmap(stride, cidr);

            if (destroy_nhi) {
                void **nhi;
                //nhi = (void **)node->child_ptr - count_ones(node->internal,
                //        pos) - 1;
                nhi = pointer_to_nhi(node, pos);
                destroy_nhi(*nhi);
            }

            trace_node[i].node = node;
            trace_node[i].pos =  pos; 

            break;
        }
        //push the "cidr == stride" node into the next child
        else {
            rshift_ipv6(&iptmp, LENGTH_v6 - STRIDE);
            stride = iptmp.iplo;
            pos = count_enl_bitmap(stride);

            
            trace_node[i].node = node;
            trace_node[i].pos  = pos; 

            //node = (struct mb_node*) node->child_ptr + count_ones(node->external, pos); 
            node = next_child(node, pos);

            cidr -= STRIDE;
            //ip <<= STRIDE;
            lshift_ipv6(&ip, STRIDE);
        }
        i++;
    }

    ret = update_nodes(m, trace_node, i, &stash);
    if( ret != -1) {
        rollback_stash_clear(&stash);
    }
    return ret;
}


//return 1 means the prefix exists
int bitmapv6_prefix_exist(struct mb_node *node, 
        struct ip_v6 ip, uint8_t cidr)
{
    int ret;
    ret = bitmapv6_traverse_branch(node, ip, cidr, prefix_exist_func, NULL);
    return ret;
}


   
void * bitmapv6_do_search(struct mb_node *n, struct ip_v6 ip)
{
    uint8_t stride;
    int pos;
    struct ip_v6 iptmp;
    void **longest = NULL;
    //int depth = 1;

    for (;;){
        iptmp = ip;
        rshift_ipv6(&iptmp, LENGTH_v6 - STRIDE);
        stride = iptmp.iplo;
        pos = tree_function(n->internal, stride);

        if (pos != -1){
            //longest = (void**)n->child_ptr - count_ones(n->internal, pos) - 1;
            longest = pointer_to_nhi(n, pos);
        }
        if (test_bitmap(n->external, count_enl_bitmap(stride))) {
            //printf("%d %p\n", depth, n);
            //n = (struct mb_node*)n->child_ptr + count_ones(n->external, count_enl_bitmap(stride));
            n = next_child(n, count_enl_bitmap(stride));
            lshift_ipv6(&ip, STRIDE);
            //ip = (uint32_t)(ip << STRIDE);
            //depth ++;
        }
        else {
            break;
        }
    }
//    printf("depth %d\n",depth);
    return (longest == NULL)?NULL:*longest;
}



uint8_t bitmapv6_detect_overlap_generic(struct mb_node *n, 
        struct ip_v6 ip, uint8_t cidr, 
        uint32_t bits_limit, void **nhi_over)
{
    //uint8_t ret;
    uint8_t stride;
    uint8_t mask = 0;
    uint8_t final_mask = 0;
    uint8_t curr_mask = 0;

    uint8_t step = 0;
    struct ip_v6 iptmp;
    int pos;
    void **longest = NULL;

    //limit the bits to detect
    int limit;
    int limit_inside;
    int org_limit;
    
    limit = (cidr > bits_limit) ? bits_limit : cidr;
    org_limit = limit;
    while(limit>0) {
        iptmp = ip;
        rshift_ipv6(&iptmp, LENGTH_v6 - STRIDE);
        stride = iptmp.iplo;
        limit_inside = (limit > STRIDE) ? STRIDE: limit;
        pos = find_overlap_in_node(n->internal, stride, &mask, limit_inside);

        if (pos != -1){
            curr_mask = step * STRIDE + mask;
            if (curr_mask < org_limit) {  
                final_mask = curr_mask;
                //longest = (void**)n->child_ptr - count_ones(n->internal, pos) - 1;
                longest = pointer_to_nhi(n, pos);
            }
        }

        limit -= STRIDE;
        step ++;

        if (test_bitmap(n->external, count_enl_bitmap(stride))) {
            //printf("%d %p\n", depth, n);
            //n = (struct mb_node*)n->child_ptr + count_ones(n->external, count_enl_bitmap(stride));
            n = next_child(n, count_enl_bitmap(stride));
            lshift_ipv6(&ip, STRIDE);
            //ip = (uint32_t)(ip << STRIDE);
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


//skip node as far as possible
//don't check the internal bitmap as you have to check

static __thread struct lazy_travel lazy_mark[LEVEL_v6]; 

void * bitmapv6_do_search_lazy(struct mb_node *n, struct ip_v6 ip)
{
    uint8_t stride;
    int pos;
    void **longest = NULL;
    struct ip_v6 iptmp;
    int travel_depth = -1;
//    int depth = 1;

    for (;;){
        iptmp = ip;
        rshift_ipv6(&iptmp, LENGTH_v6 - STRIDE);
        stride = iptmp.iplo;

        if (likely(test_bitmap(n->external, count_enl_bitmap(stride)))) {
            travel_depth++;
            lazy_mark[travel_depth].lazy_p = n;
            lazy_mark[travel_depth].stride = stride;
            //n = (struct mb_node*)n->child_ptr + count_ones(n->external, count_enl_bitmap(stride));
            n = next_child(n, count_enl_bitmap(stride));
            //ip = (uint32_t)(ip << STRIDE);
            lshift_ipv6(&ip, STRIDE);
//           depth ++;
        }
        else {

            //printf("1 check node %d\n", travel_depth);
            pos = tree_function(n->internal, stride);
            if (pos != -1) {
                longest = (void**)n->child_ptr - count_ones(n->internal, pos) - 1;
                //already the longest match 
                goto out;
            }

            for(;travel_depth>=0;travel_depth --) {
                n = lazy_mark[travel_depth].lazy_p;
                stride = lazy_mark[travel_depth].stride;
                pos = tree_function(n->internal, stride);
                if (pos != -1) {
                    //longest = (void**)n->child_ptr - count_ones(n->internal, pos) - 1;
                    longest = pointer_to_nhi(n, pos);
                    //printf("2 check node %d\n", travel_depth);
                    //already the longest match 
                    goto out;
                }
            }
            //anyway we have to go out
            break;
        }
    }
//    printf("depth %d\n",depth);
out:
    return (longest == NULL)?NULL:*longest;

}


struct print_key {
    struct ip_v6 ip;
    uint32_t cidr;
};

static inline void swap(unsigned char *ip, int i, int j)
{
    unsigned char tmp;
    tmp = ip[i];
    ip[i] = ip[j];
    ip[j] = tmp;
}

void hton_ipv6(struct in6_addr *ip)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    int i = 0;
    for(;i<LENGTH_v6/16;i++) 
        swap((unsigned char *)ip,i,LENGTH_v6/8 - i - 1);
#endif

}

static void print_ptr(struct print_key *key, void (*print_next_hop)(void *nhi), void *nhi)
{
    struct in6_addr addr;
    char str[INET6_ADDRSTRLEN];

    memcpy(&addr, &key->ip, sizeof(addr));
    hton_ipv6(&addr);

    inet_ntop(AF_INET6, (const void *)&addr, str, INET6_ADDRSTRLEN);

    printf("%s/%d ",str , key->cidr);

    if (print_next_hop)
        print_next_hop(nhi);

    printf("\n");
}

static void print_mb_node_iter(struct mb_node *node, 
        struct ip_v6 ip, uint32_t left_bits, 
        uint32_t cur_cidr, void (*print_next_hop)(void *nhi)
        )
{
    int bit=0;
    int cidr=0;
    int stride = 0;
    int pos;
    void **nhi;
    struct mb_node *next;
    struct print_key key;

    struct ip_v6 stride_bits = {0,0};
    struct ip_v6 iptmp;

    //internal prefix first
    for (cidr=0;cidr<= STRIDE -1;cidr ++ ){
        for (bit=0; bit< (1<<cidr); bit++) {
            pos = count_inl_bitmap(bit,cidr);
            if (test_bitmap(node->internal, pos)) {
                //nhi = (void**)node->child_ptr - count_ones(node->internal, pos) - 1;
                nhi = pointer_to_nhi(node, pos);

                //here the ugly code
                stride_bits.iplo = bit;
                stride_bits.iphi = 0;
                iptmp = ip;
                lshift_ipv6(&stride_bits, left_bits - cidr);
                iptmp.iphi |= stride_bits.iphi;
                iptmp.iplo |= stride_bits.iplo;
                //end
                
                key.ip = iptmp;
                key.cidr = cur_cidr + cidr; 
                print_ptr(&key, print_next_hop, *nhi); 
            }
        }
    }

    memset(&stride_bits, 0, sizeof(stride_bits));

    for (stride = 0; stride < (1<<STRIDE); stride ++ ){
        pos = count_enl_bitmap(stride);
        if (test_bitmap(node->external, pos)) {

            //here the ugly code
            stride_bits.iplo = stride;
            stride_bits.iphi = 0;
            iptmp = ip;
            lshift_ipv6(&stride_bits, left_bits - STRIDE);
            iptmp.iphi |= stride_bits.iphi;
            iptmp.iplo |= stride_bits.iplo;
            //end
            
            //next = (struct mb_node *)node->child_ptr + count_ones(node->external, pos);
            next = next_child(node, pos);
            print_mb_node_iter(next, iptmp, left_bits - STRIDE, cur_cidr + STRIDE, print_next_hop); 
        }
    }
}


void bitmapv6_print_all_prefix(struct mb_node *n, void (*print_next_hop)(void *nhi)) 
{
    struct ip_v6 nullip = {0, 0};
    print_mb_node_iter(n, nullip, LENGTH_v6, 0, print_next_hop);  
}

uint8_t bitmapv6_detect_overlap(struct mb_node *n, struct ip_v6 ip, uint8_t cidr, void **nhi_over)
{
    return bitmapv6_detect_overlap_generic(n, ip, cidr, LENGTH_v6, nhi_over);
}

void bitmapv6_destroy_trie(struct mb_node *root,
        struct mm *m, void (*destroy_nhi)(void *nhi))
{
    destroy_subtrie(root, m, destroy_nhi, 0);
}

/* follow ip/cidr to copy involved part of
 * the tree
 */

int bitmap_copy_branch_v6(struct mb_node *node, 
        struct mm *m, 
        struct ip_v6 ip, int cidr, struct copy_stash *stash)

{
    uint8_t pos;
    uint8_t stride;
    uint8_t level = 0;
    int offset = 0;
    int ret = 0;
    struct ip_v6 iptmp;

    copy_stash_init(stash, m);
    ret = copy_stash_copy_root(stash, node, level);
    if(ret < 0)
        return ret;

    for (;;) {
        if (unlikely(cidr < STRIDE)) {
            /* the last offset is useless */
            ret = copy_stash_copy_children(stash, node, 0, level); 
            break;
        } else {
            //push the "cidr == stride" node into the next child
            iptmp = ip;
            rshift_ipv6(&iptmp, LENGTH_v6 - STRIDE);
            stride = iptmp.iplo;
            pos = count_enl_bitmap(stride);

            if (test_bitmap(node->external, pos)) {
                offset = count_ones(node->external, pos);
                ret = copy_stash_copy_children(stash, node, offset, level);
                if(ret < 0)
                    break;
                node = next_child(node, pos);
            } else {
                break;
            }
            cidr -= STRIDE;
            lshift_ipv6(&ip, STRIDE);
            level ++;
        }
    }

    if(ret < 0) {
        copy_stash_free_new(stash);
        return ret;
    }

    return ret;
}

/* first copy the branch that will be updated,
 * then use the normal insert/delete function 
 * to update the copied part
 *
 * after finish the update, switch the root
 * pointer then free the old branch.
 */

int bitmap_insert_prefix_read_copy_v6(struct mb_node *root,
        struct mm *m, struct ip_v6 ip, int cidr, void *nhi, 
        struct copy_stash *stash)
{
    int ret;
    ret = bitmap_copy_branch_v6(root, m, ip, cidr, stash);
    if(ret < 0)
        return ret;

    struct mb_node *new_root = stash->nodes[0].new_node;
    ret = bitmapv6_insert_prefix(new_root, m, ip, cidr, nhi);
    if(ret < 0) {
        copy_stash_free_new(stash);
        return ret;
    }

    return 0;
}

int bitmap_delete_prefix_read_copy_v6(struct mb_node *root, 
        struct mm *m, struct ip_v6 ip, int cidr, \
        void (*destroy_nhi)(void *nhi), \
        struct copy_stash *stash)
{
    int ret;
    ret = bitmap_copy_branch_v6(root, m, ip, cidr, stash);
    if(ret < 0)
        return ret;

    struct mb_node *new_root = stash->nodes[0].new_node;
    ret = bitmapv6_delete_prefix(new_root, m, ip, cidr, destroy_nhi);
    if(ret < 0) {
        copy_stash_free_new(stash);
        return ret;
    }

    return 0;
}

void bitmap_rcu_after_update_v6(struct copy_stash *stash)
{
    copy_stash_free_old(stash);
}


