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

/* mnstagesel.c: MnSelectStageDataTable (an anonymous struct at its one use
 * site, mnStageSel_Scene_OnEnter). Same shape as the character select's table:
 * camera, two lights, fog, then the models -- eleven static ones and a loose
 * joint with its three animations (struct mnStageSel_804D6C98_t). Without this
 * the stage select was only relocated, never byte-swapped, and Training died
 * on a garbage camera projection the moment MnSlMap loaded. */
typedef struct {
    StaticModelDesc models[11];
    HSD_Joint* joint;
    HSD_AnimJoint* animjoint;
    HSD_MatAnimJoint* matanim_joint;
    HSD_ShapeAnimJoint* shapeanim_joint;
} port_MnSelectStageModels;
typedef struct {
    HSD_CObjDesc* cam;
    HSD_LightDesc* light0;
    HSD_LightDesc* light1;
    HSD_FogDesc* fog;
    port_MnSelectStageModels models;
} port_MnSelectStageDataTable;

/* gmevent.c: sqEventInitDataLevelTbl is an array of 51 pointers to per-event
 * records (struct gm_804D6900_t), not one record. Typed as one, only the
 * first event's fields were touched and every event started from big-endian
 * data (event 1 asked for stage 0xCA00 and crashed in lbDvd_80018254). The
 * structs below copy gmevent.c's file-local ones. A record's x4 means
 * something different per event: most point at words ({30, 60}, one int, a
 * Vec3, a list of item kinds), events 13, 25 and 46 at byte lists of
 * character kinds, and event 43 at {count, player record}. */
typedef struct {
    u32 x0_0 : 3;
    u32 x0_3 : 3;
    u32 x0_6 : 1;
    u32 x0_7 : 1;
    u32 x1_0 : 1;
    u32 x1_1 : 1;
    u32 x1_2 : 1;
    u32 x1_3 : 1;
    u32 x1_4 : 1;
    u32 x1_5 : 3;
    u8 is_teams;
    s8 item_freq;
    s8 sd_penalty;
    u8 unk5;
    u16 stkind;
    u32 time_limit;
    u8 padC[4];
    u64 x10;
    s32 x18;
    f32 x1C;
    f32 game_speed;
    f32 unk24;
} port_EvInit;
typedef struct {
    s8 c_kind;
    u8 slot_type, stocks, color, x5, sub_color, team, xB, flags, xE, cpu_level, pad;
    u16 x12;
    u16 hp;
    f32 x18, x1C, x20;
} port_EvPlayer;
typedef struct {
    u8 count;
    u8 pad1;
    u16 stage[7];
    port_EvPlayer* entries[6];
} port_EvStageTable;
typedef struct {
    s8 c_kind;
    u8 x1, x2, x3, x4, x5, color, pad7;
    f32 x8, xC, x10;
    u8 flags, x15, x16, x17;
} port_EvBonus;
typedef struct {
    s32 count;
    port_EvPlayer* player;
} port_EvPair;
typedef struct { u32 w[1]; } port_EvWords; /* words to the next object */
#define PORT_EV_RECORD(name, x4_type)          \
    typedef struct {                           \
        u8 kind, flags, pad2[2];               \
        x4_type x4;                            \
        port_EvInit* evinit;                   \
        port_EvBonus* evbonus;                 \
        port_EvStageTable* evstage_table;      \
        port_EvPlayer* player_init[5];         \
    } name
PORT_EV_RECORD(port_EvRecW, port_EvWords*);
PORT_EV_RECORD(port_EvRecB, void*);
PORT_EV_RECORD(port_EvRecP, port_EvPair*);
typedef struct {
    port_EvRecW* e0;
    port_EvRecW* e1;
    port_EvRecW* e2;
    port_EvRecW* e3;
    port_EvRecW* e4;
    port_EvRecW* e5;
    port_EvRecW* e6;
    port_EvRecW* e7;
    port_EvRecW* e8;
    port_EvRecW* e9;
    port_EvRecW* e10;
    port_EvRecW* e11;
    port_EvRecW* e12;
    port_EvRecB* e13;
    port_EvRecW* e14;
    port_EvRecW* e15;
    port_EvRecW* e16;
    port_EvRecW* e17;
    port_EvRecW* e18;
    port_EvRecW* e19;
    port_EvRecW* e20;
    port_EvRecW* e21;
    port_EvRecW* e22;
    port_EvRecW* e23;
    port_EvRecW* e24;
    port_EvRecB* e25;
    port_EvRecW* e26;
    port_EvRecW* e27;
    port_EvRecW* e28;
    port_EvRecW* e29;
    port_EvRecW* e30;
    port_EvRecW* e31;
    port_EvRecW* e32;
    port_EvRecW* e33;
    port_EvRecW* e34;
    port_EvRecW* e35;
    port_EvRecW* e36;
    port_EvRecW* e37;
    port_EvRecW* e38;
    port_EvRecW* e39;
    port_EvRecW* e40;
    port_EvRecW* e41;
    port_EvRecW* e42;
    port_EvRecP* e43;
    port_EvRecW* e44;
    port_EvRecW* e45;
    port_EvRecB* e46;
    port_EvRecW* e47;
    port_EvRecW* e48;
    port_EvRecW* e49;
    port_EvRecW* e50;
} port_EventTable;

/* gmtoulib.c: TmBox.dat's tournament_box{2,3,4}_array roots are arrays of
 * BracketSrcEntry (gmtoulib.static.h, a file-local type): the bracket box
 * coordinates fn_8018A514 copies into lbl_80473AB8 for 8-, 13- and 64-entrant
 * brackets. The archive holds no length and the entries hold no pointers, so
 * each root runs until the next root symbol (or the end of the data). Without
 * this every s32 in them was read big-endian and the bracket was laid out
 * from garbage. */
typedef struct {
    u8 x0, x1, x2, x3, x4, x5, x6, pad7;
    s32 x8;
    s32 xC;
    s32 x10;
    s32 x14;
    u8 x18, x19, x1A, x1B, x1C, x1D, x1E, x1F, x20, x21, x22, x23, x24;
    u8 pad25[3];
} port_BracketSrcEntry;

/* grdatfiles.c: the "itemdata" stage root is a NULL-terminated array of
 * pointers to {kind, Article*} pairs (StageInfo::itemdata's element type,
 * which is an anonymous struct in the header); ground.c registers each
 * Article under its item kind with it_8026B40C. */
typedef struct {
    s32 kind;
    Article* article;
} port_GroundItemData;

/* A stage item's attribute block (the Articles in a stage's itemdata):
 * word 0 points at a small header -- an int (Shy Guy reads it as hit points
 * through `s32** attr`), three floats, an int and a pointer back to the
 * block -- and plain words follow. The decomp's structs declare word 0 as a
 * float, so a float run stopped at it and every stage item's block stayed
 * big-endian. */
typedef struct {
    s32 x0;
    f32 x4, x8, xC;
    s32 x10;
    void* back;
} port_StageItemHead;
typedef struct {
    port_StageItemHead* head;
    f32 rest[1];
} port_StageItemAttrs;

/* Great Bay's itemdata (GrGb.dat): its one entry is Tingle on his balloon
 * (It_Kind_Tincle), whose Article::x4_specialAttributes is ittincle.c's
 * itTincleAttributes. The generic itemdata rule leaves that block alone, so
 * the balloon's start position (xC = -60, x14 = 200) was read byte-swapped,
 * the item's CollData landed 4e8 units away and the stepped collision pass
 * (mpColl_80043754) then ran 71 million steps a frame: Great Bay took 30 s
 * per frame. x0 is relocated on the disc (the header declares f32; nothing
 * in ittincle.c reads it), so it is a pointer here. */
typedef struct {
    void* x0;
    s32 x4, x8;
    f32 xC, x10, x14, x18, x1C, x20, x24;
    s32 x28, x2C, x30;
    f32 x34, x38;
    s32 x3C, x40;
    f32 x44, x48, x4C, x50;
    s8 x54, x55;
} port_itTincleAttributes;
typedef struct {
    ItemAttr* x0_common_attr;
    port_itTincleAttributes* x4_specialAttributes;
    ItHurtBoneList* x8_hurtbones;
    ItemStateArray* xC_itemStates;
    ItemModelDesc* x10_modelDesc;
    ItemDynamics* x14_dynamics;
} port_GbArticle;
typedef struct {
    s32 kind;
    port_GbArticle* article;
} port_GbGroundItemData;

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

/* ground.c (find_light_override): UnkStageDat::unk18 is a table of light
 * override entries, one per light descriptor the stage overrides. The count
 * beside it (unk1C) is twice the row count in every stage archive on the disc
 * -- the PowerPC scan over-reads harmlessly into the tables packed after it --
 * so the rows are described as running to the next object instead. */
typedef struct {
    HSD_LightDesc* desc;
    u8 a : 1;
    u8 b : 1;
    u8 c : 1;
    u8 _ : 5;
    u8 _pad[3];
} port_LightOverrideEntry;

/* fighter.c (Fighter_LoadCommonData): "ftLoadCommonData" is an array of 23
 * pointers into PlCo.dat, copied one by one into the globals named beside each
 * field. Tables indexed by fighter kind have Ft_Kind_Max (33) entries. */
typedef struct { f32 v[5]; } port_Float5;
/* ftCo_ItemThrow.c's ftCo_ItemThrowAttrs: speed, angle and heavy-item
 * multiplier, one row per item-throw motion (LightThrowF .. HeavyThrowLw4). */
typedef struct { f32 speed; f32 angle; f32 heavy_mul; } port_ItemThrowRow;
/* ft_0D4D.c: the respawn platform's joint and animation. */
typedef struct { struct HSD_Joint* joint; struct HSD_AnimJoint* anim; } port_RespawnPlatform;
/* ftCo_DamageFall.c: hit-shake offsets, one row per kind of hit (air,
 * ground, electric), each a Vec2 list and its count. */
typedef struct { Vec2* shifts; u32 count; } port_HitShakeRow;

typedef struct {
    struct ftCommonData* common;                     /* p_ftCommonData */
    port_ItemThrowRow* item_throws;                  /* Fighter_804D6550 */
    port_Float5* float5_rows;                        /* Fighter_804D654C */
    f32* per_kind_floats;                            /* Fighter_804D6548 */
    struct FighterPartsTable** parts_tables;         /* ftPartsTable */
    struct Fighter_804D6540_t** virtual_parts;       /* Fighter_804D6540 */
    struct Fighter_804D653C_t* colanim_a;            /* Fighter_804D653C */
    struct Fighter_804D653C_t* colanim_b;            /* Fighter_804D6538 */
    port_RespawnPlatform* respawn_platform;          /* Fighter_804D6534 */
    port_HitShakeRow* hit_shake;                     /* Fighter_804D6530 */
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

/* --- Stage parameter blocks: the "yakumono_param" root --------------------
 * Every stage archive exports `yakumono_param`, and every stage reads it
 * through a struct declared inside its own gr*.c (or .static.h), so the
 * symbol alone says nothing about the layout: roots.yml scopes each rule to
 * its archive (`archive: GrCs.`). Each mirror is the game's declaration
 * copied field for field and checked against the disc: the object's size
 * (the distance to the next object) and the relocation slots inside it.
 *
 * Fields the game declares as int but the archive relocates are pointers --
 * colour-overlay scripts the stage plays on its background through
 * grMaterial_801C9604 -- and are wrapped in port_GrColorScript so the loader
 * finds and converts the script too (archive_swap.c collect_ground_scripts).
 * Padding the game never reads keeps its disc bytes (big-endian). */

typedef struct { void* script; } port_GrColorScript;

/* The stage device descriptors the gr*.c files declare as DynamicsDesc*
 * (the pointers a stage's ftDevice callback returns, and Pushon's x0) are
 * not DynamicsDesc on the disc: each points at a 0x24-byte block with no
 * relocated slot, holding {1, damage, angle, ...} -- and ftCo_800C08A0, the
 * only reader, reads it as a hit descriptor (lbColl_80008D30_arg1: state,
 * damage, kb_angle, ..., element, sfx) after taking `count` (+4, the damage)
 * through the DynamicsDesc name. Nine 32-bit words either way. */
typedef lbColl_80008D30_arg1 port_GrDeviceHit;

/* grbattle.c (GrNBa.dat, 8 bytes): two colour-overlay scripts. */
typedef struct {
    port_GrColorScript bg_curr_color_overlay;
    port_GrColorScript bg_prev_color_overlay;
} port_grBattle_YakumonoParam;

/* grbigblue.static.h grBb_YakumonoParam (GrBb.dat, 0x144 bytes). */
typedef struct {
    f32 x0, x4, x8, xC;
    s32 x10, x14, x18, x1C, x20;
    f32 x24, x28, x2C, x30, x34, x38, x3C, x40, x44, x48, x4C, x50, x54, x58, x5C, x60;
    u8 pad64[0x68 - 0x64];
    f32 x68, x6C, x70, x74, x78, x7C, x80, x84;
    s32 x88, x8C;
    f32 x90, x94, x98, x9C, xA0, xA4, xA8, xAC;
    s32 xB0, xB4, xB8;
    f32 xBC, xC0, xC4, xC8, xCC, xD0, xD4, xD8;
    s32 xDC, xE0;
    f32 xE4, xE8, xEC, xF0, xF4, xF8, xFC, x100, x104, x108;
    s32 x10C, x110;
    u8 pad114[0x11C - 0x114];
    s32 x11C, x120;
    f32 x124, x128, x12C, x130;
    Vec3 x134_translate;
    f32 x140_scale;
} port_grBb_YakumonoParam;

/* grbigblueroute.c (GrNBr.dat, 0x50 bytes). */
typedef struct {
    s32 x0;
    f32 x4;
    u8 pad_8[0x20 - 0x8];
    f32 x20;
    u8 pad_24[0x3C - 0x24];
    f32 x3C, x40, x44, x48, x4C;
} port_grBigBlueRoute_YakumonoParam;

/* grcastle.c (GrCs.dat, 0x148 bytes on the disc; the game declares 0x144).
 * x114 is relocated: grCastle_801D... plays it through grMaterial_801C9604. */
typedef struct {
    s16 x0;
    u8 pad_x2[2];
    f32 x4;
    Vec3 rot;
} port_grCastleParams_Entry;
typedef struct {
    s16 x0, x2, x4, x6, x8, xA, xC, xE;
    f32 x10, x14, x18;
    u8 pad_x1C[4];
    f32 x20, x24, x28, x2C, x30, x34, x38, x3C;
    s16 x40, x42, x44;
    u8 pad_x46[2];
    f32 x48, x4C, x50;
    s16 x54;
    u8 pad_x56[2];
    s16 x58;
    u8 pad_x5A[2];
    port_grCastleParams_Entry entries[9];
    f32 x110;
    port_GrColorScript x114;
    f32 x118, x11C, x120, x124;
    u8 pad_x128[4];
    s16 x12C[4];
    f32 x134, x138, x13C, x140;
} port_grCastle_YakumonoParam;

/* grcorneria.c (GrCn.dat / GrCn.usd, 0x8C bytes). x84 is relocated: a
 * colour-overlay script (grMaterial_801C9604 at two sites). */
typedef struct {
    f32 x0, x4, x8, xC, x10, x14, x18, x1C, x20, x24, x28, x2C, x30, x34, x38, x3C, x40, x44, x48, x4C;
    u8 pad50[0x18];
    f32 x68;
    u8 pad6C[0x4];
    f32 x70;
    s32 x74, x78, x7C, x80;
    port_GrColorScript x84;
    f32 x88;
} port_grCorneria_YakumonoParam;

/* grflatzone.c (GrFz.dat, 0x40 bytes). */
typedef struct {
    s32 unk0, unk4, unk8, unkC, unk10, unk14, unk18, unk1C, unk20;
    f32 unk24, unk28;
    s32 unk2C, unk30, unk34;
    f32 unk38;
    s32 unk3C;
} port_grFlatzone_YakumonoParam;

/* grfourside.c (GrFs.dat, 0x4C bytes). */
typedef struct {
    s32 heli_wait, heli_wait_add, heli_stay_time, crane_wait, crane_wait_add, crane_iron_wait, crane_iron_wait_add;
    f32 crane_iron_up_min, crane_iron_up_max, crane_iron_down_min, crane_iron_down_max, crane_iron_spd,
        crane_iron_stop_acl;
    s32 ufo_wait;
    f32 ufo_cs_offs;
    s32 ufo_stay_time, ufo_stay_time_add;
    u16 ufo_challenge, x46, x48;
} port_grFourside_YakumonoParam;

/* grgarden.c (GrGd.dat, 0x20 bytes). */
typedef struct {
    f32 x0, x4;
    s32 x8, xC, x10, x14;
    f32 x18, x1C;
} port_grGarden_YakumonoParam;

/* grgreatbay.c grGb_StageAttr (GrGb.dat, 0xA4 bytes). */
typedef struct { s16 kind; s16 weight; } port_grGb_ItemEntry;
typedef struct {
    s16 moon_fall_wait_a, moon_fall_wait_b;
    f32 floatfloor_landing_rate, floatfloor_slant_mul, floatfloor_slant_add, floatfloor_slant_limit,
        floatfloor_slant_rate, floatfloor_slant_reb_rate, floatfloor_slide_mul, floatfloor_slide_add,
        floatfloor_slide_limit, floatfloor_slide_rate, floatfloor_slide_reb_rate, floatfloor_down_mul,
        floatfloor_down_add, floatfloor_down_limit, floatfloor_down_up_rate, floatfloor_down_down_rate;
    s16 kame_wait_frame_a, kame_wait_frame_b, kame_rebirth_frame_a, kame_rebirth_frame_b;
    f32 kame_x, kame_y, kame_x_offset_init, kame_x_lr_offset_a, kame_x_lr_offset_b, kame_x_fb_offset_a,
        kame_x_fb_offset_b, kame_scale, kame_ud_scale;
    s16 kame_dir_prob[4];
    f32 kame_item_prob;
    port_grGb_ItemEntry items[10];
} port_grGb_StageAttr;

/* grgreens.c (GrGr.dat, 0x7C bytes). */
typedef struct {
    s32 x0_blockTimerMin, x4_blockTimerMax, x8_blockBombChance, xC, x10, x14, x18, x1C, x20, x24, x28;
    f32 x2C, x30;
    s32 x34_windTimerMin, x38_windTimerMax;
    f32 x3C_windSpeed, x40_left, x44_right, x48_top, x4C_bottom, x50, x54, x58;
    s32 x5C, x60, x64, x68;
    f32 x6C, x70, x74, x78;
} port_grGreens_YakumonoParam;

/* grheal.c grHeal_UnkData (GrHe.dat, 8 bytes). */
typedef struct { s32 x0; s32 x4; } port_grHeal_UnkData;

/* gricemt.c (GrIm.dat, 0x13C bytes on the disc; the game declares 0xD0).
 * field_ixs is read at indices 0..4 (annotations.yml); xB0 and xB4 are
 * relocated too but nothing decompiled reads them, so their length is not
 * known and what they point at stays big-endian. */
typedef struct {
    s16 x0, x2, x4;
    f32 x8, xC, x10, x14, x18, x1C, x20, x24, x28, x2C, x30;
    s16 x34, x36, x38;
    u16 x3A;
    f32 x3C, x40, x44, x48, x4C, x50, x54, x58, x5C, x60, x64, x68, x6C, x70, x74, x78, x7C, x80, x84, x88, x8C,
        x90, x94;
    s16 ft_max_y, x9E;
    f32 x9C, xA0;
    s16 xA4, xA6, xA8;
    s16* field_ixs;
    s16* xB0;
    s16* xB4;
    s16 xB8, pad;
    grZakoGenerator_SpawnDesc xBC;
    f32 xC0, xC4, xC8, xCC;
} port_grIceMt_YakumonoParam;

/* grinishie1.c (GrI1.dat, 0x54 bytes). */
typedef struct {
    f32 unk0, unk4, unk8, unkC, unk10;
    s16 unk14, unk16;
    u16 unk18;
    s16 unk1A, unk1C, unk1E;
    f32 unk20, unk24, unk28;
    Vec3 unk2C[2];
    f32 unk44, unk48, unk4C, unk50;
} port_grInishie1_YakumonoParam;

/* grinishie2.c (GrI2.dat, 0x4C bytes). */
typedef struct {
    s16 unk0, unk2, unk4, unk6, unk8, unkA, unkC, unkE;
    s16 unk10[2];
    Vec3 unk14[2];
    f32 unk2C;
    Vec3 unk30[2];
    s16 unk48, unk4A;
} port_grInishie2_YakumonoParam;

/* grizumi.c (GrIz.dat, 0x54 bytes). */
typedef struct {
    f32 x0;
    s32 x4;
    f32 x8, xC, x10, x14, x18, x1C, x20, x24, x28, x2C, x30, x34, x38, x3C, x40, x44, x48, x4C, x50;
} port_grIzumi_YakumonoParam;

/* grkinokoroute.c (GrNKr.dat, 0x184 bytes on the disc; the game reads 8). */
typedef struct {
    s32 x0;
    grZakoGenerator_SpawnDesc x4;
} port_grKinokoRoute_YakumonoParam;

/* grkongo.static.h (GrKg.dat, 0xBC bytes). unk84 is relocated: a
 * colour-overlay script (grMaterial_801C9604 at two sites). */
typedef struct {
    f32 unk0, unk4, unk8, unkC, unk10, unk14, unk18, unk1C, unk20, unk24, unk28, unk2C, unk30, unk34, unk38, unk3C,
        unk40;
    s16 unk44, unk46, unk48, unk4A, unk4C, unk4E, unk50, unk52;
    f32 unk54, unk58, unk5C, unk60;
    s32 unk64, unk68;
    f32 unk6C, unk70, unk74, unk78, unk7C, unk80;
    port_GrColorScript unk84;
    f32 unk88, unk8C, unk90, unk94, unk98, unk9C, unkA0, unkA4, unkA8, unkAC, unkB0, unkB4, unkB8;
} port_grKongo_YakumonoParam;

/* grkraid.c (GrKr.dat, 0x34 bytes). */
typedef struct {
    u32 map_time_min, map_time_max;
    s32 map_time_acl;
    f32 map_rot_spd_min, map_rot_spd_max;
    u32 kraid_wait_time, kraid_wait_time_add;
    f32 kraid_pos_x[6];
} port_grKraid_YakumonoParam;

/* grlast.c (GrNLa.dat, 0x10 bytes): grLast reads it as int[4], each played
 * through grMaterial_801C9604; all four are relocated. */
typedef struct {
    port_GrColorScript x0, x4, x8, xC;
} port_grLast_YakumonoParam;

/* grmutecity.c grMc_YakumonoParam (GrMc.dat, 0x50 bytes). x0 (declared int)
 * and x4 (declared void*, cast to s32) are both relocated and both go to
 * grMaterial_801C9604; x8 and xC are the stage's bury-device hit descriptors
 * (see port_GrDeviceHit below). */
typedef struct {
    port_GrColorScript x0;
    port_GrColorScript x4;
    port_GrDeviceHit* x8;
    port_GrDeviceHit* xC;
    u8 pad10[0x1C];
    f32 x2C, x30, x34, x38, x3C, x40, x44, x48, x4C;
} port_grMc_YakumonoParam;

/* groldkongo.c (GrOk.dat, 0x70 bytes). x6C is relocated: a colour-overlay
 * script (grMaterial_801C9604). */
typedef struct {
    s16 rframe_bird_wait_a, rframe_bird_wait_b;
    f32 rrange_bird_random_offset_y, rframe_barrel_shoot_a, rframe_barrel_shoot_b, rframe_barrel_in,
        rframe_barrel_wait_a, rframe_barrel_wait_b, rspeed_barrel_rot_accel, rspeed_barrel_rot_max,
        rframe_barrel_roll_a, rframe_barrel_roll_b;
    s16 rrate_barrel_ld, rrate_barrel_l, rrate_barrel_lu, rrate_barrel_u, rrate_barrel_ru, rrate_barrel_r,
        rrate_barrel_rd, rrate_barrel_d;
    s32 rframe_barrel_interval_a, rframe_barrel_interval_b;
    f32 rspeed_barrel_move_accel, rspeed_barrel_move_max;
    s32 rframe_barrel_stop_a, rframe_barrel_stop_b, rpower_barrel_attack, rvector_barrel_attack,
        rreff_barrel_attack, rrfix_barrel_attack, rradd_barrel_attack, x68;
    port_GrColorScript x6C;
} port_grOldKongo_YakumonoParam;

/* groldpupupu.c (GrOp.dat, 0x34 bytes). */
typedef struct {
    s16 x0, x2, x4, x6;
    s32 x8, xC;
    f32 x10, x14, x18, x1C, x20, x24, x28, x2C, x30;
} port_grOldpupupu_YakumonoParam;

/* groldyoshi.c (GrOy.dat, 0x1C bytes; an anonymous struct there). */
typedef struct {
    s16 x0, x2;
    f32 x4, x8, xC;
    s16 x10, x12, x14, x16, x18;
} port_grOldYoshi_YakumonoParam;

/* gronett.c grOnett_StageParam (GrOt.dat / GrOt.usd, 0x68 bytes). */
typedef struct {
    f32 awning_initial, max_velocity, vel_threshold, pos_threshold, damping, spring_force, spring_constant,
        max_displacement, awning_delta, x24, x28, x2C, x30, x34, x38, x3C, x40, x44, x48, x4C, x50, x54, x58, x5C,
        x60, x64;
} port_grOnett_StageParam;

/* grpstadium.c (GrPs.dat / GrPs.usd, 0x54 bytes; an anonymous struct there). */
typedef struct {
    s32 x0, x4, x8, xC, x10, x14, x18;
    u8 r, g, b;
    u32 x20, x24, x28, x2C, x30, x34, x38, x3C, x40, x44;
    s16 x48, x4A, x4C, x4E, x50;
} port_grPStadium_YakumonoParam;

/* grpushon.c (GrNPo.dat, 0x214 bytes). x0 (declared s32) is relocated: the
 * descriptor fn_802192A4 hands ftCo_800C08A0 as a bury device, like x4..x14.
 * x18 is the game's 4-byte bool. */
typedef struct { s32 x0; s16 x4; s16 x6; } port_grPushOn_Entry;
typedef struct { s32 key; s32 value; } port_grPushOn_Lookup;
typedef struct {
    port_GrDeviceHit* x0;
    port_GrDeviceHit* x4;
    port_GrDeviceHit* x8;
    port_GrDeviceHit* xC;
    port_GrDeviceHit* x10;
    port_GrDeviceHit* x14;
    s32 x18;
    port_grPushOn_Entry x1c[0x1E];
    port_grPushOn_Lookup x10c[0x21];
} port_grPushon_YakumonoParam;

/* grrcruise.c (GrRc.dat, 0x48 bytes). */
typedef struct {
    f32 x0, x4, x8;
    s32 xC, x10, x14, x18, x1C, x20, x24, x28;
    f32 x2C, x30, x34, x38;
    s32 x3C, x40, x44;
} port_grRCruise_YakumonoParam;

/* grshrineroute.c (GrNSr.dat, 0x128 bytes on the disc; the game declares
 * 0x2C). x0..x10 are relocated: x0, x4, x8 and xC are colour-overlay scripts
 * (grMaterial_801C9604); nothing decompiled reads x10, so what it points at
 * stays big-endian. */
typedef struct {
    port_GrColorScript x0, x4, x8, xC;
    void* x10;
    f32 x14, x18, x1C, x20;
    s32 x24;
    grZakoGenerator_SpawnDesc spawn_desc;
} port_grShrineRoute_YakumonoParam;

/* grstory.c (GrSt.dat, 0x24 bytes). */
typedef struct {
    f32 timer_min, timer_rand, spawnmany_rarity;
    f32 vpos[6];
} port_grStory_YakumonoParam;

/* Break-the-Targets stages whose yakumono_param is a table of device hit
 * descriptors, one per collision line kind the stage's device callback
 * answers (grtfalco.c grTFalco_80220ACC, grtfox.c, grtganon.c, grtmewtwo.c
 * inlineA0, grtpurin.c). Falco's and Fox's are declared UNK_T; the returning
 * function's type is DynamicsDesc*, and every slot is relocated on the disc. */
typedef struct { port_GrDeviceHit* unk_0; port_GrDeviceHit* unk_4; port_GrDeviceHit* unk_8; port_GrDeviceHit* unk_C; } port_grTFalco_YakumonoParam;
typedef struct { port_GrDeviceHit* unk0; port_GrDeviceHit* unk4; port_GrDeviceHit* unk8; port_GrDeviceHit* unkC; } port_grTFox_YakumonoParam;
typedef struct { port_GrDeviceHit* x0; port_GrDeviceHit* x4; port_GrDeviceHit* x8; } port_grTGn_YakumonoParam;
typedef struct {
    port_GrDeviceHit* x0;
    port_GrDeviceHit* x4;
    port_GrDeviceHit* xC;
    port_GrDeviceHit* x8;
    port_GrDeviceHit* x10;
    port_GrDeviceHit* x14;
    port_GrDeviceHit* x1C;
    port_GrDeviceHit* x18;
} port_grTMewtwo_UnkStruct;
typedef struct { port_GrDeviceHit* x0; } port_grTPrSpecialParams;

/* grvenom.c (GrVe.dat / GrVe.usd, 0x3C bytes). x38 is relocated: a
 * colour-overlay script (grMaterial_801C9604). */
typedef struct {
    f32 x0, x4, x8, xC, x10;
    u8 x14[0x2C - 0x14];
    f32 x2C;
    u8 x30[0x34 - 0x30];
    f32 x34;
    port_GrColorScript x38;
} port_grVenom_YakumonoParam;

/* gryorster.c YorsterParams (GrYt.dat, 0x24 bytes on the disc; the game
 * declares 0x20). */
typedef struct {
    f32 x00, x04, x08, x0C;
    s32 x10, x14, x18, x1C;
} port_YorsterParams;

/* grzebes.c grZe_YakumonoParam (GrZe.dat, 0x190 bytes). The slot at +0x2C,
 * inside the game's pad_14, is the acid's hit descriptor: grZebes_801DCBFC
 * reads it as ((HSD_GObj*) yakumono_param)->user_data (offset 0x2C here too).
 * Undescribed, its damage stayed big-endian: 14 read as 234881024, and the
 * first touch of the acid trapped in the hit code. */
typedef struct {
    s16 x0_base, x2_delay_min, x4_delay_max, x6_level;
} port_grZe_AcidLevelEntry;
typedef struct {
    f32 x00, x04, x08, x0C;
    s32 x10;
    u8 pad_14[0x2C - 0x14];
    port_GrDeviceHit* x2C_acid_hit;
    f32 x30, x34, x38, x3C, x40, x44, x48, x4C, x50, x54, x58, x5C, x60, x64, x68, x6C, x70, x74, x78, x7C, x80,
        x84, x88, x8C, x90, x94, x98, x9C;
    port_grZe_AcidLevelEntry xA0_entries[30];
} port_grZe_YakumonoParam;

/* grzebesroute.c (GrNZr.dat, 8 bytes). */
typedef struct { s32 camera_timer; s32 zako_spawn_chance; } port_grZebesRoute_YakumonoParam;

/* ftcpuattack.c ftCo_AttackEntry (PlCo.dat): the CPU's attack candidates,
 * a list per fighter kind in each of Fighter_804D64FC's seven tables, ended
 * by an entry whose cmd is 0. The struct lives in the .c file. */
typedef struct {
    s32 cmd;
    s32 x04;
    f32 x08, x0C, x10, x14, weight;
    s32 x1C, x20;
} port_ftCo_AttackEntry;
typedef struct { port_ftCo_AttackEntry* list; } port_CpuAttackList;

/* ftdynamics.c: ftDynamics::x10 is indexed by an animation's blend slot
 * (fp->x28[anim][1]), each entry an array of one FigaTree per physics bone
 * (ftCo_8009E4A8 reads tree[i] for i < dynamics_num). Neither length is in
 * the archive, so both run to the next object. */
typedef struct { struct FigaTree* tree; } port_FigaTreeRef;
typedef struct { port_FigaTreeRef* trees; } port_DynFigaSlot;

/* Yoshi's attribute block (ftData ext_attr, 0x138 bytes). The code reads it
 * through two structs: ftYoshiAttributes names 0x00-0xE8 and 0x114-0x128,
 * ftYs_DatAttrs names 0xEC-0x11C (up-B and down-B), and each calls the
 * other's range padding. Every field is a word except the byte table at 0x12C
 * (ftCo_CatchPull.c indexes yattrs->x12C[rate]). */
typedef struct {
    f32 w[0x12C / 4];
    u8 x12C[0x138 - 0x12C];
} port_YoshiAttrs;

/* Game & Watch's chef, judge and rescue items: attribute word 0 points at
 * the bone lists it_8026EECC shows and hides around the flat outline pass
 * (it_266F_ItemVars' first four fields; it_8027CE64 stores the pointer).
 * Left big-endian, a count of 0x0300 walked the item's bone table past its
 * end. */
typedef struct {
    u16 n0;
    u8* bones0;
    u16 n1;
    u8* bones1;
} port_GwItemDraw;
typedef struct {
    port_GwItemDraw* draw;
    f32 rest[1];
} port_GwItemAttrs;

/* PlGw.dat ext_attr (ftGameWatchAttributes): the costume and outline colors
 * are GXColor bytes, the rest words. A plain float run reversed the colors'
 * bytes. */
typedef struct {
    f32 x0;
    GXColor x4_colors[5];
    f32 rest[1];
} port_GwAttrs;

/* PlKbCpXx.dat, the hat Kirby wears after copying XX; ft_80459B88 slot N is
 * FighterKind N (the header's `hats[k]` is slot k + 1). Most files start with
 * the hat's joint and a one-row parts table (KirbyHatStruct). DK, Jigglypuff,
 * Mewtwo, Falco and Game & Watch instead start with a costume-indexed parts
 * block like a fighter's own (ftData x8), a part mask and a joint tree that
 * LOAD_HAT splices into Kirby's model. The pointer slots after that, the
 * header's hat_dynamics[], hold something else for every character: the
 * copied projectile's Articles (converted when ftKb_SpecialN_800F16D0
 * registers them), physics-bone sets, joints, animations. Treating all of them
 * as ftDynamics walked the Articles' attribute blocks as bone arrays with
 * pointer-sized counts. Each file gets the type naming what its code reads. */
typedef struct {
    u32 num;
    BoneDynamicsDesc* bones;
} port_KbHatDyn;
typedef struct {
    HSD_Joint* joint;
    FtPartsDesc desc;
} port_KbHat;
typedef struct { HSD_Joint* joint; FtPartsDesc desc; port_KbHatDyn* s0; } port_KbHatDyn0;
typedef struct { HSD_Joint* joint; FtPartsDesc desc; void* s0; port_KbHatDyn* s1; } port_KbHatDyn1;
typedef struct { HSD_Joint* joint; FtPartsDesc desc; void* s0; void* s1; port_KbHatDyn* s2; } port_KbHatDyn2;
typedef struct { HSD_Joint* joint; FtPartsDesc desc; void* s0; HSD_Joint* s1; } port_KbHatPopo;
typedef struct { HSD_Joint* joint; FtPartsDesc desc; HSD_Joint* s0; port_KbHatDyn* s1; } port_KbHatSword;
typedef struct {
    HSD_Joint* joint;
    FtPartsDesc desc;
    HSD_Joint* s0;
    HSD_AnimJoint* s1;
    HSD_AnimJoint* s2;
    HSD_AnimJoint* s3;
    HSD_AnimJoint* s4;
} port_KbHatYoshi;
/* The color block Kirby reads when he copies Game & Watch (+4 color, +8
 * outline, as bytes). */
typedef struct {
    f32 x0;
    GXColor x4;
    GXColor x8;
} port_KbGwColors;
typedef struct {
    FtPartsDesc desc;
    ftData_x8_x8 anim;
    u32 mask;
    HSD_Joint* joint;
} port_KbHatCostume;
typedef struct { FtPartsDesc desc; ftData_x8_x8 anim; u32 mask; HSD_Joint* joint; port_KbHatDyn* s3; } port_KbHatPurin;
typedef struct { FtPartsDesc desc; ftData_x8_x8 anim; u32 mask; HSD_Joint* joint; void* s3; port_KbHatDyn* s4; } port_KbHatMewtwo;
typedef struct { FtPartsDesc desc; ftData_x8_x8 anim; u32 mask; HSD_Joint* joint; void* s3; port_KbGwColors* s4; } port_KbHatGw;

/* grfigureget.c grFigureGet_Params (GrNFg.dat, 0x18 bytes). */
typedef struct {
    s32 x0, x4, x8;
    f32 xC, x10, x14;
} port_grFigureGet_Params;

#endif
