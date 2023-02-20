#include <assert.h>
#include <stdio.h>

#define MEM_ASSERT assert
#define MEM_IMPL
#include "mem.h"

#pragma warning(disable : 4201)

int
main(void)
{
    MemArena *mem = memBootstrap(1024 * 1024 * 1024);
    assert(mem && "Reserve failed");

    MemSpan(uint32_t) ints = {.len = 128};
    ints.ptr = memAlloc(uint32_t, mem, ints.len);

    assert(ints.ptr && "Alloc failed");

    MemBuf(int32_t) buf = {0};

    for (int32_t i = 0; i < 10; ++i)
    {
        // Push
        assert(memBufReserveOne(&buf, mem));
        buf.ptr[buf.len++] = i;
    }

    assert(buf.len == 10);
    for (int32_t i = 0; i < buf.len; ++i)
    {
        assert(buf.ptr[i] == i);
    }

    assert(memBufInsert(&buf, 10, 4, mem));
    assert(buf.ptr[4] == 10);
    assert(buf.ptr[5] == 4);

    return 0;
}
