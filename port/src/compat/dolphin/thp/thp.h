/* The game includes <dolphin/thp/thp.h> (its own SDK layout); Aurora's tree has
 * no such path. Forward to the decomp's header, and supply the two CodeWarrior
 * PowerPC intrinsics the decoder uses, which clang has no notion of.
 *
 * Both are single PowerPC instructions with simple semantics:
 *   dcbz   zeroes the 32-byte cache block holding an address
 *   cntlzw counts the leading zero bits of a 32-bit word, answering 32 for 0
 * (clang's __builtin_clz leaves zero undefined, hence the test). */
#ifndef PORT_COMPAT_DOLPHIN_THP_H
#define PORT_COMPAT_DOLPHIN_THP_H

#include <string.h> /* the decoder calls memset without declaring it */

#include "../../../../../extern/dolphin/include/dolphin/thp/thp.h"

#ifdef TARGET_PC
#define PORT_DCBZ_BLOCK 32

static inline void __dcbz(void* addr, int offset)
{
    unsigned char* p = (unsigned char*) addr + offset;
    /* dcbz acts on the block containing the address, not the address itself. */
    p -= (unsigned long) p % PORT_DCBZ_BLOCK;
    memset(p, 0, PORT_DCBZ_BLOCK);
}

static inline int __cntlzw(unsigned int v)
{
    return v != 0 ? __builtin_clz(v) : 32;
}
#endif

#endif
