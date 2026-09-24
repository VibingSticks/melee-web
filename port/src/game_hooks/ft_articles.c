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
extern const port_type port_T_port_F32Run;

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
        /* The attribute structs mark bytes they do not name as padding, and
         * the code sometimes reads them under another struct (Sheik's chain
         * reads a velocity at +0x50 of itSeakChain_Attrs' pad_4C). Sweep the
         * block for words the typed walk left big-endian. */
        void* attrs = ((void**) article)[1]; /* Article::x4_specialAttributes */
        if (attrs != NULL) {
            port_archive_fill_gaps(attrs, &port_T_port_F32Run, "fighter item attributes");
        }
    }
}

/* The entries of ftData x48 past the Articles are models the fighter's own
 * code loads (Samus' grab beam, Yoshi's egg shell, Kirby's star, Link's
 * sword-and-shield joint). The archive walk never reaches them either;
 * the fighter's OnLoad hands each one here with what it is. */
extern const port_type port_T_HSD_Joint;
extern const port_type port_T_HSD_AnimJoint;
extern const port_type port_T_HSD_MatAnimJoint;

void port_swap_ft_model(void* obj, int kind)
{
    static const port_type* const types[] = { &port_T_HSD_Joint, &port_T_HSD_AnimJoint, &port_T_HSD_MatAnimJoint };
    if (obj != NULL && kind >= 0 && kind < 3) {
        port_archive_swap_object(obj, types[kind], "fighter model");
    }
}
