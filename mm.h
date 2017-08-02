#ifndef __MM_H__
#define __MM_H__

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#define MAX_LEVEL 32

struct mem_stat {
    uint32_t mem;
    uint32_t node;
    uint32_t lmem[MAX_LEVEL];
    uint32_t lnode[MAX_LEVEL];
};


struct mm {
    struct mem_stat ms;
};


#endif
