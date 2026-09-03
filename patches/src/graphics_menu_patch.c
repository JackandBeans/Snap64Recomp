/**
 * A GRAPHICS entry in the game's own Option screen.
 *
 * "Graphics" appears as a sixth item, directly under "Screen", drawn in the
 * game's UI font with the screen's own colours, sounds, pulse animation and
 * help line. Selecting it opens a page in the same dress as the Option list
 * itself: the same island background, the same header and rules, values in
 * the same "< Stereo >" style, the help box explaining the controls. Nothing
 * about the presentation says "port".
 *
 * The Option screen's five items are baked as literals into its functions and
 * its selection visuals index a five-entry label cache, so the sixth item is
 * added by replacing the selection loop and the screen loop and re-creating
 * their two cosmetic coroutines (label pulse, help swap) with six-entry
 * versions. The stock value-cycling code is reused untouched for the three
 * stock toggles by translating the selection index around the call.
 *
 * Text arrives as pixels: the port composites every string into IA16 strips
 * from the game's own font atlas and stages them at 0x80C01000 (see
 * src/menu_assets.cpp). A strip is drawn by wrapping it in a runtime-built
 * Sprite; built at runtime because a patch's .data is never loaded into
 * RDRAM, so every table below lives on the stack and one scratch byte of the
 * staging area carries the selection index between coroutines.
 *
 * Settings travel through the mailbox at 0x80C00000 (its byte map is at
 * SNAP_GFX_MAILBOX below): the port seeds
 * it with saved values, the page edits bytes and bumps a sequence counter,
 * and the port applies on each bump and writes the file once the edits
 * pause (src/settings.h, settings_flush_if_due) -- every change is live
 * while the menu is open. A keeps what is on screen; B puts back the bytes
 * the page opened with and bumps once more, so the header's Cancel is a
 * real cancel, the same way the SOUND page's is.
 */

#include "common.h"

#include "sys/om.h"
#include "PR/sp.h"
#include "PR/libaudio.h"

/* Option screen globals, resolved from the reference symbols. */
extern s8 D_800E8374_A0F904;                 /* selected item index */
extern s8 D_800E8394_A0F924;
extern s8 D_800E8395_A0F925;
extern s8 D_800E8396_A0F926;
extern GObj* D_800E8334_A0F8C4;
extern GObj* D_800E8338_A0F8C8;
extern GObj* D_800E833C_A0F8CC;
extern GObj* D_800E8340_A0F8D0;              /* item labels */
extern GObj* D_800E8344_A0F8D4;              /* help lines */
extern GObj* D_800E8348_A0F8D8;
extern GObj* D_800E834C_A0F8DC;
extern GObj* D_800E8350_A0F8E0;
extern GObj* D_800E8354_A0F8E4;
extern GObj* D_800E8358_A0F8E8;              /* Sound value pair */
extern GObj* D_800E835C_A0F8EC;              /* Z Button value pair */
extern GObj* D_800E8360_A0F8F0;              /* Control Stick value pair */

void func_800E71DC_A0E76C(void);
void func_800E7408_A0E998(void);
s8 func_800E7700_A0EC90(void);
void func_800E6F68_A0E4F8(void);
void func_800E7C40_A0F1D0(void);
void func_800E6C00_A0E190(SObj* sobj, u8 color);
void func_800E6C14_A0E1A4(SObj* sobj, u8 red, u8 green, u8 blue);

/* Title screen (A08E30): background creation, its attribute, tint, show
 * and position helpers, the screen fade, and the two GObj slots the
 * background and the bouncing letters live in. */
extern GObj* D_800E82B0_A0F840;
extern GObj* D_800E82B4_A0F844;
extern GObj* D_800E82BC_A0F84C;
void func_800E18A0_A08E30(SObj* sobj, u32 attr);
void func_800E18AC_A08E3C(SObj* sobj, u8 show);
void func_800E18E0_A08E70(SObj* sobj, u8 red, u8 green, u8 blue);
void func_800E18FC_A08E8C(SObj* sobj, s16 x, s16 y);
void func_800E1930_A08EC0(u8 arg0, u8 red, u8 green, u8 blue, f32 speed);
void ohRemoveSprite(GObj* obj);
void func_800BFB90_5CA30(s32 left, s32 top);
UnkStruct800BEDF8* func_800AA38C(s32);

/* The staged-asset directory and the settings mailbox, shared with
 * src/menu_assets.cpp (seed_mailbox and poll_menu_mailbox own the host
 * side). The mailbox block, byte by byte:
 *
 *   +0x00  u32  magic 'SGFX', written last by the seed
 *   +0x04  u32  GRAPHICS sequence word: the page bumps it, the host applies
 *   +0x08  u8   GRAPHICS fields 0..15, one setting apiece, through +0x17:
 *                0 Render Scale, 1 Anti-Aliasing, 2 Widescreen, 3 Frame
 *                Rate, 4 2D Detail, 5 Filter, 6 Dither, 7 Fullscreen,
 *                8 Super Sampling, 9 Texture Filter, 10 Color Depth,
 *                11 Buffering, 12 Overscan Crop, 13 Cutscene Fix (also
 *                read by the intro patches), 14 Photo Detail, 15 VC
 *                Recolour. The bank is full: field 16 would be the
 *                pointer word below.
 *   +0x18  u32  SCRATCH_GRAPHICS_GOBJ, the patch's own (see below)
 *   +0x1C  u32  SCRATCH_HELP_ITEM, the patch's own
 *   +0x20  u32  SOUND sequence word
 *   +0x28  u8   SOUND fields 0..5, through +0x2D (sfx_volume_patch.c reads
 *               them too)
 *   +0x30  u32  MBOX_DBG, retired
 *   +0x40  u32  the audio backlog word the patched AI_LEN read consumes
 *               (src/overlay_hook.cpp); moved here from 0x80700004 because
 *               the Snap Station boot's memory test sweeps 0x80400000-0x807FFFF0
 *   +0x34  u8   MBOX_SEL, the patch's own: the current selection,
 *               readable from every coroutine
 *   +0x38  u8   MBOX_TITLE_REQ: the title's Snap Station item was chosen;
 *               the host reads it, clears it, attaches the station
 *   +0x50  u32  SCRATCH_TITLE_GOBJ, the patch's own: the title's Snap
 *               Station label, between its creation and its deletion
 *   +0x100      SCRATCH_ARRAYS, the page's pointer and snapshot arrays
 *
 * The host never touches anything the map calls the patch's own. */
#define SNAP_GFX_MAILBOX   0x80C00000
#define SNAP_GFX_ASSETS    0x80C01000

#define MBOX_SEQ     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x4))
#define MBOX_FIELD(i) (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x8 + (i)))
/* Moved off +0x16 when the GRAPHICS bank grew to sixteen fields (+0x08..
 * +0x17): field 14 (Photo Detail) now lives where the selection byte did.
 * Not +0x1F, the first free-looking byte -- that is the low byte of the
 * SCRATCH_HELP_ITEM pointer word at +0x1C, and writing a selection index
 * into it would corrupt the help sprite's GObj pointer. +0x34 is the
 * byte after the retired debug word, in the hole nothing claims before
 * SCRATCH_ARRAYS at +0x100. */
#define MBOX_SEL     (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x34))

#define DIR_MAGIC    (*(volatile u32*) (SNAP_GFX_ASSETS + 0x0))
#define DIR_COUNT    (*(volatile u32*) (SNAP_GFX_ASSETS + 0x4))
#define DIR_ADDR(id) (*(volatile u32*) (SNAP_GFX_ASSETS + 0x8 + (id) * 8))
#define DIR_W(id)    (*(volatile u16*) (SNAP_GFX_ASSETS + 0xC + (id) * 8))
#define DIR_H(id)    (*(volatile u16*) (SNAP_GFX_ASSETS + 0xE + (id) * 8))

/* String ids, matching the port's stager. */
#define STR_ITEM_LABEL 1
#define STR_L_SCALE    2   /* ..STR_L_SCALE+7 are the eight setting labels */
#define STR_ITEM_HELP  10
#define STR_AUTO       11
#define STR_OFF        12
#define STR_ON         13
#define STR_1X         14   /* ..21 = 8x */
#define STR_ORIGINAL   22
#define STR_DISPLAY    23
#define STR_CLASSIC    24
#define STR_SHARP      25
#define STR_POINT      26
#define STR_SMOOTH     27
#define STR_CRISP      28
#define STR_PAGE_HELP  29
#define STR_DESC       30   /* ..STR_DESC+7: two-line setting descriptions */
#define STR_HDR        38   /* "Graphics" in the header face */
#define STR_SS_LABEL   39   /* Super Sampling */
#define STR_TEXF_LABEL 40   /* Texture Filter */
#define STR_DEPTH_LABEL 41  /* Color Depth */
#define STR_BUF_LABEL  42   /* Buffering */
#define STR_AUTHENTIC  43
#define STR_STANDARD   44
#define STR_HIGH       45
#define STR_DOUBLE     46
#define STR_TRIPLE     47
#define STR_DESC2      48   /* ..+3: descriptions for the second-wave rows */
#define STR_LOGO       52   /* "Recomp" wordmark, RGBA16, for the title */
#define STR_CREDITS    53   /* the port's credits line, RGBA16, animated */
#define STR_SCROLL_UP  54   /* the value chevron turned upward */
#define STR_SCROLL_DN  55   /* and downward: more settings that way */
#define STR_SND_HDR    56   /* "Sound" in the header face */
#define STR_SND_LABEL  57   /* ..62: the SOUND page's six labels */
#define STR_VOL0       63   /* ..73: the shared volume steps, 0..100 by 10 */
#define STR_STEREO     74
#define STR_MONO       75
#define STR_SND_DESC   76   /* ..81: the SOUND page's descriptions */
#define STR_CROP_LABEL 82   /* "Overscan Crop", the thirteenth Graphics row */
#define STR_CROP_DESC  83
/* "Cutscene Fix": the intro hand-off fix (settings.h intro_fix). Not
 * "Intro Fix" -- the body face the labels are set in has no capital I. */
#define STR_INTRO_LABEL 84
#define STR_INTRO_DESC  85
#define STR_PHOTO_LABEL 86  /* "Photo Detail" (settings.h photo_detail) */
#define STR_PHOTO_DESC  87
/* "Jynx Recolour": the purple Jynx of the re-releases (settings.h jynx_vc).
 * Not "Jynx Recolour" -- the body face has no capital J either, and the
 * help face the description is set in has no J, no V and no hyphen, so
 * the description says what changes and where, never the name. */
#define STR_JYNX_LABEL  88
#define STR_JYNX_DESC   89
/* "Snap Station" in the title menu's own face, for the title's fifth item
 * (the title section at the end of this file). Width 0 when the port could
 * not compose it, and then there is no fifth item. */
#define STR_TITLE_STATION 90

/* The SOUND bank of the mailbox: its own sequence word and value bytes
 * (percent volumes; stereo and background-mute booleans). The patched
 * audio functions below read the bytes live on every call. */
#define MBOX_MAGIC   (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x0))
#define SND_SEQ      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x20))
#define SND_FIELD(i) (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x28 + (i)))

#define OPT_ITEMS      6    /* Screen, Graphics, Sound, Z, Stick, Return */
#define OPT_GRAPHICS   1
#define OPT_SOUND      2
#define PAGE_ITEMS     16
/* The stock Options list's own rhythm: first row at 73, sixteen rows of
 * pitch, six rows on screen -- the Graphics page reads as the same menu.
 * The rest scroll into view, which the edge arrows announce. */
#define PAGE_VISIBLE   6
#define PAGE_TOP_Y     73
#define PAGE_PITCH     16
/* Scroll arrows: the chevron at its native 1:1 scale (12x13 with the
 * ring), bracketing the list level with the first and last visible
 * rows, in the LEFT gutter where this menu already hangs its row
 * marks (the root page's bullet dots): every label starts at 50, so
 * the arrow keeps a constant four pixels to the text column on every
 * row. The right rail floated instead -- the values end at different
 * widths, so over there the arrow's distance to the nearest content
 * changed row by row and the pair read as unanchored. The down
 * arrow's travel stays above the help box frame at y=168. */
#define ARROW_X        34
#define ARROW_UP_Y     72
#define ARROW_DN_Y     151

#define SEL_R 0xFF
#define SEL_G 0x82
#define SEL_B 0x41

/* A staged text strip wrapped in a runtime-built sprite. Strips are staged
 * as whole 64-texel column blocks, so every bitmap is exactly one block.
 * Text strips are IA16; the wordmark passes G_IM_FMT_RGBA instead. */
static GObj* snap_make_strip_fmt(s32 id, s32 x, s32 y, u8 fmt);

static GObj* snap_make_strip(s32 id, s32 x, s32 y) {
    return snap_make_strip_fmt(id, x, y, G_IM_FMT_IA);
}

/* Builds the Sprite + Bitmap chain for a staged strip without creating a
 * GObj, so a strip can also ride an existing object's sprite chain. */
static Sprite* snap_build_sprite(s32 id, s32 x, s32 y, u8 fmt);

static GObj* snap_make_strip_fmt(s32 id, s32 x, s32 y, u8 fmt) {
    Sprite* sp = snap_build_sprite(id, x, y, fmt);
    if (sp == NULL) {
        return NULL;
    }
    return ohCreateSprite(0xE, NULL, 0, 0x80000000, renDrawSprite, 1, 0x80000000, -1,
                          sp, 0, NULL, 1);
}

static Sprite* snap_build_sprite(s32 id, s32 x, s32 y, u8 fmt) {
    Sprite* sp;
    Bitmap* bm;
    s32 w, h, chunks, i;
    u32 pixels;

    if ((DIR_MAGIC != 0x53474130) || ((u32) id >= DIR_COUNT)) {
        return NULL;
    }
    pixels = DIR_ADDR(id);
    w = DIR_W(id);
    h = DIR_H(id);
    if ((pixels == 0) || (w <= 0) || (h <= 0)) {
        return NULL;
    }

    chunks = w / 64;
    /* Room for four chunks even when the first string is narrower, so a
     * later snap_swap_strip to a wider string has bitmaps to fill. */
    sp = (Sprite*) gtlMalloc(sizeof(Sprite) + ((chunks < 4) ? 4 : chunks) * sizeof(Bitmap), 8);
    if (sp == NULL) {
        return NULL;
    }
    bm = (Bitmap*) (sp + 1);

    sp->x = x;
    sp->y = y;
    sp->width = w;
    sp->height = h;
    sp->scalex = 1.0f;
    sp->scaley = 1.0f;
    sp->expx = 0;
    sp->expy = 0;
    sp->attr = SP_TRANSPARENT;
    sp->zdepth = 0;
    sp->red = 0xFF;
    sp->green = 0xFF;
    sp->blue = 0xFF;
    sp->alpha = 0xFF;
    sp->startTLUT = 0;
    sp->nTLUT = 0;
    sp->LUT = NULL;
    sp->istart = 0;
    sp->istep = 0;
    sp->nbitmaps = chunks;
    sp->ndisplist = 24 + 12 * ((chunks < 4) ? 4 : chunks);
    sp->bmheight = h;
    sp->bmHreal = h;
    sp->bmfmt = fmt;
    sp->bmsiz = G_IM_SIZ_16b;
    sp->bitmap = bm;
    sp->rsp_dl = NULL;
    sp->rsp_dl_next = NULL;
    sp->frac_s = 0;
    sp->frac_t = 0;

    /* Every allocated slot gets valid fields, including the spares a wider
     * swapped-in string will use. */
    for (i = 0; i < ((chunks < 4) ? 4 : chunks); i++) {
        bm[i].width = 64;
        bm[i].width_img = 64;
        bm[i].s = 0;
        bm[i].t = 0;
        bm[i].buf = (void*) (pixels + ((i < chunks) ? i : 0) * 64 * h * 2);
        bm[i].actualHeight = h;
        bm[i].LUToffset = 0;
    }

    return sp;
}

/* Points an existing strip sprite at a different staged string. */
static void snap_swap_strip(GObj* gobj, s32 id) {
    SObj* sobj;
    Sprite* sp;
    Bitmap* bm;
    s32 w, h, chunks, i;
    u32 pixels;

    if ((gobj == NULL) || ((u32) id >= DIR_COUNT)) {
        return;
    }
    sobj = gobj->data.sobj;
    if (sobj == NULL) {
        return;
    }
    pixels = DIR_ADDR(id);
    w = DIR_W(id);
    h = DIR_H(id);
    chunks = w / 64;
    if (chunks > 4) {
        chunks = 4;
    }

    sp = &sobj->sprite;
    sp->width = w;
    sp->height = h;
    sp->bmheight = h;
    sp->bmHreal = h;
    sp->nbitmaps = chunks;
    bm = sp->bitmap;
    for (i = 0; i < chunks; i++) {
        bm[i].buf = (void*) (pixels + i * 64 * h * 2);
        bm[i].actualHeight = h;
    }
}

static void snap_tint(GObj* gobj, u8 r, u8 g, u8 b) {
    if ((gobj != NULL) && (gobj->data.sobj != NULL)) {
        func_800E6C14_A0E1A4(gobj->data.sobj, r, g, b);
    }
}

/* Collects the six item labels in display order: the five stock label
 * sprites plus the staged "Graphics" label the screen loop created (whose
 * GObj is remembered in scratch). Display order: Screen, Graphics, Sound,
 * Z Button, Control Stick, Return. */
#define SCRATCH_GRAPHICS_GOBJ (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x18))
#define SCRATCH_HELP_ITEM     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x1C))
/* +0x20 belongs to SND_SEQ; a scratch slot briefly defined here collided
 * with it and was never used -- any write would have faked a sound-bank
 * sequence bump every tick and spammed apply+save. Left retired. */
/* Diagnostic heartbeat the port prints when it changes. */
/* Moved off +0x28 when the SOUND bank claimed +0x28..0x2D: a debug write
 * there would have silently zeroed the volume sliders. Unused today. */
#define MBOX_DBG              (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x30))

/* Pointer arrays live in the mailbox block's spare space, NOT on the stack.
 * These functions run on a GObj process coroutine, and those threads get a
 * few hundred bytes of stack with a canary at the far end -- ohWait checks
 * it every call and a blown canary is a trip straight to the game's crash
 * screen ("gobjthread stack over"), which on the port is a silent freeze.
 * Measured: the page's original ~600 bytes of local arrays killed it. */
#define SCRATCH_ARRAYS        (SNAP_GFX_MAILBOX + 0x100)
/* Sixteen slots for the page rows, and sixteen rows now use them: the
 * day the page once gained a thirteenth row, twelve-slot arrays silently
 * aliased -- label 12 landed on value 0 and value 12 landed on hidden 0,
 * which corrupted value swaps, leaked strips onto the root list, and left
 * the teardown restoring sprites through a clobbered pointer. A
 * seventeenth row needs wider arrays here, a seventeenth PAGE_ENTRY byte,
 * and a mailbox field past +0x17 -- which is the pointer word at +0x18,
 * so the field bank has to move first (see the byte map above). */
#define PAGE_LABEL(i)  (*(volatile u32*) (SCRATCH_ARRAYS + 0x00 + (i) * 4))   /* GObj*, 16 */
#define PAGE_VALUE(i)  (*(volatile u32*) (SCRATCH_ARRAYS + 0x40 + (i) * 4))   /* GObj*, 16 */
#define PAGE_HIDDEN(i) (*(volatile u32*) (SCRATCH_ARRAYS + 0x80 + (i) * 4))   /* SObj*, 64 */
#define LIST_LABEL(i)  (*(volatile u32*) (SCRATCH_ARRAYS + 0x180 + (i) * 4))  /* SObj*, 8 */
#define LIST_HELP(i)   (*(volatile u32*) (SCRATCH_ARRAYS + 0x1A0 + (i) * 4))  /* SObj*, 8 */
#define PAGE_ARROW_UP  (*(volatile u32*) (SCRATCH_ARRAYS + 0x1C0))            /* GObj* */
#define PAGE_ARROW_DN  (*(volatile u32*) (SCRATCH_ARRAYS + 0x1C4))            /* GObj* */
/* The Graphics page's entry snapshot of the mailbox value bytes, by field
 * index, for B to restore. Sixteen bytes for sixteen fields, one per row
 * array slot; nothing else lives past the arrow slots. */
#define PAGE_ENTRY(i)  (*(volatile u8*)  (SCRATCH_ARRAYS + 0x1C8 + (i)))       /* u8, 16 */

/* The page's sixteen rows, in display order. Each row cycles one mailbox
 * field and shows one label, one value set and one description; the maps
 * below are functions so nothing needs a table in a coroutine frame. */
static s32 snap_row_field(s32 row) {
    switch (row) {
        case 0:  return 0;    /* Render Scale */
        case 1:  return 8;    /* Super Sampling */
        case 2:  return 1;    /* Anti-Aliasing */
        case 3:  return 2;    /* Widescreen */
        case 4:  return 3;    /* Frame Rate */
        case 5:  return 4;    /* 2D Detail */
        case 6:  return 5;    /* Filter */
        case 7:  return 9;    /* Texture Filter */
        case 8:  return 10;   /* Color Depth */
        case 9:  return 11;   /* Buffering */
        case 10: return 6;    /* Dither */
        case 11: return 7;    /* Fullscreen */
        case 12: return 12;   /* Overscan Crop */
        case 13: return 13;   /* Cutscene Fix */
        case 14: return 14;   /* Photo Detail */
        default: return 15;   /* Jynx Recolour */
    }
}

static s32 snap_row_label(s32 row) {
    switch (row) {
        case 0:  return STR_L_SCALE + 0;
        case 1:  return STR_SS_LABEL;
        case 2:  return STR_L_SCALE + 1;
        case 3:  return STR_L_SCALE + 2;
        case 4:  return STR_L_SCALE + 3;
        case 5:  return STR_L_SCALE + 4;
        case 6:  return STR_L_SCALE + 5;
        case 7:  return STR_TEXF_LABEL;
        case 8:  return STR_DEPTH_LABEL;
        case 9:  return STR_BUF_LABEL;
        case 10: return STR_L_SCALE + 6;
        case 11: return STR_L_SCALE + 7;
        case 12: return STR_CROP_LABEL;
        case 13: return STR_INTRO_LABEL;
        case 14: return STR_PHOTO_LABEL;
        default: return STR_JYNX_LABEL;
    }
}

static s32 snap_row_desc(s32 row) {
    switch (row) {
        case 0:  return STR_DESC + 0;
        case 1:  return STR_DESC2 + 0;
        case 2:  return STR_DESC + 1;
        case 3:  return STR_DESC + 2;
        case 4:  return STR_DESC + 3;
        case 5:  return STR_DESC + 4;
        case 6:  return STR_DESC + 5;
        case 7:  return STR_DESC2 + 1;
        case 8:  return STR_DESC2 + 2;
        case 9:  return STR_DESC2 + 3;
        case 10: return STR_DESC + 6;
        case 11: return STR_DESC + 7;
        case 12: return STR_CROP_DESC;
        case 13: return STR_INTRO_DESC;
        case 14: return STR_PHOTO_DESC;
        default: return STR_JYNX_DESC;
    }
}

/* The twelve sprite chains of the Option screen, by index -- a function so
 * no caller needs a 48-byte pointer table in its frame. */
static GObj* snap_chain(s32 i) {
    switch (i) {
        case 0:  return D_800E8334_A0F8C4;
        case 1:  return D_800E8338_A0F8C8;
        case 2:  return D_800E833C_A0F8CC;
        case 3:  return D_800E8340_A0F8D0;
        case 4:  return D_800E8344_A0F8D4;
        case 5:  return D_800E8348_A0F8D8;
        case 6:  return D_800E834C_A0F8DC;
        case 7:  return D_800E8350_A0F8E0;
        case 8:  return D_800E8354_A0F8E4;
        case 9:  return D_800E8358_A0F8E8;
        case 10: return D_800E835C_A0F8EC;
        default: return D_800E8360_A0F8F0;
    }
}

/* How many values a page row cycles through. */
static s32 snap_value_count(s32 row) {
    switch (row) {
        case 0:  return 9;   /* Render Scale: Auto, 1x..8x */
        case 1:  return 8;   /* Super Sampling: Off, 2x..8x. The only lever
                              * against texture aliasing in a game that never
                              * enables the hardware's texture LOD, and the one
                              * every other N64 project leans on for it. */
        case 2:  return 4;   /* Anti-Aliasing: Off, 2x, 4x, 8x */
        case 5:  return 3;   /* 2D Detail */
        case 6:  return 3;   /* Filter */
        case 8:  return 3;   /* Color Depth: Auto, Standard, High */
        default: return 2;   /* the on/off pairs */
    }
}

/* Which staged string a page row shows for a value -- computed, not a
 * table in the coroutine's frame. */
static s32 snap_value_str(s32 row, s32 v) {
    switch (row) {
        case 0: return (v == 0) ? STR_AUTO : (STR_1X + v - 1);
        case 1: return (v == 0) ? STR_OFF : (STR_1X + v);          /* 2x 3x 4x */
        case 2: return (v == 0) ? STR_OFF
                     : (v == 1) ? (STR_1X + 1)
                     : (v == 2) ? (STR_1X + 3) : (STR_1X + 7);
        case 4: return v ? STR_DISPLAY : STR_ORIGINAL;
        case 5: return (v == 0) ? STR_CLASSIC : (v == 1) ? STR_AUTO : STR_SHARP;
        case 6: return (v == 0) ? STR_POINT : (v == 1) ? STR_SMOOTH : STR_CRISP;
        case 7: return (v == 0) ? STR_AUTHENTIC : STR_SMOOTH;
        case 8: return (v == 0) ? STR_AUTO : (v == 1) ? STR_STANDARD : STR_HIGH;
        case 9: return (v == 0) ? STR_DOUBLE : STR_TRIPLE;
        default: return v ? STR_ON : STR_OFF;  /* Widescreen, Dither, Fullscreen,
                                                * Overscan Crop, Cutscene Fix,
                                                * Photo Detail, Jynx Recolour */
    }
}

/* Scrolls the window: rows [top, top+PAGE_VISIBLE) sit at the eight fixed
 * slots, everything else hides. */
static void snap_page_layout(s32 top) {
    s32 i;
    for (i = 0; i < PAGE_ITEMS; i++) {
        GObj* label = (GObj*) PAGE_LABEL(i);
        GObj* value = (GObj*) PAGE_VALUE(i);
        const s32 shown = (i >= top) && (i < top + PAGE_VISIBLE);
        const s16 y = PAGE_TOP_Y + (i - top) * PAGE_PITCH;
        if ((label != NULL) && (label->data.sobj != NULL)) {
            label->data.sobj->sprite.y = y;
            if (shown) {
                label->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                label->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
        if ((value != NULL) && (value->data.sobj != NULL)) {
            value->data.sobj->sprite.y = y;
            if (shown) {
                value->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                value->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
    }
    /* The edge arrows say which way the hidden rows lie. */
    {
        GObj* upArrow = (GObj*) PAGE_ARROW_UP;
        GObj* dnArrow = (GObj*) PAGE_ARROW_DN;
        if ((upArrow != NULL) && (upArrow->data.sobj != NULL)) {
            if (top > 0) {
                upArrow->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                upArrow->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
        if ((dnArrow != NULL) && (dnArrow->data.sobj != NULL)) {
            if (top + PAGE_VISIBLE < PAGE_ITEMS) {
                dnArrow->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                dnArrow->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
    }
}

/* Fills LIST_LABEL() with the six item labels in display order. */
static s32 snap_option_labels(void) {
    SObj* sobj = D_800E8340_A0F8D0->data.sobj;
    s32 n = 0;
    s32 i, o;
    GObj* mine = (GObj*) SCRATCH_GRAPHICS_GOBJ;

    o = 1;   /* slot 0 is filled below; stock labels go after Screen */
    while ((sobj != NULL) && (n < 5)) {
        if (n == 0) {
            LIST_LABEL(0) = (u32) sobj;
        }
        else {
            LIST_LABEL(o + n - 1) = (u32) sobj;
        }
        n++;
        sobj = sobj->next;
    }
    /* Display order: Screen, Graphics, Sound, Z Button, Stick, Return. */
    for (i = n - 1; i >= 1; i--) {
        LIST_LABEL(i + 1) = LIST_LABEL(i);
    }
    LIST_LABEL(OPT_GRAPHICS) =
        ((mine != NULL) && (mine->data.sobj != NULL)) ? (u32) mine->data.sobj : LIST_LABEL(0);
    return n + 1;
}

/* The GRAPHICS page: the Option screen's own dress -- island background,
 * header and rules kept, the item rows hidden, eight settings in the same
 * label-and-<value> style, the help box explaining the controls. */
static void snap_graphics_page(void) {
    UnkStruct800BEDF8* input;
    GObj* hdrStrip;
    GObj* descStrip;
    s32 sel, i, moved, hiddenCount;
    s32 v, top, field;
    u8 pulseState, pulseCounter, bobTick;
    u8 nudgeUp, nudgeDn;

    if (DIR_MAGIC != 0x53474130) {
        return;
    }

    /* Hide the Option list's rows -- and remember exactly which sprites were
     * visible, so leaving cannot reveal anything the screen keeps hidden by
     * design (the stacked help lines, the sub-screen extras). */
    hiddenCount = 0;
    for (i = 0; i < 12; i++) {
        GObj* chain = snap_chain(i);
        SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
        while (sobj != NULL) {
            const s32 y = sobj->sprite.y;
            if ((y >= 56) && (y < 164) && !(sobj->sprite.attr & SP_HIDDEN) &&
                (hiddenCount < 64)) {
                sobj->sprite.attr |= SP_HIDDEN;
                PAGE_HIDDEN(hiddenCount) = (u32) sobj;
                hiddenCount++;
            }
            sobj = sobj->next;
        }
    }
    {
        GObj* mine = (GObj*) SCRATCH_GRAPHICS_GOBJ;
        if ((mine != NULL) && (mine->data.sobj != NULL) &&
            !(mine->data.sobj->sprite.attr & SP_HIDDEN) && (hiddenCount < 64)) {
            mine->data.sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) mine->data.sobj;
            hiddenCount++;
        }
    }

    /* The header hands over: the stock "Options" title (the y=40 sprite of
     * its chain; the OK/Cancel hints at y=41 stay) gives way to "Graphics"
     * in the same face, the way the Screen Setting sub-screen retitles. */
    {
        GObj* chain = snap_chain(2);
        SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
        while (sobj != NULL) {
            if ((sobj->sprite.y == 40) && !(sobj->sprite.attr & SP_HIDDEN) &&
                (hiddenCount < 64)) {
                sobj->sprite.attr |= SP_HIDDEN;
                PAGE_HIDDEN(hiddenCount) = (u32) sobj;
                hiddenCount++;
            }
            sobj = sobj->next;
        }
    }
    /* The stock title sprite draws at (43,40) and its O core sits three
     * columns and two rows into the texture -- screen (46,42). This
     * strip's cores sit at its own (1,1), so it seats at (45,41) for the
     * G to land exactly where the O does. */
    hdrStrip = snap_make_strip(STR_HDR, 45, 41);

    /* The help box shows what the selected setting does; the list's item
     * help hides while the page is open. */
    {
        GObj* itemHelp = (GObj*) SCRATCH_HELP_ITEM;
        if ((itemHelp != NULL) && (itemHelp->data.sobj != NULL)) {
            itemHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
    }
    /* Same seat as the item help: cores at (1,1) inside the strip, so the
     * sprite sits one up-left of the stock text position (50,172). */
    descStrip = snap_make_strip(STR_DESC + 0, 49, 171);

    for (i = 0; i < PAGE_ITEMS; i++) {
        field = snap_row_field(i);
        v = MBOX_FIELD(field);
        if ((v < 0) || (v >= snap_value_count(i))) {
            v = 0;
            MBOX_FIELD(field) = 0;
        }
        PAGE_LABEL(i) = (u32) snap_make_strip(snap_row_label(i), 50, PAGE_TOP_Y);
        PAGE_VALUE(i) = (u32) snap_make_strip(snap_value_str(i, v), 163, PAGE_TOP_Y);
        snap_tint((GObj*) PAGE_VALUE(i), SEL_R, SEL_G, SEL_B);
    }

    /* The header promises A OK and B Cancel, and B keeps the promise the
     * way the SOUND page's does: the values as they stood at entry, put
     * back and re-published on the way out. Every mailbox byte the host
     * reads (sixteen, one per field, whichever row shows it) is
     * snapshotted by field index -- after the range check above, so a
     * Cancel republishes exactly what the page showed, never a byte it
     * refused to display. */
    for (i = 0; i < PAGE_ITEMS; i++) {
        PAGE_ENTRY(i) = MBOX_FIELD(i);
    }

    /* The scroll arrows sit at the list's right edge, doubled in their
     * own texels and bobbing a couple of pixels in the main loop: colour
     * alone at the screen's edge went unnoticed, and motion is the one
     * thing the eye cannot ignore. RGBA like the credits line, wearing
     * the same live rainbow inside the same baked ring -- the port's
     * marks speak one language. White prim so the texel colours pass
     * through. snap_page_layout owns their visibility. */
    PAGE_ARROW_UP = (u32) snap_make_strip_fmt(STR_SCROLL_UP, ARROW_X, ARROW_UP_Y, G_IM_FMT_RGBA);
    PAGE_ARROW_DN = (u32) snap_make_strip_fmt(STR_SCROLL_DN, ARROW_X, ARROW_DN_Y, G_IM_FMT_RGBA);

    sel = 0;
    top = 0;
    snap_page_layout(top);
    pulseState = 0;
    pulseCounter = 0;
    bobTick = 0;
    nudgeUp = 0;
    nudgeDn = 0;

    ohWait(2);

    while (1) {
        input = func_800AA38C(0);
        moved = 0;

        if (gContInputPressedButtons & B_BUTTON) {
            /* Cancel. Every stick edit was published live (the host
             * applied it on the next tick and marked the file dirty), so B
             * restores each field byte from the entry snapshot and bumps
             * the sequence word once more: that bump is what makes the
             * host re-apply the old values and mark them for the debounced
             * write -- it ignores an unchanged sequence. When nothing
             * differs there is nothing to publish and no bump, so backing
             * out of an untouched page costs no apply and no file write. */
            auPlaySoundWithParams(0x43, 0x7FFF, 0x40, 1.0f, 0);
            for (i = 0; i < PAGE_ITEMS; i++) {
                if (MBOX_FIELD(i) != PAGE_ENTRY(i)) {
                    MBOX_FIELD(i) = PAGE_ENTRY(i);
                    moved = 1;
                }
            }
            if (moved) {
                MBOX_SEQ = MBOX_SEQ + 1;
            }
            break;
        }

        if (gContInputPressedButtons & A_BUTTON) {
            /* A accepts what is on screen and leaves, matching the SOUND
             * page. A used to cycle the selected row's value instead --
             * which silently edited AND saved a setting on the button
             * everyone presses to mean "yes": that is how a player's 2D
             * Detail ended up on Classic without them knowing, reported as
             * a pixelation bug. Edits belong to the stick alone. */
            auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
            break;
        }

        if (input->pressedButtons & STICK_SLOW_UP) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel == 0) ? (PAGE_ITEMS - 1) : (sel - 1);
            pulseState = 0;
            if (sel < top) {
                top = sel;
                snap_page_layout(top);
                nudgeUp = 12;
            }
            else if (sel >= top + PAGE_VISIBLE) {
                top = sel - (PAGE_VISIBLE - 1);
                snap_page_layout(top);
                nudgeUp = 12;
            }
            snap_swap_strip(descStrip, snap_row_desc(sel));
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
        }
        else if (input->pressedButtons & STICK_SLOW_DOWN) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel + 1) % PAGE_ITEMS;
            pulseState = 0;
            if (sel < top) {
                top = sel;
                snap_page_layout(top);
                nudgeDn = 12;
            }
            else if (sel >= top + PAGE_VISIBLE) {
                top = sel - (PAGE_VISIBLE - 1);
                snap_page_layout(top);
                nudgeDn = 12;
            }
            snap_swap_strip(descStrip, snap_row_desc(sel));
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
        }
        else if (input->pressedButtons & STICK_SLOW_RIGHT) {
            field = snap_row_field(sel);
            v = MBOX_FIELD(field) + 1;
            if (v >= snap_value_count(sel)) {
                v = 0;
            }
            MBOX_FIELD(field) = v;
            moved = 1;
        }
        else if (input->pressedButtons & STICK_SLOW_LEFT) {
            field = snap_row_field(sel);
            v = MBOX_FIELD(field) - 1;
            if (v < 0) {
                v = snap_value_count(sel) - 1;
            }
            MBOX_FIELD(field) = v;
            moved = 1;
        }

        if (moved) {
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
            snap_swap_strip((GObj*) PAGE_VALUE(sel), snap_value_str(sel, MBOX_FIELD(snap_row_field(sel))));
            MBOX_SEQ = MBOX_SEQ + 1;
        }

        /* The stock label pulse, inline, on the selected row. */
        if (PAGE_LABEL(sel) != 0) {
            SObj* sobj = ((GObj*) PAGE_LABEL(sel))->data.sobj;
            switch (pulseState) {
                case 0:
                    if (sobj->sprite.red >= 0x84) {
                        sobj->sprite.red -= 4;
                        func_800E6C00_A0E190(sobj, sobj->sprite.red);
                    } else {
                        func_800E6C00_A0E190(sobj, 0x80);
                        pulseState = 1;
                    }
                    break;
                case 1:
                    if (sobj->sprite.red < 0xE2) {
                        sobj->sprite.red += 0x1E;
                        func_800E6C00_A0E190(sobj, sobj->sprite.red);
                    } else {
                        pulseCounter = 0;
                        func_800E6C00_A0E190(sobj, 0xFF);
                        pulseState = 2;
                    }
                    break;
                case 2:
                    if (pulseCounter++ > 30) {
                        pulseState = 0;
                    }
                    break;
            }
        }

        /* The arrows breathe on an eased four-phase sway (0-1-2-1, not a
         * hard toggle), each leaning the way it points -- and they hop
         * two pixels further when the list actually scrolls past them,
         * the acknowledgement every era menu owes the hand on the stick.
         * No table: patch .data never loads. */
        bobTick++;
        if (nudgeUp > 0) {
            nudgeUp--;
        }
        if (nudgeDn > 0) {
            nudgeDn--;
        }
        {
            /* A spring, not a shim: the hop starts at four pixels and
             * settles through two and one, so a scroll reads as a kick
             * even when the idle sway was already leaning. */
            const s32 phase = (bobTick >> 3) & 3;
            const s16 sway = (s16) ((phase == 3) ? 1 : phase);
            const s16 hopUp = (s16) ((nudgeUp >= 7) ? 4 : ((nudgeUp >= 3) ? 2 : 1));
            const s16 hopDn = (s16) ((nudgeDn >= 7) ? 4 : ((nudgeDn >= 3) ? 2 : 1));
            const s16 offUp = (nudgeUp > 0) ? hopUp : sway;
            const s16 offDn = (nudgeDn > 0) ? hopDn : sway;
            GObj* upArrow = (GObj*) PAGE_ARROW_UP;
            GObj* dnArrow = (GObj*) PAGE_ARROW_DN;
            if ((upArrow != NULL) && (upArrow->data.sobj != NULL)) {
                upArrow->data.sobj->sprite.y = ARROW_UP_Y - offUp;
            }
            if ((dnArrow != NULL) && (dnArrow->data.sobj != NULL)) {
                dnArrow->data.sobj->sprite.y = ARROW_DN_Y + offDn;
            }
        }
        ohWait(1);
    }

    for (i = 0; i < PAGE_ITEMS; i++) {
        if (PAGE_LABEL(i) != 0) {
            omDeleteGObj((GObj*) PAGE_LABEL(i));
        }
        if (PAGE_VALUE(i) != 0) {
            omDeleteGObj((GObj*) PAGE_VALUE(i));
        }
    }
    if (hdrStrip != NULL) {
        omDeleteGObj(hdrStrip);
    }
    if (descStrip != NULL) {
        omDeleteGObj(descStrip);
    }
    if (PAGE_ARROW_UP != 0) {
        omDeleteGObj((GObj*) PAGE_ARROW_UP);
        PAGE_ARROW_UP = 0;
    }
    if (PAGE_ARROW_DN != 0) {
        omDeleteGObj((GObj*) PAGE_ARROW_DN);
        PAGE_ARROW_DN = 0;
    }

    /* Put the Option list back exactly as it was; the next selection loop
     * re-shows the right item help line. */
    for (i = 0; i < hiddenCount; i++) {
        SObj* sobj = (SObj*) PAGE_HIDDEN(i);
        sobj->sprite.attr &= ~SP_HIDDEN;
    }
    ohWait(1);
}

/* Replaces the Option screen's selection loop: six items, the stock feel.
 * The label pulse and the help-line swap run inline in this loop rather than
 * as processes -- a patch function's address cannot be dispatched by the
 * runtime's tables, only called by name, so handing one to omCreateProcess
 * aborts the program on the process's first tick. */
s8 func_800E7700_A0EC90(void) {
    UNUSED s32 pad;
    UnkStruct800BEDF8* temp_v0_2;
    s32 pressedB;
    SObj* sobj;
    GObj* helpItemObj;
    s32 helpCount;
    s32 i;
    s8 sel;
    s8 shownHelp;
    u8 pulseState;
    u8 pulseCounter;

    snap_option_labels();

    helpCount = 0;
    sobj = D_800E8344_A0F8D4->data.sobj;
    while ((sobj != NULL) && (helpCount < 8)) {
        LIST_HELP(helpCount) = (u32) sobj;
        helpCount++;
        sobj->sprite.attr |= SP_HIDDEN;
        sobj = sobj->next;
    }
    helpItemObj = (GObj*) SCRATCH_HELP_ITEM;
    if ((helpItemObj != NULL) && (helpItemObj->data.sobj != NULL)) {
        helpItemObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }

    pulseState = 0;
    pulseCounter = 0;
    shownHelp = -1;
    ohWait(1);

    while (1) {
        temp_v0_2 = func_800AA38C(0);
        if (gContInputPressedButtons & A_BUTTON) {
            auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
            pressedB = 0;
            break;
        } else if (gContInputPressedButtons & B_BUTTON) {
            auPlaySoundWithParams(0x43, 0x7FFF, 0x40, 1.0f, 0);
            pressedB = 1;
            break;
        } else {
            sel = MBOX_SEL;
            if (temp_v0_2->pressedButtons & STICK_SLOW_UP) {
                auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
                func_800E6C00_A0E190((SObj*) LIST_LABEL(sel), 0xFF);
                sel--;
                if (sel < 0) {
                    sel = OPT_ITEMS - 1;
                }
                MBOX_SEL = sel;
                pulseState = 0;
            } else if (temp_v0_2->pressedButtons & STICK_SLOW_DOWN) {
                auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
                func_800E6C00_A0E190((SObj*) LIST_LABEL(sel), 0xFF);
                sel++;
                sel %= OPT_ITEMS;
                MBOX_SEL = sel;
                pulseState = 0;
            }

            /* The help line follows the selection: the staged strip for
             * Graphics, the stock sprite for everything else. */
            sel = MBOX_SEL;
            if (sel != shownHelp) {
                for (i = 0; i < helpCount; i++) {
                    ((SObj*) LIST_HELP(i))->sprite.attr |= SP_HIDDEN;
                }
                if ((helpItemObj != NULL) && (helpItemObj->data.sobj != NULL)) {
                    helpItemObj->data.sobj->sprite.attr |= SP_HIDDEN;
                }
                if (sel == OPT_GRAPHICS) {
                    if ((helpItemObj != NULL) && (helpItemObj->data.sobj != NULL)) {
                        helpItemObj->data.sobj->sprite.attr &= ~SP_HIDDEN;
                    }
                }
                else {
                    i = (sel < OPT_GRAPHICS) ? sel : (sel - 1);
                    if (i < helpCount) {
                        ((SObj*) LIST_HELP(i))->sprite.attr &= ~SP_HIDDEN;
                    }
                }
                shownHelp = sel;
            }

            /* The stock pulse, verbatim, on the selected label. */
            sobj = (SObj*) LIST_LABEL(MBOX_SEL);
            if (sobj != NULL) {
                switch (pulseState) {
                    case 0:
                        if (sobj->sprite.red >= 0x84) {
                            sobj->sprite.red -= 4;
                            func_800E6C00_A0E190(sobj, sobj->sprite.red);
                        } else {
                            func_800E6C00_A0E190(sobj, 0x80);
                            pulseState = 1;
                        }
                        break;
                    case 1:
                        if (sobj->sprite.red < 0xE2) {
                            sobj->sprite.red += 0x1E;
                            func_800E6C00_A0E190(sobj, sobj->sprite.red);
                        } else {
                            pulseCounter = 0;
                            func_800E6C00_A0E190(sobj, 0xFF);
                            pulseState = 2;
                        }
                        break;
                    case 2:
                        if (pulseCounter++ > 30) {
                            pulseState = 0;
                        }
                        break;
                }
            }

            ohWait(1);
        }
    }

    /* The stock screen hides the help line the moment a choice is made. */
    for (i = 0; i < helpCount; i++) {
        ((SObj*) LIST_HELP(i))->sprite.attr |= SP_HIDDEN;
    }
    if ((helpItemObj != NULL) && (helpItemObj->data.sobj != NULL)) {
        helpItemObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    func_800E6C00_A0E190((SObj*) LIST_LABEL(MBOX_SEL), 0xFF);
    ohWait(1);
    func_800E6C14_A0E1A4((SObj*) LIST_LABEL(MBOX_SEL), 0xFF, 0x82, 0x41);
    ohWait(1);
    if (!pressedB) {
        return MBOX_SEL;
    }
    return OPT_ITEMS - 1;   /* B backs out through Return */
}

/* ---------------------------------------------------------------------------
 * The SOUND page's live volume plumbing.
 *
 * Music has one choke point: every BGM volume write in the game -- direct
 * sets and the audio thread's smooth fades alike -- flows through
 * alCSPSetVol, and the replacement below scales there. Sound effects have
 * none. A voice's volume is a slot in auSoundVolume[]: the audio thread
 * reads it once when it starts the voice, the global-volume fades re-read
 * it, and four functions write it -- the three play functions here, which
 * fill the slot as a sound starts, and auSetSoundVolume, which rewrites it
 * while the sound plays (every positional sound, every tick, from
 * EnvSound_Update, plus each course's own ambience ramps). The effects
 * scale is applied wherever the slot is written, so the slot always holds
 * the scaled value and every reader sees the scale exactly once; the
 * auSetSoundVolume replacement lives in sfx_volume_patch.c with a copy of
 * the two helpers below. Every percentage is read live from the mailbox's
 * SOUND bank, so a slider change is heard on the very next note or call.
 * The game's BGM bookkeeping (auBGMVolume) and the global sound volume the
 * level-end fades drive stay untouched and unscaled.
 */

/* Percent from the SOUND bank; full volume until the port has staged. */
static s32 snap_snd_pct(s32 i) {
    if (MBOX_MAGIC != 0x53474658) {
        return 100;
    }
    return SND_FIELD(i);
}

/* The shutter is its own slider on top of the effects slider: the two
 * take-photo sounds, and nothing else. Mirrored verbatim in
 * sfx_volume_patch.c (a shared helper would land in the patch section and
 * be refused); a change here is a change there. */
static u16 snap_scaled_sfx(u32 soundID, s32 vol) {
    s32 pct = snap_snd_pct(2);
    if ((soundID == 0) || (soundID == 16)) {   /* SOUND_ID_TAKE_PHOTO(_2) */
        pct = (pct * snap_snd_pct(3)) / 100;
    }
    return (u16) ((vol * pct) / 100);
}

/* auCurrentSettings is file-local to the game's audio.c; the one field the
 * play functions need sits fourteen bytes in. */
#define AU_NUM_SOUNDS (*(volatile u8*) (0x80096930 + 0x0E))

extern u8 auSoundPriorities[400];
extern u8* auSoundIdleCounter;
extern s32* auPlayingSound;
extern s32* auStartingSound;
void auSetSoundQuality(s32 quality);
extern u8* auSoundPriority;
extern f32* auSoundPitch;
extern u16* auSoundVolume;
extern u8* auSoundPan;
extern u8* auSoundReverbAmt;
extern ALInstrument* auSFXPlayer;
extern ALCSPlayer* auBGMPlayers[2];
extern f32 auBGMVolume[2];
s32 auStealSound(u8 priority);

/* Replaces the sequence players' volume setter: the body the library
 * shipped, with the music percentage applied to the argument. The stored
 * game state upstream keeps the unscaled value, so ramps and restores
 * interpolate the numbers the game believes in. */
void alCSPSetVol(ALCSPlayer* seqp, s16 vol) {
    ALEvent evt;

    evt.type = AL_SEQP_VOL_EVT;
    evt.msg.spvol.vol = (s16) (((s32) vol * snap_snd_pct(1)) / 100);

    alEvtqPostEvent(&seqp->evtq, &evt, 0);
}

/* Three of the four writers of the volume slot, each the stock body with
 * the slot scaled as it is filled. The audio thread reads the slot next
 * tick, so the scaled value is what the voice starts at. The fourth,
 * auSetSoundVolume, is in sfx_volume_patch.c. */
s32 auPlaySound(u32 soundID) {
    s32 i;

    for (i = 0; i < AU_NUM_SOUNDS; i++) {
        if (auSoundIdleCounter[i] < 255) {
            auSoundIdleCounter[i]++;
        }
    }

    if (soundID < auSFXPlayer->soundCount) {
        i = auStealSound(auSoundPriorities[soundID]);
        if (i >= 0) {
            OSIntMask mask = osSetIntMask(OS_IM_NONE);
            auPlayingSound[i] = soundID;
            auStartingSound[i] = soundID;
            auSoundIdleCounter[i] = 0;
            auSoundPriority[i] = auSoundPriorities[soundID];
            auSoundPitch[i] = 1.0f;
            auSoundVolume[i] = snap_scaled_sfx(soundID, 0x7FFF);
            auSoundPan[i] = 64;
            auSoundReverbAmt[i] = 0;
            osSetIntMask(mask);
        }
        return i;
    } else {
        return -1;
    }
}

s32 auPlaySoundWithParams(u32 soundID, s32 volume, s32 pan, f32 pitch, s32 reverbAmt) {
    s32 i;
    OSIntMask mask = osSetIntMask(OS_IM_NONE);

    i = auPlaySound(soundID);

    if (i >= 0) {
        auSoundPitch[i] = pitch;
        auSoundVolume[i] = snap_scaled_sfx(soundID, volume);
        auSoundPan[i] = pan;
        auSoundReverbAmt[i] = reverbAmt;
        osSetIntMask(mask);
        return i;
    } else {
        osSetIntMask(mask);
        return -1;
    }
}

s32 auPlaySoundWithVolume(u32 soundID, s32 vol) {
    s32 i;

    for (i = 0; i < AU_NUM_SOUNDS; i++) {
        if (auSoundIdleCounter[i] < 255) {
            auSoundIdleCounter[i]++;
        }
    }

    if (soundID < auSFXPlayer->soundCount) {
        i = auStealSound(auSoundPriorities[soundID]);
        if (i >= 0) {
            OSIntMask mask = osSetIntMask(OS_IM_NONE);
            auPlayingSound[i] = soundID;
            auStartingSound[i] = soundID;
            auSoundIdleCounter[i] = 0;
            auSoundPriority[i] = auSoundPriorities[soundID];
            auSoundPitch[i] = 1.0f;
            auSoundVolume[i] = snap_scaled_sfx(soundID, vol);
            auSoundPan[i] = 64;
            auSoundReverbAmt[i] = 0;
            osSetIntMask(mask);
        }
        return i;
    } else {
        return -1;
    }
}

/* Re-applies the music scale to whatever the game believes both BGM
 * players are set to -- called when the slider moves, so the change is
 * heard without waiting for the game's next volume write. */
static void snap_apply_music_volume(void) {
    s32 i;
    for (i = 0; i < 2; i++) {
        if (auBGMPlayers[i] != NULL) {
            alCSPSetVol(auBGMPlayers[i], (s16) auBGMVolume[i]);
        }
    }
}

/* The SOUND page: six rows in the Graphics page's dress, no scrolling --
 * the list fits whole. Values live in the mailbox's SOUND bank; the
 * patched audio functions above read them on every call. */
static s32 snap_snd_value_count(s32 row) {
    return (row < 4) ? 11 : 2;
}

static s32 snap_snd_value_str(s32 row, s32 v) {
    if (row < 4) {
        return STR_VOL0 + v;
    }
    if (row == 4) {
        return v ? STR_STEREO : STR_MONO;
    }
    return v ? STR_ON : STR_OFF;
}

static void snap_sound_page(void) {
    UnkStruct800BEDF8* input;
    GObj* hdrStrip;
    GObj* descStrip;
    s32 sel, i, moved, hiddenCount;
    s32 v;
    u8 pulseState, pulseCounter;
    u8 entryFields[6];

    if (DIR_MAGIC != 0x53474130) {
        return;
    }

    /* The header promises A OK and B Cancel, and here B keeps the
     * promise: the values as they stood at entry, restored and re-applied
     * on the way out. The stock Sound row cancelled the same way. */
    for (i = 0; i < 6; i++) {
        entryFields[i] = SND_FIELD(i);
    }

    /* Hide the Option list's rows, remembering exactly what was visible --
     * the same discipline the Graphics page keeps. */
    hiddenCount = 0;
    for (i = 0; i < 12; i++) {
        GObj* chain = snap_chain(i);
        SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
        while (sobj != NULL) {
            const s32 y = sobj->sprite.y;
            if ((y >= 56) && (y < 164) && !(sobj->sprite.attr & SP_HIDDEN) &&
                (hiddenCount < 64)) {
                sobj->sprite.attr |= SP_HIDDEN;
                PAGE_HIDDEN(hiddenCount) = (u32) sobj;
                hiddenCount++;
            }
            sobj = sobj->next;
        }
    }
    {
        GObj* mine = (GObj*) SCRATCH_GRAPHICS_GOBJ;
        if ((mine != NULL) && (mine->data.sobj != NULL) &&
            !(mine->data.sobj->sprite.attr & SP_HIDDEN) && (hiddenCount < 64)) {
            mine->data.sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) mine->data.sobj;
            hiddenCount++;
        }
    }
    {
        GObj* chain = snap_chain(2);
        SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
        while (sobj != NULL) {
            if ((sobj->sprite.y == 40) && !(sobj->sprite.attr & SP_HIDDEN) &&
                (hiddenCount < 64)) {
                sobj->sprite.attr |= SP_HIDDEN;
                PAGE_HIDDEN(hiddenCount) = (u32) sobj;
                hiddenCount++;
            }
            sobj = sobj->next;
        }
    }
    hdrStrip = snap_make_strip(STR_SND_HDR, 45, 41);
    {
        GObj* itemHelp = (GObj*) SCRATCH_HELP_ITEM;
        if ((itemHelp != NULL) && (itemHelp->data.sobj != NULL)) {
            itemHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
    }
    descStrip = snap_make_strip(STR_SND_DESC + 0, 49, 171);

    /* Six fixed rows on the stock seats; PAGE_LABEL/PAGE_VALUE scratch is
     * free while the Graphics page is closed. */
    for (i = 0; i < 6; i++) {
        v = SND_FIELD(i);
        if (i < 4) {
            v = v / 10;
            if (v > 10) {
                v = 10;
                SND_FIELD(i) = 100;
            }
        }
        else if (v > 1) {
            v = 1;
            SND_FIELD(i) = 1;
        }
        PAGE_LABEL(i) = (u32) snap_make_strip(STR_SND_LABEL + i, 50, PAGE_TOP_Y + i * PAGE_PITCH);
        PAGE_VALUE(i) = (u32) snap_make_strip(snap_snd_value_str(i, v), 163, PAGE_TOP_Y + i * PAGE_PITCH);
        snap_tint((GObj*) PAGE_VALUE(i), SEL_R, SEL_G, SEL_B);
    }

    sel = 0;
    pulseState = 0;
    pulseCounter = 0;

    ohWait(2);

    while (1) {
        input = func_800AA38C(0);
        moved = 0;

        if (gContInputPressedButtons & B_BUTTON) {
            auPlaySoundWithParams(0x43, 0x7FFF, 0x40, 1.0f, 0);
            for (i = 0; i < 6; i++) {
                SND_FIELD(i) = entryFields[i];
            }
            SND_SEQ = SND_SEQ + 1;
            snap_apply_music_volume();
            auSetSoundQuality(entryFields[4]);
            D_800E8394_A0F924 = entryFields[4] ? 0 : 1;
            break;
        }

        if (gContInputPressedButtons & A_BUTTON) {
            /* A accepts what is on screen and leaves, as the header says. */
            auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
            break;
        }

        if (input->pressedButtons & STICK_SLOW_UP) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel == 0) ? 5 : (sel - 1);
            pulseState = 0;
            snap_swap_strip(descStrip, STR_SND_DESC + sel);
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
        }
        else if (input->pressedButtons & STICK_SLOW_DOWN) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel + 1) % 6;
            pulseState = 0;
            snap_swap_strip(descStrip, STR_SND_DESC + sel);
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
        }
        else if (input->pressedButtons & STICK_SLOW_RIGHT) {
            v = (sel < 4) ? (SND_FIELD(sel) / 10) : SND_FIELD(sel);
            v = (v + 1) % snap_snd_value_count(sel);
            SND_FIELD(sel) = (sel < 4) ? (v * 10) : v;
            moved = 1;
        }
        else if (input->pressedButtons & STICK_SLOW_LEFT) {
            v = (sel < 4) ? (SND_FIELD(sel) / 10) : SND_FIELD(sel);
            v = (v == 0) ? (snap_snd_value_count(sel) - 1) : (v - 1);
            SND_FIELD(sel) = (sel < 4) ? (v * 10) : v;
            moved = 1;
        }

        if (moved) {
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
            snap_swap_strip((GObj*) PAGE_VALUE(sel), snap_snd_value_str(sel, v));
            SND_SEQ = SND_SEQ + 1;
            if (sel == 1) {
                /* Music applies to what is already playing. */
                snap_apply_music_volume();
            }
            else if (sel == 4) {
                /* The game's own Stereo/Mono flag, live, plus the save
                 * snapshot the screen's exit path writes into PFID_9
                 * (zero there means Stereo). */
                auSetSoundQuality(v);
                D_800E8394_A0F924 = v ? 0 : 1;
            }
        }

        /* The stock label pulse, on the selected row. */
        if (PAGE_LABEL(sel) != 0) {
            SObj* sobj = ((GObj*) PAGE_LABEL(sel))->data.sobj;
            switch (pulseState) {
                case 0:
                    if (sobj->sprite.red >= 0x84) {
                        sobj->sprite.red -= 4;
                        func_800E6C00_A0E190(sobj, sobj->sprite.red);
                    } else {
                        func_800E6C00_A0E190(sobj, 0x80);
                        pulseState = 1;
                    }
                    break;
                case 1:
                    if (sobj->sprite.red < 0xE2) {
                        sobj->sprite.red += 0x1E;
                        func_800E6C00_A0E190(sobj, sobj->sprite.red);
                    } else {
                        pulseCounter = 0;
                        func_800E6C00_A0E190(sobj, 0xFF);
                        pulseState = 2;
                    }
                    break;
                case 2:
                    if (pulseCounter++ > 30) {
                        pulseState = 0;
                    }
                    break;
            }
        }
        ohWait(1);
    }

    for (i = 0; i < 6; i++) {
        if (PAGE_LABEL(i) != 0) {
            omDeleteGObj((GObj*) PAGE_LABEL(i));
            PAGE_LABEL(i) = 0;
        }
        if (PAGE_VALUE(i) != 0) {
            omDeleteGObj((GObj*) PAGE_VALUE(i));
            PAGE_VALUE(i) = 0;
        }
    }
    if (hdrStrip != NULL) {
        omDeleteGObj(hdrStrip);
    }
    if (descStrip != NULL) {
        omDeleteGObj(descStrip);
    }

    for (i = 0; i < hiddenCount; i++) {
        SObj* sobj = (SObj*) PAGE_HIDDEN(i);
        sobj->sprite.attr &= ~SP_HIDDEN;
    }
    ohWait(1);
}

/* Replaces the Option screen loop: makes room for the sixth item, creates
 * its label and help line, and dispatches -- translating the three stock
 * toggles back to the indices their code was compiled against. */
void func_800E7F98_A0F528(void) {
    s32 cond;
    u32 sel;
    s32 i;
    GObj* graphicsLabel;
    GObj* itemHelp;

    func_800E71DC_A0E76C();

    graphicsLabel = NULL;
    itemHelp = NULL;
    SCRATCH_GRAPHICS_GOBJ = 0;
    SCRATCH_HELP_ITEM = 0;
    MBOX_SEL = 0;

    if (DIR_MAGIC == 0x53474130) {
        /* Every stock row below "Screen" slides down one slot to make room:
         * item labels, their bullet dots, the colons and the value pairs all
         * sit between y=85 and y=160, and nothing else does. */
        for (i = 0; i < 12; i++) {
            GObj* chain = snap_chain(i);
            SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
            while (sobj != NULL) {
                if ((sobj->sprite.y >= 85) && (sobj->sprite.y < 160)) {
                    sobj->sprite.y += 16;
                }
                sobj = sobj->next;
            }
        }

        /* The Sound row opens a page now, like Screen and Graphics do, so
         * its inline Stereo/Mono value pair retires from the root list --
         * and so does its colon, which lives on a different chain (the
         * screen chrome's three colons sit at x=158, one per value row;
         * Sound's is the one the shift just moved to y=107). Hidden, not
         * deleted: the screen's teardown owns the chains. */
        if ((D_800E8358_A0F8E8 != NULL) && (D_800E8358_A0F8E8->data.sobj != NULL)) {
            SObj* pair = D_800E8358_A0F8E8->data.sobj;
            pair->sprite.attr |= SP_HIDDEN;
            if (pair->next != NULL) {
                pair->next->sprite.attr |= SP_HIDDEN;
            }
        }
        for (i = 0; i < 12; i++) {
            GObj* chain = snap_chain(i);
            SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
            while (sobj != NULL) {
                if ((sobj->sprite.x == 158) && (sobj->sprite.y == 107)) {
                    sobj->sprite.attr |= SP_HIDDEN;
                }
                sobj = sobj->next;
            }
        }

        /* At the stock label column and row cadence: the strip now carries
         * the items' bullet dot at its start, like every other row. White,
         * like every stock label shows when unselected. */
        graphicsLabel = snap_make_strip(STR_ITEM_LABEL, 43, 89);
        /* At the stock help sprites' own text inset: their sheets bake
         * nine columns and four rows of padding before the ink, so the
         * stock cores sit at (50,172). This strip's cores sit at its own
         * (1,1) -- one fringe column and one faint row in -- so the sprite
         * seats at (49,171) for the cores to land on the same pixels. A
         * flip between the stock sentence and this one must not move. */
        itemHelp = snap_make_strip(STR_ITEM_HELP, 49, 171);
        if (itemHelp != NULL) {
            itemHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
        SCRATCH_GRAPHICS_GOBJ = (u32) graphicsLabel;
        SCRATCH_HELP_ITEM = (u32) itemHelp;
    }

    func_800E7408_A0E998();
    auSetBGMVolume(0, 0x7F00);
    auPlaySong(0, 0x1B);

    do {
        cond = 0;
        sel = func_800E7700_A0EC90();
        if (DIR_MAGIC != 0x53474130) {
            /* No staged assets: the screen behaves exactly as shipped. */
            switch (sel) {
                case 0:
                    func_800E6F68_A0E4F8();
                    break;
                case 1:
                case 2:
                case 3:
                    D_800E8374_A0F904 = sel;
                    func_800E7C40_A0F1D0();
                    break;
                default:
                    cond = 1;
                    break;
            }
        }
        else {
            switch (sel) {
                case 0:
                    func_800E6F68_A0E4F8();
                    break;
                case OPT_GRAPHICS:
                    snap_graphics_page();
                    break;
                case OPT_SOUND:
                    snap_sound_page();
                    break;
                case 3:
                case 4:
                    /* The stock cycling code indexes its sprites by the
                     * five-item numbering it was compiled with. */
                    D_800E8374_A0F904 = sel - 1;
                    func_800E7C40_A0F1D0();
                    D_800E8374_A0F904 = MBOX_SEL;
                    break;
                default:
                    func_800BFB90_5CA30(viEdgeOffsetLeft, viEdgeOffsetTop);
                    setPlayerFlag(PFID_9, D_800E8394_A0F924);
                    setPlayerFlag(PFID_ZOOM_SWITCH, D_800E8395_A0F925);
                    setPlayerFlag(PFID_INVERTED_Y, D_800E8396_A0F926);
                    cond = 1;
                    break;
            }
        }
    } while (!cond);

    auSetBGMVolumeSmooth(0, 0, 30);
    ohWait(30);
    auStopBGM();

    /* Deleted only after the fade above: the stock rows stay on screen
     * through it, and a sixth row that vanishes early is a visible pop. */
    if (graphicsLabel != NULL) {
        omDeleteGObj(graphicsLabel);
    }
    if (itemHelp != NULL) {
        omDeleteGObj(itemHelp);
    }
    SCRATCH_GRAPHICS_GOBJ = 0;
    SCRATCH_HELP_ITEM = 0;
}

/* Replaces the title screen's background creation: everything the original
 * did, plus the port's "Recomp" wordmark under the Snap logo -- staged by
 * the port as RGBA16 from menu_text/recomp_logo.png, drawn only when an
 * image was actually provided, and dimmed exactly as the background is
 * when the title sits behind a menu. */
void func_800E1D44_A092D4(u8 arg0) {
    SObj* sobj;
    Sprite* badge;

    D_800E82B0_A0F840 = ohCreateSprite(0xE, ohUpdateDefault, 0, 0x80000000, renDrawSprite, 1, 0x80000000, -1,
                                       (Sprite*) 0x802DABC0, 0, NULL, 1);
    sobj = D_800E82B0_A0F840->data.sobj;
    func_800E18A0_A08E30(sobj, SP_TEXSHUF | SP_SCALE | SP_TRANSPARENT);
    if (arg0 == 0xD) {
        func_800E18E0_A08E70(sobj, 0xFF, 0xFF, 0xFF);
    } else if (arg0 == 0xC) {
        func_800E18E0_A08E70(sobj, 0x80, 0x80, 0x80);
    }

    /* The badge rides the background's own sprite chain, so it is drawn
     * with the title and torn down with the title, wherever the title
     * goes. Dimmed with the background when the title sits behind a menu. */
    badge = snap_build_sprite(STR_LOGO, 197, 107, G_IM_FMT_RGBA);
    if (badge != NULL) {
        /* Born hidden: the title fades in before the logo exists, and the
         * badge must never be on screen ahead of the mark it belongs to.
         * It is revealed with the Snap flash on the animated intro, or
         * with the static logo's creation everywhere else. Never dimmed:
         * the title behind a menu is rebuilt dim and then only the
         * background sprite itself is re-brightened on return, so a dim
         * tint here would stick to the badge for good. */
        badge->attr |= SP_HIDDEN;
        omGObjAddSprite(D_800E82B0_A0F840, badge);
    }
}

/* Reveals the badge riding the title background, if one is there. */
static void snap_show_badge(void) {
    if ((D_800E82B0_A0F840 != NULL) && (D_800E82B0_A0F840->data.sobj != NULL)) {
        SObj* badgeSobj = D_800E82B0_A0F840->data.sobj->next;
        if (badgeSobj != NULL) {
            badgeSobj->sprite.attr &= ~SP_HIDDEN;
        }
    }
}

/* Replaces the static title's logo creation: exactly as shipped, plus the
 * badge appearing in the same moment the Snap logo does. */
void func_800E2058_A095E8(void) {
    GObj* gobj;
    SObj* sobj;

    gobj = D_800E82BC_A0F84C = ohCreateSprite(0xE, ohUpdateDefault, 0, 0x80000000, renDrawSprite, 1, 0x80000000, -1,
                                              (Sprite*) 0x802F20F0, 0, NULL, 1);
    sobj = gobj->data.sobj;

    func_800E18FC_A08E8C(sobj, 35, 35);
    func_800E18A0_A08E30(sobj, SP_TEXSHUF | SP_TRANSPARENT);
    omGObjAddSprite(gobj, (Sprite*) 0x802F82C8);

    sobj = sobj->next;
    /* The stock seat, exactly: Nintendo's copyright block draws where the
     * ROM draws it (src/main_menu/A08E30.c, func_800E2058_A095E8). */
    func_800E18FC_A08E8C(sobj, 74, 198);
    func_800E18A0_A08E30(sobj, SP_TEXSHUF | SP_TRANSPARENT);

    snap_show_badge();

    /* The port's own credits line, centred, in the rows under the block
     * that nothing on the title uses: no sprite of this screen seats below
     * 198, and the main menu's icons stop at 174. The block's second line
     * ends its coloured cores at 216 (one stray comma pixel at 217) and its
     * black ring at 218; this strip's cores sit one row inside its ring, so
     * at 219 they start at 220 with two clear rows between the two texts.
     * Its letters' cores end at 226 and their ring at 227 -- the last row
     * the overscan crop leaves visible when a player turns it on -- and
     * only the p descender's ring, at 228, falls under that crop. The
     * block's own three-row rhythm would seat this at 220 and lose every
     * letter's bottom ring to the crop instead. The port recolours its
     * texels live, so all the sprite carries is position. Rides this gobj
     * and leaves with it. */
    {
        Sprite* credits = snap_build_sprite(STR_CREDITS, 160 - (DIR_W(STR_CREDITS) / 2), 219, G_IM_FMT_RGBA);
        if (credits != NULL) {
            omGObjAddSprite(gobj, credits);
        }
    }
}

/* The title's letter bounce is deliberately NOT replaced: the original
 * recompiled func_800E28CC_A09E5C runs untouched, so the intro is the
 * factory sequence by construction. The badge needs nothing from it --
 * the intro's own objects are deleted at its end, and the badge the
 * player sees arrives with the static title build above, revealed under
 * the same flash that brings the logo in. */


/* =========================================================================
 * The title screen's fifth item: Snap Station
 *
 * The title menu is built by func_800E33C8_A0A958 from four whole-word
 * sprites ("New Game", "Continue", "Gallery", "Options") into the cursor
 * loop's four-slot buffer, positioned from a 3x4 table of y values that
 * re-centres the block for two, three or four items. There is no fifth slot
 * anywhere: the buffer, the table and the dispatch (func_800E37E8_A0AD78)
 * all end at four. The functions below are the stock ones with one more
 * item, "Snap Station", shown whenever "Gallery" is (the saved report holds
 * more than three species: the kiosk needed photos too). Its label is a
 * strip the port composes from the title face's own letters (menu_harvest.cpp
 * cuts them out of the word sprites; STR_TITLE_STATION) and it rides the
 * stock labels' lifecycle: created where they are (func_800E281C_A09DAC),
 * positioned, shown and tinted by the builder, pulsed by the stock cursor
 * process, hidden by B with the rest, deleted where they are
 * (func_800E1B78_A09108). Its GObj lives in a scratch word of the mailbox,
 * since a patch has no data of its own.
 *
 * Choosing it sets MBOX_TITLE_REQ for the host, which makes port 4 carry
 * the station for the rest of the run (src/snap_station.cpp), and then goes
 * where Gallery goes: the same sound, the same fade, the same scene. The
 * Gallery's own watcher finds the printer and shows Print; everything from
 * there is the game's and the station's. When the label is absent (a strip
 * of width 0: the title face did not harvest), the menu is the stock four.
 * ========================================================================= */
#define MBOX_TITLE_REQ     (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0x38))
#define SCRATCH_TITLE_GOBJ (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x50))

/* Five rows at the stock four-row pitch of 18, begun one row above the
 * four-row block's 120: ink from 105 to 189, below the logo (whose lowest
 * stroke ends near 100) and above the copyright block (from 195). The
 * label strip is 128 wide with its text centred, so its x centres it on
 * the column the stock words share. */
#define TITLE_ROW_Y(i)   (104 + (i) * 18)
#define TITLE_STATION_X  96

extern s8 D_800E80D0_A0F660;     /* the Gallery is unlocked */
extern s8 D_800E82ED_A0F87D;     /* a save exists */
extern u8 D_800E82E4_A0F874;     /* the title's state */
extern u32 D_800E82E8_A0F878;    /* idle frames */
extern s8 D_800BF051;
extern GObj* D_800E82C0_A0F850;
extern GObj* D_800E82C4_A0F854;
extern GObj* D_800E82C8_A0F858;
extern GObj* D_800E82CC_A0F85C;  /* New Game */
extern GObj* D_800E82D0_A0F860;  /* Continue */
extern GObj* D_800E82D4_A0F864;  /* Gallery */
extern GObj* D_800E82D8_A0F868;  /* Options */
extern GObj* D_800E82DC_A0F86C;
extern GObj* D_800E82E0_A0F870;
void func_800E3240_A0A7D0(GObj* gobj);
s32 func_800E3E28_A0B3B8(void);
void func_800E1AEC_A0907C(void);
void func_800E1AD4_A09064(void);
void func_800E2348_A098D8(void);
void func_800E23E4_A09974(void);
void func_800E2480_A09A10(void);
void func_800E251C_A09AAC(void);
void func_800E25B8_A09B48(void);
void func_800E2654_A09BE4(void);
void func_800E2780_A09D10(void);
s32 func_800E1CCC_A0925C(void);
s32 checkPlayerFlag(s32 pfid);
void auSetBGMVolumeSmooth(s32 playerID, u32 vol, u32 time);
void func_800E1B78_A09108(u8 arg0);   /* replaced below; used before that */

/* The label, or NULL when the port staged no strip for it. Hidden until
 * the builder shows it; no SP_TEXSHUF, since the strip is staged for the
 * plain load the pages' strips use. */
static GObj* snap_title_station_label(void) {
    Sprite* sp;
    GObj* gobj;

    if ((DIR_MAGIC != 0x53474130) || ((u32) STR_TITLE_STATION >= DIR_COUNT)) {
        return NULL;
    }
    if (DIR_W(STR_TITLE_STATION) <= 0) {
        return NULL;
    }
    sp = snap_build_sprite(STR_TITLE_STATION, TITLE_STATION_X, TITLE_ROW_Y(3), G_IM_FMT_IA);
    if (sp == NULL) {
        return NULL;
    }
    gobj = ohCreateSprite(0xE, ohUpdateDefault, 0, 0x80000000, renDrawSprite, 1, 0x80000000, -1,
                          sp, 0, NULL, 1);
    if (gobj == NULL) {
        return NULL;
    }
    func_800E18A0_A08E30(gobj->data.sobj, SP_HIDDEN | SP_TRANSPARENT);
    func_800E18E0_A08E70(gobj->data.sobj, 0x80, 0x80, 0x80);
    return gobj;
}

/* Stock, plus the Snap Station label beside the Gallery's. */
void func_800E281C_A09DAC(void) {
    switch (D_800BF051) {
        case 0:
            D_800E82ED_A0F87D = checkPlayerFlag(PFID_16);
            func_800E2348_A098D8();
            func_800E23E4_A09974();
            D_800E80D0_A0F660 = func_800E1CCC_A0925C();
            SCRATCH_TITLE_GOBJ = 0;
            if (D_800E80D0_A0F660 == 1) {
                func_800E251C_A09AAC();
                SCRATCH_TITLE_GOBJ = (u32) snap_title_station_label();
            }
            func_800E25B8_A09B48();
            if (D_800E82ED_A0F87D != 0) {
                func_800E2480_A09A10();
                func_800E2654_A09BE4();
            }
            break;
        case -1:
            func_800E2780_A09D10();
            break;
    }
}

/* One row of the list: placed, shown, and the stock unselected tint. */
static void snap_title_row(GObj* gobj, s16 x, s16 y) {
    func_800E18FC_A08E8C(gobj->data.sobj, x, y);
    func_800E18AC_A08E3C(gobj->data.sobj, 1);
    func_800E18E0_A08E70(gobj->data.sobj, 0xC0, 0xC0, 0);
}

/* Stock for two, three and four items (the y values are the game's table,
 * D_800E80E4_A0F674), and five when the Snap Station label exists. */
u8 func_800E33C8_A0A958(GObj** gobjs) {
    GObj* station;

    if (D_800E82ED_A0F87D == 0) {
        gobjs[0] = D_800E82CC_A0F85C;
        snap_title_row(gobjs[0], 127, 138);
        gobjs[1] = D_800E82D8_A0F868;
        snap_title_row(gobjs[1], 128, 162);
        return 2;
    }
    if (D_800E80D0_A0F660 != 1) {
        gobjs[0] = D_800E82CC_A0F85C;
        snap_title_row(gobjs[0], 127, 132);
        gobjs[1] = D_800E82D0_A0F860;
        snap_title_row(gobjs[1], 128, 150);
        gobjs[2] = D_800E82D8_A0F868;
        snap_title_row(gobjs[2], 128, 168);
        return 3;
    }
    station = (GObj*) SCRATCH_TITLE_GOBJ;
    if (station == NULL) {
        gobjs[0] = D_800E82CC_A0F85C;
        snap_title_row(gobjs[0], 127, 120);
        gobjs[1] = D_800E82D0_A0F860;
        snap_title_row(gobjs[1], 128, 138);
        gobjs[2] = D_800E82D4_A0F864;
        snap_title_row(gobjs[2], 116, 155);
        gobjs[3] = D_800E82D8_A0F868;
        snap_title_row(gobjs[3], 128, 174);
        return 4;
    }
    gobjs[0] = D_800E82CC_A0F85C;
    snap_title_row(gobjs[0], 127, TITLE_ROW_Y(0));
    gobjs[1] = D_800E82D0_A0F860;
    snap_title_row(gobjs[1], 128, TITLE_ROW_Y(1));
    gobjs[2] = D_800E82D4_A0F864;
    snap_title_row(gobjs[2], 116, TITLE_ROW_Y(2));
    gobjs[3] = station;
    snap_title_row(gobjs[3], TITLE_STATION_X, TITLE_ROW_Y(3));
    gobjs[4] = D_800E82D8_A0F868;
    snap_title_row(gobjs[4], 128, TITLE_ROW_Y(4));
    return 5;
}

/* Stock, with Snap Station between Gallery and Options when its label
 * exists: the station is requested of the host and the Gallery's own path
 * is taken. */
s32 func_800E37E8_A0AD78(s32 arg0, s8 arg1) {
    s32 station;

    if (D_800E82ED_A0F87D == 0) {
        switch (arg1) {
            case 0:
                auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
                func_800E1B78_A09108(1);
                return 6;
            case 1:
                auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
                func_800E1B78_A09108(1);
                return 8;
        }
    } else {
        switch (arg1) {
            case 0:
                if (func_800E3E28_A0B3B8() == 6) {
                    func_800E1B78_A09108(1);
                    return 6;
                } else {
                    return 4;
                }
            case 1:
                auPlaySoundWithParams(0x40, 0x7FFF, 0x40, 1.0f, 0);
                func_800E1B78_A09108(0);
                return 7;
            default:
                if (D_800E80D0_A0F660 == 1) {
                    station = (SCRATCH_TITLE_GOBJ != 0);
                    if (arg1 == 2) {
                        auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
                        func_800E1B78_A09108(0);
                        return 9;
                    } else if (station && (arg1 == 3)) {
                        MBOX_TITLE_REQ = 1;
                        auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
                        func_800E1B78_A09108(0);
                        return 9;
                    } else if (arg1 == (station ? 4 : 3)) {
                        auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
                        func_800E1B78_A09108(1);
                        return 8;
                    }
                } else {
                    auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
                    func_800E1B78_A09108(1);
                    return 8;
                }
        }
    }
    return 0;
}

/* Stock, with a five-slot buffer for the list. */
s32 func_800E3974_A0AF04(s8 arg0) {
    UnkStruct800BEDF8* temp_v0;
    GObj* sp54[5];
    s32 ret;
    s8 temp_s3;
    s8 var_s0;
    u8 i;

    if (D_800E82E4_A0F874 == 4) {
        var_s0 = 0;
    } else if (D_800E82ED_A0F87D != 0) {
        var_s0 = 1;
    } else {
        var_s0 = 0;
    }

    temp_s3 = func_800E33C8_A0A958(sp54);
    omCreateProcess(sp54[var_s0], func_800E3240_A0A7D0, 0, 1);
    ohWait(1);

    while (1) {
        temp_v0 = func_800AA38C(0);
        if (temp_v0->pressedButtons != 0) {
            func_800E1AEC_A0907C();
            func_800E1AD4_A09064();
        }

        if (D_800E82E8_A0F878 >= 1800 && D_800BF051 == 0) {
            func_800E1B78_A09108(0);
            if (arg0 == 0xF) {
                ret = 11;
            } else {
                ret = 10;
            }
            break;
        }

        if (temp_v0->pressedButtons & STICK_SLOW_UP) {
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
            ohEndAllObjectProcesses(sp54[var_s0]);
            func_800E18E0_A08E70((sp54[var_s0])->data.sobj, 0xC0, 0xC0, 0);
            var_s0--;
            if (var_s0 < 0) {
                var_s0 = temp_s3 - 1;
            }
            omCreateProcess(sp54[var_s0], func_800E3240_A0A7D0, 0, 1);
        } else if (temp_v0->pressedButtons & STICK_SLOW_DOWN) {
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
            ohEndAllObjectProcesses(sp54[var_s0]);
            func_800E18E0_A08E70(sp54[var_s0]->data.sobj, 0xC0, 0xC0, 0);
            var_s0++;
            var_s0 %= temp_s3;
            omCreateProcess(sp54[var_s0], func_800E3240_A0A7D0, 0, 1);
        } else if (temp_v0->pressedButtons & B_BUTTON) {
            auPlaySoundWithParams(0x43, 0x7FFF, 0x40, 1.0f, 0);
            ohEndAllObjectProcesses(sp54[var_s0]);
            for (i = 0; i < temp_s3; i++) {
                func_800E18AC_A08E3C(sp54[i]->data.sobj, 0);
            }
            ret = 2;
            break;
        } else if (temp_v0->pressedButtons & (0x8000 | 0x1000)) {
            ret = func_800E37E8_A0AD78(D_800E82ED_A0F87D, var_s0) & 0xFF;
            break;
        }

        ohWait(1);
    }
    return ret;
}

/* Stock, plus the Snap Station label's deletion beside the Gallery's. */
void func_800E1B78_A09108(u8 arg0) {
    if (!arg0) {
        auSetBGMVolumeSmooth(0, 0, 60);
        func_800E1930_A08EC0(1, 0, 0, 0, 1.0f);
    } else {
        auSetBGMVolumeSmooth(0, 0, 30);
        ohWait(30);
    }
    omDeleteGObj(D_800E82B0_A0F840);
    omDeleteGObj(D_800E82BC_A0F84C);
    omDeleteGObj(D_800E82C0_A0F850);
    omDeleteGObj(D_800E82C4_A0F854);
    if (D_800BF051 == 0) {
        omDeleteGObj(D_800E82C8_A0F858);
        omDeleteGObj(D_800E82CC_A0F85C);
        if (D_800E82ED_A0F87D != 0) {
            omDeleteGObj(D_800E82D0_A0F860);
            omDeleteGObj(D_800E82DC_A0F86C);
        }
        if (D_800E80D0_A0F660 == 1) {
            omDeleteGObj(D_800E82D4_A0F864);
            if (SCRATCH_TITLE_GOBJ != 0) {
                omDeleteGObj((GObj*) SCRATCH_TITLE_GOBJ);
                SCRATCH_TITLE_GOBJ = 0;
            }
        }
        omDeleteGObj(D_800E82D8_A0F868);
    } else {
        omDeleteGObj(D_800E82E0_A0F870);
    }
}
