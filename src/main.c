#include <assert.h>

#define BUF_ASSERT assert
#define BUF_IMPL
#include "buf.h"

#pragma warning(disable : 4201)

int
main(void)
{
    MemArena *mem = memBootstrap(1024 * 1024 * 1024);
    assert(mem && "Reserve failed");

    Span(uint32_t) ints = {.len = 128};
    ints.ptr = memAlloc(uint32_t, mem, ints.len);

    assert(ints.ptr && "Alloc failed");

    Buf(int32_t) buf = {0};

    for (int32_t i = 0; i < 10; ++i)
    {
        assert(bufPush(&buf, i, mem));
    }

    for (int32_t i = 0; i < buf.len; ++i)
    {
        assert(buf.ptr[i] == i);
    }

    return 0;
}
