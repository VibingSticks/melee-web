/* Minimal assert harness for host unit tests. No dependencies. */
#ifndef PORT_CHECK_H
#define PORT_CHECK_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
            check_failures++;                                                  \
        }                                                                      \
    } while (0)

#define CHECK_EQ_U32(a, b)                                                     \
    do {                                                                       \
        uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b);                       \
        if (_a != _b) {                                                        \
            fprintf(stderr, "%s:%d: %s == 0x%08x, expected 0x%08x\n",          \
                    __FILE__, __LINE__, #a, _a, _b);                           \
            check_failures++;                                                  \
        }                                                                      \
    } while (0)

#define CHECK_MEM_EQ(a, b, n)                                                  \
    do {                                                                       \
        if (memcmp((a), (b), (n)) != 0) {                                      \
            fprintf(stderr, "%s:%d: memory differs: %s vs %s\n", __FILE__,     \
                    __LINE__, #a, #b);                                         \
            check_failures++;                                                  \
        }                                                                      \
    } while (0)

#define TEST_MAIN(fn)                                                          \
    int main(void)                                                             \
    {                                                                          \
        fn();                                                                  \
        if (check_failures) {                                                  \
            fprintf(stderr, "%d failure(s)\n", check_failures);                \
            return 1;                                                          \
        }                                                                      \
        puts("ok");                                                            \
        return 0;                                                              \
    }

#endif
