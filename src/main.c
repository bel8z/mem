#include <assert.h>

#define BUF_ASSERT assert
#define BUF_IMPL
#include "buf.h"

#pragma warning(disable : 4201)

int
main(void)
{
    MemArena *mem = memBootstrap(1024 * 1024 * 1024);
    BUF_ASSERT_MSG(mem, "Reserve failed");

    Span(uint32_t) ints = {.len = 128};
    ints.ptr = memAlloc(uint32_t, mem, ints.len);

    BUF_ASSERT_MSG(ints.ptr, "Alloc failed");

    Buf(int32_t) buf = {0};

    for (int32_t i = 0; i < 10; ++i)
    {
        BUF_ASSERT(bufPush(&buf, i, mem));
    }

    for (int32_t i = 0; i < buf.len; ++i)
    {
        BUF_ASSERT(buf.ptr[i] == i);
    }

    return 0;
}
