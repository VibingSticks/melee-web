/* The fighters' item Articles (port_swap_ft_article, see port_game.h).
 *
 * Lives beside the game hooks rather than in hsd_endian: it names two
 * generated schema types the host tests' stand-in tables do not define. */
#include <string.h>

#include <port_game.h>

#include "../hsd_endian/archive_swap.h"
#include "../hsd_endian/schema.h"

/* ftData x48, registered by it_8026B3F8:
 * `slot` is the kind relative to It_Kind_Kuriboh, which is how ItCo.dat's
 * x8 table is indexed too, so that table's per-kind element types name the
 * attribute block; kinds it leaves untyped are converted as a plain Article,
 * their attributes staying big-endian. */
extern const port_type port_T_Article;
extern const port_type port_T_it_804D6D20_t;

void port_swap_ft_article(void* article, int slot)
{
    const port_type* t = &port_T_Article;
    const port_type* tab = &port_T_it_804D6D20_t;
    for (uint32_t i = 0; i < tab->nfields; i++) {
        const port_field* f = &tab->fields[i];
        if (f->kind == F_PTR_LIST && f->name != NULL && strcmp(f->name, "x8") == 0 && slot >= 0 &&
            (uint32_t) slot < f->len && f->disc_types[slot] != NULL)
        {
            t = f->disc_types[slot];
            break;
        }
    }
    if (article != NULL) {
        port_archive_swap_object(article, t, "fighter article");
    }
}
