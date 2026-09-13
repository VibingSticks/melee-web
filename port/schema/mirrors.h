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

/* ftwaitanim.c getAnimID() reads a fighter's wait-animation table as (anim id,
 * weight) pairs and stops at an id of -1. WaitStruct spells the pair as an
 * undiscriminated union of two int-sized members, which the walk cannot read;
 * both arms are two 32-bit words, so it swaps them as this. */
typedef struct {
    s32 anim_id;
    s32 weight;
} port_WaitEntry;

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

/* fighter.c (Fighter_LoadCommonData): "ftLoadCommonData" is an array of 23
 * pointers into PlCo.dat, copied one by one into the globals named beside each
 * field. Tables indexed by fighter kind have Ft_Kind_Max (33) entries. */
typedef struct { f32 v[5]; } port_Float5;

typedef struct {
    struct ftCommonData* common;                     /* p_ftCommonData */
    void* per_kind_int_lists;                        /* Fighter_804D6550 */
    port_Float5* float5_rows;                        /* Fighter_804D654C */
    f32* per_kind_floats;                            /* Fighter_804D6548 */
    struct FighterPartsTable** parts_tables;         /* ftPartsTable */
    struct Fighter_804D6540_t** virtual_parts;       /* Fighter_804D6540 */
    void* rumble_a;                                  /* Fighter_804D653C */
    void* rumble_b;                                  /* Fighter_804D6538 */
    void* p8;                                        /* Fighter_804D6534 */
    void* per_kind_vec2_lists;                       /* Fighter_804D6530 */
    struct Fighter_ShakeTable_t* grab_mash_shake;    /* Fighter_GrabMashShake */
    struct Fighter_ShakeTable_t* smash_charge_shake; /* Fighter_SmashChargeShakeTable */
    struct Fighter_804D6524_t* scale_mods;           /* Fighter_804D6524 */
    struct Fighter_804D6520_t* bunnyhood_mods;       /* Fighter_804D6520 */
    struct Fighter_804D651C_t* metal_mods;           /* Fighter_804D651C */
    struct Fighter_804D6518_t* p15;                  /* Fighter_804D6518 */
    struct HSD_Joint* trophy_platform;               /* Fighter_804D6514 */
    void* p17;                                       /* Fighter_804D6510 */
    u8* p18;                                         /* Fighter_804D650C */
    u8* p19;                                         /* Fighter_804D6508 */
    struct HSD_Joint* p20;                           /* Fighter_804D6504 */
    struct CrowdConfig* crowd;                       /* gCrowdConfig */
    struct Fighter_804D64FC_t* attack_tables;        /* Fighter_804D64FC */
} port_FtLoadCommonData;

/* efasync.c: an "eff<Name>DataTable" root is the two particle banks followed by
 * the effect descriptors, indexed by gfx id. Nothing records how many there
 * are, so the walk runs while the entries still look like descriptors. */
typedef struct {
    void* cmd_bank;
    void* tex_bank;
    struct EF_EffectDesc descs[1];
} port_EfDataTable;

/* --- ItCo.dat: item common data (it_3F14.c, iteffect.c) ------------------- */

/* Article::x14_dynamics. The header's ItemDynamics declares only the first
 * pair; itcoll.c (it_8027163C) reads a second {count, descs} pair at +8
 * through its file-local ItCollDynamics, and every block in ItCo.dat is 16
 * bytes. */
typedef struct {
    s32 bone_id;
    Vec3 offset;
    f32 size;
} port_ItCollDynamicsDesc;
typedef struct {
    s32 count;
    struct BoneDynamicsDesc* dyn_descs;      /* Item_80268560 */
    s32 coll_count;
    port_ItCollDynamicsDesc* coll_descs;     /* it_8027163C */
} port_ItemDynamics;

/* Per-kind attribute blocks (Article::x4_specialAttributes) whose structs the
 * game declares inside the kind's .c file. Copied field for field; each is
 * the size of its block in ItCo.dat. */
typedef struct {                             /* itgshell.c itGShell_Attrs */
    f32 x0, x4, x8, xC, x10, x14;
    u8 pad18[4];
    f32 x1C, x20, x24, x28, x2C, x30;
    Vec3 x34;
} port_itGShell_Attrs;
typedef struct {                             /* itrshell.c itRShell_Attrs */
    f32 x0, x4, x8, xC, x10;
    Vec3 x14;
    f32 x20, x24, x28, x2C;
    u8 pad30[8];
    f32 x38, x3C, x40, x44;
    Vec3 x48;
    s32 x54;
} port_itRShell_Attrs;
typedef struct { s32 x0; Vec3 x4; } port_StarRodAttributes;          /* itstarrod.c */
typedef struct { u32 x0; u32 x4; f32 x8; } port_itHammerData;         /* ithammer.c */
typedef struct {                             /* itstarrodstar.c StarRodStarAttrs */
    f32 x0, x4, x8, xC, x10;
    s32 x14, x18;
    f32 x1C;
} port_StarRodStarAttrs;
typedef struct { f32 x0, x4, x8, xC, x10, x14; } port_itMsBomb_Attrs; /* itmsbomb.c, ASSERT_SIZE 24 */

/* itkinoko.c: two floats, then the animation joints it_80293660 reads as
 * KinokoAnim rows from +8 (attrs[idx + 2].joint); KinokoAttrs declares x8 as
 * an s32, but the archive relocates it. Used by Kinoko and DKinoko. */
typedef struct {
    f32 x0;
    f32 x4;
    struct HSD_AnimJoint* anims[1];
} port_KinokoAttrs;

/* Article::x10_modelDesc of the kinds whose block is longer than an
 * ItemModelDesc: a table of model roots (Foods, Unknown -- one HSD_Joint tree
 * per sub-kind) or of animations (WStar) follows the descriptor and runs to
 * the next object. No decompiled code reads these through x10_modelDesc; the
 * element types are what the archive holds there (HSD_Joint class/flags
 * words, HSD_AnimJoint child/aobjdesc words). Unk4's table mixes joints with
 * other objects and is left alone. */
typedef struct {
    struct ItemModelDesc desc;
    struct HSD_Joint* joints[1];
} port_ItemModelDescJoints;
typedef struct {
    struct ItemModelDesc desc;
    struct HSD_AnimJoint* anims[1];
} port_ItemModelDescAnims;

/* A block the kind's code reads as bare floats (itparasol.c, itfflowerflame.c,
 * itcerebi.c, itlugia.c's aeroblasts): floats to the next object. */
typedef struct { f32 v[1]; } port_F32Run;

/* itfoods.c: one itFoodsAttributes row per food kind (it_8028F9D8 indexes the
 * block as Vec4 entries of the same size); the row count lives nowhere, so
 * the rows run to the next object. */
typedef struct { struct itFoodsAttributes rows[1]; } port_itFoodsAttrs;

#endif
