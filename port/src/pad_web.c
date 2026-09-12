/* Controller input for the browser build.
 *
 * Gamepads reach the game through Aurora's SDL3 pad layer untouched. On top of
 * that this file gives player 1 a keyboard layout (Aurora keeps per-port key
 * bindings but ships none) and exposes Aurora's "virtual pad" to JavaScript
 * so on-screen controls and the browser tests can press buttons.
 *
 * Keyboard layout, port 1 (Dolphin's classic defaults):
 *   arrows       main stick        I J K L    C-stick
 *   X            A                 Z          B
 *   C            X                 S          Y
 *   D            Z                 Q / W      L / R
 *   T F G H      D-pad             Enter      Start
 */
#include "pad_web.h"

#include <emscripten.h>
#include <string.h>

#include <dolphin/pad.h>

#include "port.h"

/* USB HID usage ids, which SDL uses as scancodes. */
enum {
    KEY_A = 4, KEY_B = 5, KEY_C = 6, KEY_D = 7, KEY_E = 8, KEY_F = 9, KEY_G = 10, KEY_H = 11, KEY_I = 12,
    KEY_J = 13, KEY_K = 14, KEY_L = 15, KEY_Q = 20, KEY_S = 22, KEY_T = 23, KEY_V = 25, KEY_W = 26,
    KEY_X = 27, KEY_Z = 29, KEY_RETURN = 40, KEY_RIGHT = 79, KEY_LEFT = 80, KEY_DOWN = 81, KEY_UP = 82,
};

static void bind_button(u32 port, s32 key, PADButton button)
{
    PADKeyButtonBinding b = { key, button };
    PADSetKeyButtonBinding(port, b);
}

static void bind_axis(u32 port, s32 key, PADAxis axis)
{
    PADKeyAxisBinding b = { key, axis, 100 };
    PADSetKeyAxisBinding(port, b);
}

void port_pad_install_keyboard(void)
{
    const u32 port = 0;
    bind_button(port, KEY_X, PAD_BUTTON_A);
    bind_button(port, KEY_Z, PAD_BUTTON_B);
    bind_button(port, KEY_C, PAD_BUTTON_X);
    bind_button(port, KEY_S, PAD_BUTTON_Y);
    bind_button(port, KEY_D, PAD_TRIGGER_Z);
    bind_button(port, KEY_Q, PAD_TRIGGER_L);
    bind_button(port, KEY_W, PAD_TRIGGER_R);
    bind_button(port, KEY_RETURN, PAD_BUTTON_START);
    bind_button(port, KEY_T, PAD_BUTTON_UP);
    bind_button(port, KEY_G, PAD_BUTTON_DOWN);
    bind_button(port, KEY_F, PAD_BUTTON_LEFT);
    bind_button(port, KEY_H, PAD_BUTTON_RIGHT);
    bind_axis(port, KEY_UP, PAD_AXIS_LEFT_Y_POS);
    bind_axis(port, KEY_DOWN, PAD_AXIS_LEFT_Y_NEG);
    bind_axis(port, KEY_LEFT, PAD_AXIS_LEFT_X_NEG);
    bind_axis(port, KEY_RIGHT, PAD_AXIS_LEFT_X_POS);
    bind_axis(port, KEY_I, PAD_AXIS_RIGHT_Y_POS);
    bind_axis(port, KEY_K, PAD_AXIS_RIGHT_Y_NEG);
    bind_axis(port, KEY_J, PAD_AXIS_RIGHT_X_NEG);
    bind_axis(port, KEY_L, PAD_AXIS_RIGHT_X_POS);
    bind_axis(port, KEY_Q, PAD_AXIS_TRIGGER_L);
    bind_axis(port, KEY_W, PAD_AXIS_TRIGGER_R);
    PADSetKeyboardActive(port, TRUE);
    port_log("keyboard bound to controller port 1");
}

/* JavaScript entry points (Module._port_pad_virtual / _port_pad_virtual_clear).
 * Sticks are -127..127, triggers 0..255, buttons are PAD_BUTTON_* bits. */
EMSCRIPTEN_KEEPALIVE void port_pad_virtual(int port, int buttons, int stick_x, int stick_y, int substick_x,
                                           int substick_y, int trigger_l, int trigger_r)
{
    PADStatus st;
    memset(&st, 0, sizeof st);
    st.button = (u16) buttons;
    st.stickX = (s8) stick_x;
    st.stickY = (s8) stick_y;
    st.substickX = (s8) substick_x;
    st.substickY = (s8) substick_y;
    st.triggerLeft = (u8) trigger_l;
    st.triggerRight = (u8) trigger_r;
    st.err = PAD_ERR_NONE;
    PADSetVirtualStatus((u32) port, &st);
}

EMSCRIPTEN_KEEPALIVE void port_pad_virtual_clear(int port)
{
    PADClearVirtualStatus((u32) port);
}
