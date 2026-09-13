#include "sislib_font.h"

#ifdef TARGET_PC
/* The atlas is bitmap data from the executable, which this build has no copy
 * of; port/src/font_dol.c fills it from the player's disc at startup. */
TextGlyphTexture HSD_SisLib_FontAtlas[287] ATTRIBUTE_ALIGN(32);
#else
TextGlyphTexture HSD_SisLib_FontAtlas[] ATTRIBUTE_ALIGN(32) = {
#include <sysdolphin/baselib/sislib_font.inc>
};
#endif
