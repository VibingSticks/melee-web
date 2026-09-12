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

/* mncharsel.c: MnSelectChrDataTable (the struct lives in the .c file). */
typedef struct {
    StaticModelDesc background, hand, token, menu, press_start, debug_camera, regend_menu, regend_options, door;
} port_MnSelectChrModels;
typedef struct {
    HSD_CObjDesc* cam;
    HSD_LightDesc* light0;
    HSD_LightDesc* light1;
    HSD_FogDesc* fog;
    port_MnSelectChrModels models;
} port_MnSelectChrDataTable;

/* gmevent.c: sqEventInitDataLevelTbl (struct gm_804D6900_t). */
typedef struct { int x0; int x4; } port_EventLevelPair;
typedef struct {
    unsigned char kind, flags, pad2[2];
    port_EventLevelPair* x4;
    void* evinit;
    void* evbonus;
    void* evstage_table;
    void* player_init[5];
} port_EventInitLevel;

/* grdatfiles.c: the "itemdata" stage root is a NULL-terminated array of
 * pointers to {count, Article*} pairs (StageInfo::itemdata's element type,
 * which is an anonymous struct in the header). */
typedef struct {
    s32 count;
    void* articles;
} port_GroundItemData;

/* player.c: "plLoadCommonData" is a pointer to the common player parameters. */
typedef struct { struct pl_804D6470_t* data; } port_PlLoadCommonDataRef;

/* ground.c (Ground_801C34AC): UnkStageDat::unk0 is an array of joint remap
 * entries, each naming a model root and a list of (tree index, slot) pairs. */
typedef struct { s16 target; s16 slot; } port_StageJointPair;
typedef struct {
    struct HSD_Joint* joint;
    port_StageJointPair* pairs;
    s32 pair_count;
} port_StageJointMap;

/* fighter.c: "ftLoadCommonData" is an array of 23 pointers into PlCo.dat.
 * Only the entries whose targets the port must read as native integers are
 * typed; the rest are relocated pointers the game uses as opaque data.
 * Entry 0 is the common fighter parameters, entry 4 the per-character bone
 * tables (indexed by CharacterKind). */
typedef struct {
    struct ftCommonData* common;
    void* p1;
    void* p2;
    void* p3;
    struct FighterPartsTable** parts_tables;
    void* rest[18];
} port_FtLoadCommonData;

#endif
