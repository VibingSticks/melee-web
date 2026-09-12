#include "formats.h"

static uint32_t bswap32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}

static uint16_t bswap16(uint16_t v)
{
    return (uint16_t) ((v >> 8) | (v << 8));
}

void port_swap_u32_array(uint32_t* p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        p[i] = bswap32(p[i]);
    }
}

void port_swap_u16_array(uint16_t* p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        p[i] = bswap16(p[i]);
    }
}

size_t port_swap_ssm_table(uint32_t* table, uint32_t groups)
{
    uint32_t* p = table;
    for (uint32_t g = 0; g < groups; g++) {
        uint32_t n;
        p[0] = bswap32(p[0]);
        p[1] = bswap32(p[1]);
        n = p[0];
        p += 2;
        for (uint32_t k = 0; k < n; k++) {
            port_swap_u32_array(p, 4);                     /* 0x00..0x0F */
            port_swap_u16_array((uint16_t*) (p + 4), 2);   /* loopFlag, format */
            port_swap_u32_array(p + 5, 3);                 /* loop, end, current */
            port_swap_u16_array((uint16_t*) (p + 8), 16);  /* ADPCM coefficients */
            p += 16;
        }
    }
    return (size_t) ((uint8_t*) p - (uint8_t*) table);
}

size_t port_swap_sem_header(uint32_t* file)
{
    uint32_t* p = file;
    for (int run = 0; run < 4; run++) {
        uint32_t n;
        p[0] = bswap32(p[0]);
        n = p[0];
        port_swap_u32_array(p + 1, n);
        p += 1 + n;
    }
    return (size_t) ((uint8_t*) p - (uint8_t*) file);
}
