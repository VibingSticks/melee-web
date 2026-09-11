#include "check.h"

static void run(void)
{
    CHECK(1 + 1 == 2);
    CHECK_EQ_U32(0x12345678u, 0x12345678u);
}

TEST_MAIN(run)
