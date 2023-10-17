#define MEM_IMPLEMENTATION
#include "mem.h"

typedef struct Buf
{
    uint32_t *ptr;
    size_t len;
    size_t cap;
} Buf;

int
main(void)
{
    MemArena *mem = memReserve(&(MemReserveOptions){
        .total_size = MEM_GB(1),
    });

    return 0;
}