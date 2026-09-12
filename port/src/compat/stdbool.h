/* Shadows clang's <stdbool.h> for the game library only.
 * The decomp's MSL stdbool.h (src/MSL/stdbool.h) makes bool a 4-byte int, and
 * the game's struct layouts depend on that. Port-only sources under port/src
 * (which talk to Aurora's C++ side) do not use this directory. */
#ifndef PORT_COMPAT_STDBOOL_H
#define PORT_COMPAT_STDBOOL_H
#define __STDBOOL_H 1
#define __bool_true_false_are_defined 1
typedef int bool;
#define true 1
#define false 0
#endif
