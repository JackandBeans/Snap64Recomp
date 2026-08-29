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
 * Settings travel through the 16-byte mailbox at 0x80C00000: the port seeds
 * it with saved values, the page edits bytes and bumps a sequence counter,
 * and the port applies and persists on each bump -- every change is live
 * while the menu is open.
 */

#include "common.h"

#include "sys/om.h"
#include "PR/sp.h"

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
 * src/menu_assets.cpp. One extra byte of the mailbox block is scratch for
 * the patch itself: the current selection, readable from every coroutine. */
#define SNAP_GFX_MAILBOX   0x80C00000
#define SNAP_GFX_ASSETS    0x80C01000

#define MBOX_SEQ     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x4))
#define MBOX_FIELD(i) (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x8 + (i)))
#define MBOX_SEL     (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x16))

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

#define OPT_ITEMS      6    /* Screen, Graphics, Sound, Z, Stick, Return */
#define OPT_GRAPHICS   1
#define PAGE_ITEMS     12
#define PAGE_VISIBLE   8    /* rows shown at once; the rest scroll into view */
#define PAGE_TOP_Y     56
#define PAGE_PITCH     14

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
#define SCRATCH_HELP_PAGE     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x20))
/* Diagnostic heartbeat the port prints when it changes. */
#define MBOX_DBG              (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x28))

/* Pointer arrays live in the mailbox block's spare space, NOT on the stack.
 * These functions run on a GObj process coroutine, and those threads get a
 * few hundred bytes of stack with a canary at the far end -- ohWait checks
 * it every call and a blown canary is a trip straight to the game's crash
 * screen ("gobjthread stack over"), which on the port is a silent freeze.
 * Measured: the page's original ~600 bytes of local arrays killed it. */
#define SCRATCH_ARRAYS        (SNAP_GFX_MAILBOX + 0x100)
#define PAGE_LABEL(i)  (*(volatile u32*) (SCRATCH_ARRAYS + 0x00 + (i) * 4))   /* GObj*, 12 */
#define PAGE_VALUE(i)  (*(volatile u32*) (SCRATCH_ARRAYS + 0x30 + (i) * 4))   /* GObj*, 12 */
#define PAGE_HIDDEN(i) (*(volatile u32*) (SCRATCH_ARRAYS + 0x60 + (i) * 4))   /* SObj*, 64 */
#define LIST_LABEL(i)  (*(volatile u32*) (SCRATCH_ARRAYS + 0x160 + (i) * 4))  /* SObj*, 8 */
#define LIST_HELP(i)   (*(volatile u32*) (SCRATCH_ARRAYS + 0x180 + (i) * 4))  /* SObj*, 8 */

/* The page's twelve rows, in display order. Each row cycles one mailbox
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
        default: return 7;    /* Fullscreen */
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
        default: return STR_L_SCALE + 7;
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
        default: return STR_DESC + 7;
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
        case 1:  return 4;   /* Super Sampling: Off, 2x, 3x, 4x */
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
        default: return v ? STR_ON : STR_OFF;  /* Widescreen, Dither, Fullscreen */
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
    u8 pulseState, pulseCounter;

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
    /* Five left of the stock header sprite's anchor: measured against the
     * rendered "Options" title, this lands the G where the O sits. */
    hdrStrip = snap_make_strip(STR_HDR, 40, 41);

    /* The help box shows what the selected setting does; the list's item
     * help hides while the page is open. */
    {
        GObj* itemHelp = (GObj*) SCRATCH_HELP_ITEM;
        if ((itemHelp != NULL) && (itemHelp->data.sobj != NULL)) {
            itemHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
    }
    descStrip = snap_make_strip(STR_DESC + 0, 50, 172);

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

    sel = 0;
    top = 0;
    snap_page_layout(top);
    pulseState = 0;
    pulseCounter = 0;

    ohWait(2);

    while (1) {
        input = func_800AA38C(0);
        moved = 0;

        if (gContInputPressedButtons & B_BUTTON) {
            auPlaySoundWithParams(0x43, 0x7FFF, 0x40, 1.0f, 0);
            break;
        }

        if (input->pressedButtons & STICK_SLOW_UP) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel == 0) ? (PAGE_ITEMS - 1) : (sel - 1);
            pulseState = 0;
            if (sel < top) {
                top = sel;
                snap_page_layout(top);
            }
            else if (sel >= top + PAGE_VISIBLE) {
                top = sel - (PAGE_VISIBLE - 1);
                snap_page_layout(top);
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
            }
            else if (sel >= top + PAGE_VISIBLE) {
                top = sel - (PAGE_VISIBLE - 1);
                snap_page_layout(top);
            }
            snap_swap_strip(descStrip, snap_row_desc(sel));
            auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
        }
        else if ((input->pressedButtons & STICK_SLOW_RIGHT) ||
                 (gContInputPressedButtons & A_BUTTON)) {
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

        /* At the stock label column and row cadence: the strip now carries
         * the items' bullet dot at its start, like every other row. White,
         * like every stock label shows when unselected. */
        graphicsLabel = snap_make_strip(STR_ITEM_LABEL, 43, 89);
        /* At the stock help sprites' own text inset: their strips bake nine
         * columns and four rows of padding; this strip bakes none. */
        itemHelp = snap_make_strip(STR_ITEM_HELP, 50, 172);
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
                case 2:
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
         * with the static logo's creation everywhere else. */
        badge->attr |= SP_HIDDEN;
        if (arg0 == 0xC) {
            badge->red = badge->green = badge->blue = 0x80;
        }
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
    func_800E18FC_A08E8C(sobj, 74, 198);
    func_800E18A0_A08E30(sobj, SP_TEXSHUF | SP_TRANSPARENT);

    snap_show_badge();
}

/* Replaces the title's letter bounce: the eight "Pokemon" pieces land one
 * by one exactly as shipped, and the badge is revealed in the very moment
 * the full logo replaces them -- inside the same white flash that brings
 * "Snap" in, with nothing else about the sequence touched. */
void func_800E28CC_A09E5C(void) {
    UnkStruct800BEDF8* temp_v0;
    s32 reverb;
    f32 temp_f0;
    f32 x;
    f32 y;
    SObj* sobj;
    GObj* gobj;
    u8 j;
    u8 i;

    reverb = 0;
    gobj = D_800E82B4_A0F844;
    sobj = gobj->data.sobj;

    for (i = 0; i < 8; i++) {
        x = sobj->sprite.x + (sobj->sprite.width / 2);
        y = sobj->sprite.y + (sobj->sprite.height / 2);

        func_800E18AC_A08E3C(sobj, 1);

        for (j = 0; j < 6; j++) {
            temp_v0 = func_800AA38C(0);
            if (temp_v0->pressedButtons & (0x8000 | 0x1000)) {
                break;
            }
            temp_f0 = 1.2 - ((j * 0.2) / 5.0);
            sobj->sprite.scaley = temp_f0;
            sobj->sprite.scalex = temp_f0;
            func_800E18FC_A08E8C(
                sobj,
                x - (sobj->sprite.width * temp_f0 * 0.5),
                y - (sobj->sprite.height * temp_f0 * 0.5));
            ohWait(1);
        }

        if (i == 6) {
            reverb = 10;
        }
        if (temp_v0->pressedButtons & (0x8000 | 0x1000)) {
            break;
        }
        if (!(i & 1)) {
            /* The pan table is four 32-bit entries, one per letter pair. */
            auPlaySoundWithParams(0, 0x7FFF, ((s32*) 0x800E80D4)[i >> 1], 1.0f, reverb);
        }
        sobj = sobj->next;
    }
    ohRemoveSprite(gobj);
    omGObjAddSprite(gobj, (Sprite*) 0x802E8DD0);

    sobj = gobj->data.sobj;
    func_800E18FC_A08E8C(sobj, 35, 35);
    func_800E18A0_A08E30(sobj, SP_TEXSHUF | SP_TRANSPARENT);
    snap_show_badge();
    func_800E1930_A08EC0(1, 0xFF, 0xFF, 0xFF, 17.0f / 120.0f);
    omDeleteGObj(gobj);
    omDeleteGObj(D_800E82B0_A0F840);
}
