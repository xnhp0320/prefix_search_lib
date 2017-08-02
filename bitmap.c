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

int update_nodes(struct mm *mm, struct trace *t, int total)
{
    int i;
    int node_need_to_del = 0;
    int ret;
    for(i=total;i >= 0;i--){
        if(i==total){

            ret = reduce_rule(mm, t[i].node, t[i].pos, i);
            if(ret == -1) {
                rollback_stash_rollback();
                return -1;
            }

            if((t[i].node)->internal == 0 && (t[i].node)->external == 0)
            {
                node_need_to_del = 1;
            }
        }
        else{
            if(node_need_to_del){
                ret = reduce_child(mm, t[i].node, t[i].pos, i);
                if(ret == -1) {
                    rollback_stash_rollback();
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

