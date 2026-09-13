/* Font atlases read from the player's disc (src/font_dol.c). */
#ifndef PORT_FONT_DOL_H
#define PORT_FONT_DOL_H

/* Where the two atlases live in the retail executable, from
 * config/GALE01/symbols.txt. The sizes there are the authority: the
 * sislib header rounds its element count up past the symbol. */
#define PORT_DEBUG_FONT_ADDR 0x804088B8u /* HSD_DebugFontAtlas */
#define PORT_DEBUG_FONT_SIZE 0x1C00u
#define PORT_SIS_FONT_ADDR 0x8040CD40u   /* HSD_SisLib_FontAtlas, 0x23E00 */
#define PORT_SIS_FONT_SIZE 0x23E00u

/* Fills both atlases. Call once, after the disc is available and before the
 * game runs. Logs and leaves them blank if the disc cannot supply them. */
void port_font_load_from_dol(void);

#endif
