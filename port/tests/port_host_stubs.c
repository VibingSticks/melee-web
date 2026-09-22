/* Host implementations of port.h for unit tests. */
#include "port.h"

#include <stdarg.h>
#include <stdio.h>

void port_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[melee] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void port_yield(void) {}

void port_request_exit(void) {}

/* dvd_web.c provides the real one; tests that link it get that definition. */
__attribute__((weak)) const char* port_disc_file_at(const void* buf)
{
    (void) buf;
    return NULL;
}

/* Aurora's ARAM queue is not linked into host tests; nothing is ever deferred. */
void ARQPumpCallbacks(void) {}
void CARDPumpCallbacks(void) {}
