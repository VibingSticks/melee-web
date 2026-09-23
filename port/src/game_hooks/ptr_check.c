/* Cheap "is this even a heap address" test for diagnostics in game code:
 * an address outside the wasm heap would trap on the next load, so callers
 * log what they know about the object first. */
#include <emscripten/heap.h>
#include <stdint.h>

#include <port_game.h>

int port_ptr_ok(const void* p)
{
    uintptr_t a = (uintptr_t) p;
    return a >= 4096 && a + 16 <= emscripten_get_heap_size();
}
