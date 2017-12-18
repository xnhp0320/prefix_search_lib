#ifdef TEST_ROLLBACK
#include "bitmap_v4.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>



/* override alloc functions */
static int alloc_times = 0;

void * alloc_node(struct mm *m, int nb_node, int level)
{
    if(nb_node == 0)
        return NULL;

    if(alloc_times == 3)
        /* manually cause alloc fail */
        return NULL;

    alloc_times ++;

    void * ret = calloc(nb_node, NODE_SIZE);
    if(ret) {
        m->ms.mem += nb_node * NODE_SIZE;
        m->ms.node += nb_node;
        m->ms.lmem[level] += nb_node * NODE_SIZE;
        m->ms.lnode[level] += nb_node;
    }

    return ret;
}

void test_1(void) {
    alloc_times = 0;
    uint32_t ip = inet_network("192.168.0.0"); 
    uint32_t cidr = 24;

    struct mb_node root = {0,0,NULL};
    struct mm m;
    memset(&m, 0, sizeof(m));

    int ret = bitmap_insert_prefix(&root, &m, ip, cidr, (void*)1);
    assert(ret == -1);
    assert(root.internal == 0 && root.external == 0 && \
            root.child_ptr == NULL);
    assert(m.ms.mem == 0 && m.ms.node == 0);
}

void test_2() {
    alloc_times = 0;
    uint32_t ip = inet_network("192.0.0.0"); 
    uint32_t cidr = 8;

    struct mb_node root = {0,0,NULL};
    struct mm m;
    memset(&m, 0, sizeof(m));

    int ret = bitmap_insert_prefix(&root, &m, ip, cidr, (void*)1);
    assert(ret == 0);
}


int main() {
    test_1();
    test_2();

    return 0;
}
#endif
