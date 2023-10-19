#include <assert.h>
#include <stdio.h>

#define MEM_ASSERT assert
#define MEM_IMPLEMENTATION
#include "../mem.h"

typedef struct Buf
{
    MemArena *mem;
    uint32_t *ptr;
    size_t len;
    size_t cap;
} Buf;

void
bufPush(Buf *buf, uint32_t value)
{
    buf->ptr = memBufAlloc(uint32_t, buf->mem, buf->ptr, buf->len + 1, &buf->cap);
    buf->ptr[buf->len++] = value;
}

bool
bufFree(Buf *buf)
{
    if (memBufFree(uint32_t, buf->mem, buf->ptr, buf->cap))
    {
        buf->len = buf->cap = 0;
        buf->ptr = NULL;
        return true;
    }

    return false;
}

int
main(void)
{
    bool result;

    MemArena *mem = memReserve(&(MemArenaInfo){
        .total_size = MEM_GB(1),
    });

    MemBlock block = memAlloc(mem, 1024, 8);
    MEM_ASSERT(block.ptr);

    result = memFree(mem, &block);
    MEM_ASSERT(result);
    MEM_ASSERT(!block.ptr);

    Buf buf = {.mem = mem};
    for (uint32_t i = 0; i < 10; ++i)
    {
        bufPush(&buf, i);
    }

    MEM_ASSERT(bufFree(&buf));

    return 0;
}
