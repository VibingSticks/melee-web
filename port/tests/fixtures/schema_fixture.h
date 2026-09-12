/* port/tests/fixtures/schema_fixture.h: input for gen_schema_test.py */
#include <stdint.h>
typedef struct { uint16_t a; uint16_t b; float f; } Leaf;
typedef struct {
    uint32_t n;
    Leaf* items;
    Leaf* shared;
    uint32_t x : 3;
    uint32_t y : 5;
    uint32_t z : 24;
    float fs[3];
    union { float f; void* p; } w;
    char name[8];
    Leaf inl;
} Root;
