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
