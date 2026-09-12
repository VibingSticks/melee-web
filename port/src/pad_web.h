/* Keyboard layout and JavaScript-driven virtual pad on top of Aurora's PAD. */
#ifndef PORT_PAD_WEB_H
#define PORT_PAD_WEB_H

/* Bind player 1's keyboard layout. Call after the game's PADInit (which
 * resets Aurora's key bindings); the first vblank is late enough. */
void port_pad_install_keyboard(void);

#endif
