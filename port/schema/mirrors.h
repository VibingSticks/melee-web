/* Named mirrors of anonymous or file-local structs that archive root symbols
 * point at, and list shapes the C headers cannot express. gen_schema.py needs
 * a name to attach annotations to; the game keeps its own declarations.
 * Layouts are checked against the game's use sites, not against a header. */
#ifndef PORT_SCHEMA_MIRRORS_H
#define PORT_SCHEMA_MIRRORS_H

/* lbaudio_ax.c: lbAudioLoadData. Four language variants, each an array of
 * per-scene int lists terminated by 0x83D60. */
typedef struct { int* list; } port_IntListPtr;
typedef struct {
    port_IntListPtr* x0;
    port_IntListPtr* x4;
    port_IntListPtr* x8;
    port_IntListPtr* xC;
} port_lbAudioLoadData;

/* A single float behind a pointer (HSD_LightDesc.u.shininess). */
typedef struct { f32 v; } port_F32;

/* HSD_PObjDesc.u.envelope_p: NULL-terminated list of pointers to
 * {joint, weight} runs, each terminated by joint == NULL. */
typedef struct { HSD_EnvelopeDesc e[1]; } port_EnvelopeDescList;
typedef struct { port_EnvelopeDescList* p[1]; } port_EnvelopeList;

#endif
