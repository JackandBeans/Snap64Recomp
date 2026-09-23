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
#include "vr_pointer.inc"

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

/* The stick as the controller reports it, -80..80 each way, up positive.
 * The pages used to move on the game's slow-stick bits, which fire at a
 * small deflection in any direction: scrolling a page with a thumb that
 * drifted a little sideways changed the row's value on the way. The pages
 * read the stick themselves now: a dead band, the dominant axis only, an
 * edge, and a slow repeat while it is held. */
extern s8 gContInputStickX;
extern s8 gContInputStickY;
static s32 snap_nav_dir_v = 0;
static s32 snap_nav_dir_h = 0;
static s32 snap_nav_repeat_v = 0;
static s32 snap_nav_repeat_h = 0;
/* The CONTROLS page reopens on this row when the BUTTON SETUP page closes (its
 * Button Setup row opened it), and the two hand the hidden Option list to each
 * other -- the count of PAGE_HIDDEN entries still hidden -- so the list
 * never shows for a frame between them. */
static s32 snap_ctl_reopen_row;
static s32 snap_list_hidden_carry;

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

/* The sprite field writes the pages used to make through the main menu
 * overlay's little helpers (A08E30 and A0E190: an attribute, a colour, a
 * position, hidden or shown). The overlay is not loaded in a course, and
 * the pages open there too now (pause_menu_patch.c), so the writes are
 * made here; they are what the helpers did, line for line. */
static void snap_sprite_gray(SObj* sobj, u8 v) {
    sobj->sprite.red = v;
    sobj->sprite.green = v;
    sobj->sprite.blue = v;
}
static void snap_sprite_rgb(SObj* sobj, u8 red, u8 green, u8 blue) {
    sobj->sprite.red = red;
    sobj->sprite.green = green;
    sobj->sprite.blue = blue;
}
static void snap_sprite_attr(SObj* sobj, u32 attr) {
    sobj->sprite.attr = (u16) attr;
}
static void snap_sprite_show(SObj* sobj, u8 show) {
    if (!show) {
        sobj->sprite.attr |= SP_HIDDEN;
    } else {
        sobj->sprite.attr &= ~SP_HIDDEN;
    }
}
static void snap_sprite_move(SObj* sobj, s16 x, s16 y) {
    sobj->sprite.x = x;
    sobj->sprite.y = y;
}

/* Where the pages are open: 0 the Option screen, whose title, item list
 * and hints they hide and restore; 1 a course's pause menu
 * (pause_menu_patch.c), where none of that exists and the overlay's
 * globals behind snap_chain are not even loaded. */
static s32 snap_page_ctx = 0;

/* The pages' three sounds. The Option screen's are the menu bank's; a
 * course's bank has the pause menu's own, which are used there. */
#define SND_MOVE 0
#define SND_OK   1
#define SND_BACK 2
s32 auPlaySound(u32 soundID);
s32 auPlaySoundWithParams(u32 soundID, s32 volume, s32 pan, f32 pitch, s32 reverbAmt);   /* defined below (the SFX volume) */
/* Start, in a course, closes every page at once and the pause menu's
 * coroutine resumes the ride (pause_menu_patch.inc): a page leaves as it
 * does on A, keeping what is on screen, and the list closes after it. On
 * the Option screen Start does nothing, as before. Not on the Button Setup
 * page, where Start is a button to bind. */
static s32 snap_close_all = 0;
static s32 snap_start_closes(void) {
    if ((snap_page_ctx != 0) && (gContInputPressedButtons & CONT_START)) {
        snap_close_all = 1;
        return 1;
    }
    return 0;
}

/* The list's dress over a course or any other screen (the course block
 * below): its legend, which the Mods page takes down for its own hint. */
static void snap_course_legend_show(s32 show);
static void snap_course_gobj_show(u32 word, s32 show);

static void snap_ui_sound(s32 kind) {
    if (snap_page_ctx == 1) {
        auPlaySound((kind == SND_OK) ? 66 : 65);
        return;
    }
    auPlaySoundWithParams((kind == SND_MOVE) ? 0x41 : (kind == SND_OK) ? 0x42 : 0x43, 0x7FFF, 0x40, 1.0f, 0);
}
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
 *                Rate (0 Original, 1 Display, 2.. a held rate), 4 2D
 *                Detail, 5 Filter, 6 Dither, 7 Fullscreen,
 *                8 Super Sampling, 9 Texture Filter, 10 Color Depth,
 *                11 Buffering, 12 Overscan Crop, 13 Cutscene Fix (also
 *                read by the intro patches), 14 Photo Detail, 15 VC
 *                Recolor. The bank is full: field 16 would be the
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
 *   +0x44  u32  SNAP_VIEW_WIDE_Q8, host-owned: the renderer's horizontal
 *               widening under Widescreen in Q8 (256 = none), rewritten
 *               every tick; read by widescreen_cull_patch.c
 *   +0x48  u32  SNAP_VIEW_WIDE_SAVED, host-owned and zeroed at the seed:
 *               verdicts the widened bound turned from culled to drawn,
 *               counted by widescreen_cull_patch.c, printed under SNAP_STATS
 *   +0x34  u8   MBOX_SEL, the patch's own: the current selection,
 *               readable from every coroutine
 *   +0x38  u8   MBOX_TITLE_REQ: the title's Snap Station item was chosen;
 *               the host reads it, clears it, attaches the station
 *   +0x3A  u8   MBOX_QUIT_REQ: the Option screen's Exit Game item was
 *               chosen and confirmed; the host reads it, clears it, and
 *               closes the program the way the window's close button does
 *   +0x50  u32  SCRATCH_TITLE_GOBJ, the patch's own: the title's Snap
 *               Station label, between its creation and its deletion
 *   +0x54  u32  SCRATCH_CONTROLS_GOBJ, the patch's own: the CONTROLS item
 *   +0x58  u32  SCRATCH_HELP_CONTROLS, the patch's own: its help line
 *   +0x5C  u32  SCRATCH_MODS_GOBJ, the patch's own: the MODS item
 *   +0x70  u32  SCRATCH_EXIT_GOBJ, the patch's own: the EXIT GAME item
 *   +0x74  u32  SCRATCH_HELP_EXIT, the patch's own: its help line
 *   +0x78  u32  SCRATCH_HELP_EXIT2, the patch's own: its question line
 *   +0x84  u32  SCRATCH_HELP_MODS, the patch's own: the MODS item's help line
 *   +0x60  u32  CONTROLS sequence word
 *   +0x64  u8   CONTROLS fields 0..9, through +0x6D: mouse aim, the mouse
 *               speed's step, the zoom speed's step, the tilt, the gyro
 *               mode (off, on, zoomed), the gyro speed's step, the pad
 *               sticks swapped, the stick dead zone's step (fives), the
 *               fast forward speed (0 off, 1..3 for 2x, 3x, 4x), the slow
 *               motion speed (0 off, 1 half, 2 a quarter)
 *   +0x7C  u32  MBOX_POOL_FAIL, the patch's own: strips the pool refused
 *   +0x80  u32  MBOX_POOL_PEAK, the patch's own: the pool's high water
 *   +0xA0  u32  BIND_REQ, the BUTTON SETUP page's request to the host: the
 *               operation in bits 16..23 (1 listen for a press, 2 clear,
 *               3 the shipped table back), the device in 8..15 (0
 *               keyboard, 1 mouse, 2 pad), the input row in 0..7 (1..18)
 *   +0xA4  u32  BIND_ACK, the host's answer: the request in the low 24
 *               bits, the result above them (1 done, 2 cancelled, 3
 *               refused, a key with a job of its own, 4 refused, nothing
 *               else would press the input, 5 nothing pressed in time)
 *   +0xA8  u32  BIND_GEN, host-owned: bumped when the page's row values
 *               were recomposed; its low bit is the bank of ids to show
 *   +0xAC  u8   BIND_DEVICE, the page's: the device it shows
 *   +0xAD  u8   BIND_OPEN, the page's: 1 while it is open
 *   +0xAE  u8   BIND_PAD, host-owned: 1 while a pad is attached
 *   +0xB0  u32  MODS_REQ, the MODS page's request to the host: the
 *               operation in bits 16..23 (1 turn the row's mod on or off),
 *               the row in bits 0..15
 *   +0xB4  u32  MODS_ACK, the host's answer: the request in the low 24
 *               bits, the result above them (1 the mod is on now, 2 off,
 *               3 nothing done)
 *   +0xB8  u32  MODS_GEN, host-owned: bumped when the window's names and
 *               help lines were recomposed; its low bit is the bank shown
 *   +0xBC  u16  MODS_COUNT, host-owned: how many mods the folder holds
 *   +0xBE  u8   MODS_OPEN, the page's: 1 while it is open
 *   +0xBF  u8   MODS_TOP, the page's: the first row of the window it shows
 *   +0xC0  u8   MODS_STATE, host-owned, one byte per bank: bit i is the
 *               window's row i, 1 when its mod is on
 *   +0x100      SCRATCH_ARRAYS, the page's pointer and snapshot arrays
 *               (the BUTTON SETUP page's own twenty-row arrays sit at +0x300
 *               and +0x350 inside it)
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
/* "Jynx Recolor": the purple Jynx of the re-releases (settings.h jynx_vc).
 * The label's J is one of the port's own glyphs (the body face has no
 * capital J), and the help face the description is set in has no J, no V
 * and no hyphen, so the description says what changes and where, never
 * the name or the re-releases' platforms. */
#define STR_JYNX_LABEL  88
#define STR_JYNX_DESC   89
/* "Snap Station" in the title menu's own face, for the title's fifth item
 * (the title section at the end of this file). Width 0 when the port could
 * not compose it, and then there is no fifth item. */
#define STR_TITLE_STATION 90
/* The CONTROLS page (menu_assets.cpp ids BaseCount+61..+90). */
#define STR_CTL_HDR       91   /* "Controls" in the header face */
#define STR_CTL_ITEM      92   /* the Option item's label, with the dot */
#define STR_CTL_ITEM_HELP 93   /* its help line */
#define STR_CTL_LABEL     94   /* ..99: Z Button, Control Stick, Mouse Aim,
                                * Mouse Speed, Zoom Speed, Mouse Tilt */
#define STR_HOLD          100
#define STR_SWITCH        101
#define STR_NORMAL        102
#define STR_REVERSE       103
#define STR_CTL_SPEED     104  /* ..114: 25 50 75 100 125 150 175 200 250 300 400 */
#define STR_CTL_DESC      115  /* ..120: the six descriptions */
/* The page's two gyro rows (menu_assets.cpp ids BaseCount+91..+95). */
#define STR_GYRO_LABEL    121  /* ..122: Gyro Aim, Gyro Speed */
#define STR_ZOOMED        123
#define STR_GYRO_DESC     124  /* ..125: their descriptions */
/* The Option list's sixth item, Exit Game (menu_assets.cpp ids
 * BaseCount+96..+98): the label with its dot, its help line, and the
 * question the help line becomes once it is chosen. "Exit Game" because
 * every letter of it is in the body face; "Quit" has no Q there. */
#define STR_EXIT_ITEM     126
#define STR_EXIT_HELP     127
#define STR_EXIT_CONFIRM  128
/* The BUTTON SETUP page (menu_assets.cpp ids BaseCount+99..+188). */
#define STR_BTN_LABEL        129  /* "Button Setup", the CONTROLS page's third row */
#define STR_BTN_DESC         130  /* its help line */
#define STR_BIND_HDR         131  /* "Button Setup" in the header face */
#define STR_BIND_DEVICE      132  /* the Device row's label */
#define STR_BIND_INPUT       133  /* ..150: the eighteen input rows' labels */
#define STR_BIND_RESET       151  /* Restore Defaults */
#define STR_BIND_KEYBOARD    152  /* ..154: Keyboard, Mouse, Controller */
#define STR_BIND_DESC_DEVICE 155
#define STR_BIND_DESC_CONFIRM 156 /* Restore Defaults, asking */
#define STR_BIND_DESC_LISTEN 157
#define STR_BIND_DESC_RESET  158  /* Restore Defaults, at rest */
#define STR_BIND_DESC_JOB    159
#define STR_BIND_DESC_KEEP   160
#define STR_BIND_DESC_TIMEOUT 161
#define STR_BIND_DESC_INPUT  162  /* ..179: what each input does, per row */
/* The CONTROLS page's Pad Sticks row: its label, its Swapped value (Normal
 * is the Camera Tilt row's), its description. */
#define STR_STICKS_LABEL     180
#define STR_SWAPPED          181
#define STR_STICKS_DESC      182
/* The CONTROLS page's Dead Zone row: its label, its description, and the
 * three values the volume and speed steps do not already carry. */
#define STR_DZ_LABEL         183
#define STR_DZ_DESC          184
#define STR_DZ5              185
#define STR_DZ15             186
#define STR_DZ35             187
/* The Frame Rate row's held rates past 90 (60 and 90 are STR_VOL0+6 and
 * +9): 120, 144, 165, 240. */
#define STR_FPS120           188
#define STR_FPS144           189
#define STR_FPS165           190
#define STR_FPS240           191
/* ..227: the input rows' values, two banks of eighteen, composed live by
 * the host for the device shown (BIND_GEN's low bit names the bank). */
#define STR_BIND_DYN         192
/* The CONTROLS page's Fast Forward row: its label and its description
 * (menu_assets.cpp ids BaseCount+198 and +199); its values are Off and the
 * Graphics page's 2x, 3x, 4x. */
#define STR_FF_LABEL         228
#define STR_FF_DESC          229
/* The CONTROLS page's Slow Motion row, the same way (ids BaseCount+200 and
 * +201); its values are Off and the Graphics page's 2x and 4x. */
#define STR_SM_LABEL         230
#define STR_SM_DESC          231
/* The MODS page (snap_mods_page): the Option list's item and its help line,
 * the heading, the empty-folder line, and the strips the host composes
 * while the page is open -- two banks of six names and six help lines, one
 * per visible row of the window the page shows. */
#define STR_MODS_ITEM        232
#define STR_MODS_ITEM_HELP   233
#define STR_MODS_HDR         234
#define STR_MODS_NONE        235
#define STR_MODS_DYN         236  /* ..259: bank b, name i at b*12+i, help i at b*12+6+i */
/* The pause menu's OPTIONS pill (pause_menu_patch.inc), plain and selected,
 * and the list it opens in a course (snap_course_options_page): its heading,
 * the Sound item with its help line (the Option screen takes those from the
 * stock sprites), and the Option screen's dress -- the rule (240x3 IA16),
 * the help box's side (3x30) and the header's A OK / B Cancel legend, which
 * is the one 32-bit strip (RGBA32, the game's own texels). */
#define STR_PAUSE_PILL       260
#define STR_COURSE_HDR       261
#define STR_SND_ITEM         262
#define STR_SND_ITEM_HELP    263
#define STR_PAUSE_PILL_SEL   264
#define STR_DRESS_RULE       265
#define STR_DRESS_SIDE       266
#define STR_DRESS_LEGEND     267
/* A mod's options page (snap_mod_options_page): two banks of six names, six
 * values and six help lines the host composes for the window shown; the
 * Mods page's hint at the header's right, in its three forms; the options
 * page's heading and its line for a mod without options. */
#define STR_OPT_DYN          268  /* ..303: bank b, name i at b*18+i, value at b*18+6+i, help at b*18+12+i */
#define STR_MODS_HINT_BOTH   304
#define STR_MODS_HINT_OPT    305
#define STR_MODS_HINT_ORDER  306
#define STR_OPT_HDR          307
#define STR_OPT_NONE         308

/* The SOUND bank of the mailbox: its own sequence word and value bytes
 * (percent volumes; stereo and background-mute booleans). The patched
 * audio functions below read the bytes live on every call. */
#define MBOX_MAGIC   (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x0))
/* The pages from anywhere (anywhere_patch.inc, pause_menu_patch.inc): the
 * host sets MENU_REQ when Esc or the pad's Select is pressed in a course,
 * and the pause menu's code takes it; PAGES_OPEN is the patch's word that a
 * port page, or the Option screen itself, is up, so the host makes the key
 * Start instead (which closes the pages) rather than opening them twice. */
#define MBOX_MENU_REQ   (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x39))
#define MBOX_PAGES_OPEN (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x3D))
/* The room the screen's main display list buffer had left last frame (the
 * host measures it in gtlCheckBuffers, src/dl_budget.cpp). The strips are
 * sprites drawn into that buffer, the buffer is the cartridge's own size,
 * and an overrun is the game's panic: the pages stay shut where the room
 * is less than they may draw. */
#define MBOX_DL_SPARE (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xE8))
#define SNAP_PAGES_DL_NEED 8192
/* Written to 3 by the pause menu's handler every frame it runs and counted
 * down by the host: while it is above zero a ride is on and the key is
 * the pause code's; the title, its demo and a course's intro have the
 * level's code loaded but no pause handler, and get the runner instead. */
#define MBOX_PAUSE_ALIVE (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x3E))
/* The runner's word on what it did with a press, for the host's log:
 * 1 no staged strings, 2 a page was already up, 3 the pages ran. */
#define MBOX_RUNNER_NOTE (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x3F))
#define SND_SEQ      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x20))
#define SND_FIELD(i) (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x28 + (i)))

/* The Option list: Screen, Graphics, Sound, Controls, Return, Exit Game.
 * The stock Z Button and Control Stick rows live on the CONTROLS page now,
 * with the mouse's settings, so the stock four and the port's two keep the
 * stock rhythm; six rows is the most the help box leaves room for, and
 * Exit Game is the sixth: a pad has no other way to close the program,
 * which a handheld in Steam's Gaming Mode needs. It exists only when its
 * label was staged (snap_option_labels says how many rows there are). */
#define OPT_ITEMS      6
#define OPT_GRAPHICS   1
#define OPT_SOUND      2
#define OPT_CONTROLS   3
#define OPT_RETURN     4
#define OPT_EXIT       5
/* Not a row: what the selection loop returns for A on the Return row while
 * the MODS item stands there (snap_option_labels), so the dispatcher can
 * tell it from B, which comes back as OPT_RETURN as well. */
#define OPT_MODS       6
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
static GObj* snap_make_strip_siz(s32 id, s32 x, s32 y, u8 fmt, u8 siz);

static GObj* snap_make_strip(s32 id, s32 x, s32 y) {
    return snap_make_strip_fmt(id, x, y, G_IM_FMT_IA);
}

/* Builds the Sprite + Bitmap chain for a staged strip without creating a
 * GObj, so a strip can also ride an existing object's sprite chain. The
 * texels are 16-bit (IA16 text; RGBA16 for the wordmark, the arrows and
 * the pause menu's pill) but for the Option screen's legend over a course,
 * which is staged as the game keeps it, 32-bit RGBA. */
static Sprite* snap_build_sprite(s32 id, s32 x, s32 y, u8 fmt, u8 siz);

static GObj* snap_make_strip_fmt(s32 id, s32 x, s32 y, u8 fmt) {
    return snap_make_strip_siz(id, x, y, fmt, G_IM_SIZ_16b);
}

/* The draw link the strips sit on. Link 1 is the one the screens with pages
 * of their own draw through a sprite camera (the title's Options screen, a
 * course's pause). The pages opened from anywhere land on screens that may
 * draw no such link at all -- the photo screens after a ride showed nothing
 * of them while the list took every press -- so the runner moves them to a
 * link no screen uses, with a camera of its own (anywhere_patch.inc). */
#define SNAP_PAGES_DL_LINK 29
static u8 snap_strip_dl_link = 1;

static GObj* snap_make_strip_siz(s32 id, s32 x, s32 y, u8 fmt, u8 siz) {
    Sprite* sp = snap_build_sprite(id, x, y, fmt, siz);
    if (sp == NULL) {
        return NULL;
    }
    return ohCreateSprite(0xE, NULL, 0, 0x80000000, renDrawSprite, snap_strip_dl_link, 0x80000000, -1,
                          sp, 0, NULL, 1);
}

static GObj* snap_make_strip_32(s32 id, s32 x, s32 y) {
    return snap_make_strip_siz(id, x, y, G_IM_FMT_RGBA, G_IM_SIZ_32b);
}

/* -------------------------------------------------------------------------
 * The strip pool
 *
 * Every strip on these pages is a Sprite plus its Bitmap array, and both
 * used to come out of gtlMalloc -- the scene's general heap, which is a bump
 * allocator with no free at all (sys/ml.c: mlHeapAlloc moves a cursor, and
 * on overflow calls PANIC, which is `while (1) {}`; the recompiler turns
 * that into pause_self, so the game thread parks forever with no message and
 * no crash screen). The only reset is gtlInitHeap, called from omSetupScene
 * and func_80007354 -- scene setup. The Option screen is a state INSIDE the
 * main-menu scene, so nothing between two visits ever moved that cursor
 * back. Each strip costs 136 bytes of it, a Graphics visit builds 36, and
 * the scene's heap is 1,887,328 bytes: a few hundred visits walked it off
 * the end and froze the game.
 *
 * The strips come from a fixed table now. It is sized by what can be on
 * screen at once, and a slot comes back by reachability rather than by a
 * free call at each teardown -- snap_strip_sweep says why that is the only
 * definition of "free" that is true here.
 *
 * How many slots. Every call site, counted:
 *
 *   the title screen        3   the Recomp badge on the background's chain,
 *                               the credits line on the logo's, and the
 *                               Snap Station label
 *   the Option list         6   the GRAPHICS item and its help line, the
 *                               CONTROLS item and its help line, the MODS
 *                               item and its help line; alive for the
 *                               whole visit
 *   one open page          36   the largest is GRAPHICS: header, description,
 *                               16 labels, 16 values, 2 scroll arrows.
 *                               (SOUND is 14, CONTROLS is 20, and the three
 *                               are dispatched from the same switch, so only
 *                               ever one of them is open.)
 *   ------------------------------------------------------------------------
 *   peak on screen         45
 *
 * 64 slots, then: the peak plus 21, which is more than a whole CONTROLS page
 * of not-yet-swept garbage, and leaves room for the seventeenth Graphics row
 * the arrays above are already sized for.
 * ---------------------------------------------------------------------- */

/* Six bitmaps a slot. The old allocator sized the array from the string and
 * gave a narrow one four anyway, so a later snap_swap_strip to a wider
 * string had somewhere to write; the slot has to cover the widest string
 * either path can ask for. Six -- 384 texels, wider than the screen -- so
 * that no string the stager will accept is ever truncated by the pool.
 * (snap_swap_strip's own clamp moves from 4 to this, and the stager's
 * warning threshold with it: src/menu_assets.cpp.) */
#define SNAP_STRIP_CHUNKS 6
#define SNAP_STRIP_SLOTS  64
/* 0x100 a slot, not the 0xA4 it uses: a round stride keeps the slot
 * arithmetic readable and leaves room to widen the chunk count again
 * without anything else moving. 64 * 0x100 is 16 KB of .bss. Correctness
 * does not depend on the number -- snap_strip_slot_of confirms a candidate
 * against the slot's own address. */
#define SNAP_STRIP_STRIDE 0x100

/* Sprite (0x44) + the bitmaps, and the rest of the stride left spare. */
#define SNAP_STRIP_INUSE (0x44 + SNAP_STRIP_CHUNKS * 0x10)

typedef struct SnapStripSlot {
    /* 0x00 */ Sprite sprite;                    /* 0x44 */
    /* 0x44 */ Bitmap bitmap[SNAP_STRIP_CHUNKS];
    /*      */ u8 spare[SNAP_STRIP_STRIDE - SNAP_STRIP_INUSE];
} SnapStripSlot;                                 /* SNAP_STRIP_STRIDE */

/* Patch statics live in the patch ELF's .bss, which the linker places right
 * above .data at 0x80801000+ (patch.ld). Verified on a build: the section
 * lands past the last byte objcopy puts in patches.bin, so librecomp's copy
 * of the patch image stops before it and never writes here; the recompiler
 * turns every access into the same absolute lui/addiu pair it uses for game
 * data, so nothing needs relocating at run time; and the rdram allocation is
 * zero-filled at start-up (VirtualAlloc / mmap MAP_ANON), so the table
 * starts empty without an initialiser. patches/check_bss.sh refuses a build
 * whose .bss reaches the menu mailbox at 0x80C00000; the ASSERT in patch.ld
 * says the same thing but only prints it, because ld runs with
 * --noinhibit-exec and exits 0 on a failed assert under that flag. */
static SnapStripSlot snap_strip_pool[SNAP_STRIP_SLOTS];
/* 1 while a slot must not be handed out. snap_strip_take sets it; a sweep
 * recomputes the whole array from what is actually on screen. */
static u8 snap_strip_used[SNAP_STRIP_SLOTS];
/* Round-robin, so a slot is not re-issued the instant it comes free and a
 * stale display list from the frame in flight cannot land on new content. */
static s32 snap_strip_next;
/* The bitmap array handed out by the last build, before its SObj exists:
 * its caller is between snap_build_sprite and omGObjAddSprite, so no sweep
 * may reclaim it. One deep -- no call site holds two unattached strips, and
 * none may be added. */
static Bitmap* snap_strip_pending;
/* Occupancy, for the peak the host prints. */
static s32 snap_strip_live;

/* Refused strips and the high-water occupancy, in the mailbox hole above
 * the CONTROLS bank and below SCRATCH_ARRAYS at +0x100. Until 1.0.6 these
 * sat on +0x70 and +0x74, the same words the Exit Game item's GObj and
 * help-line pointers were later given (SCRATCH_EXIT_GOBJ, SCRATCH_HELP_EXIT
 * below): the peak never overwrote a pointer only because a pointer is
 * larger than any count, and a refused strip would have bumped the item's
 * pointer by one. Nothing on the host reads them. */
#define MBOX_POOL_FAIL (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x7C))
#define MBOX_POOL_PEAK (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x80))

/* Which slot a Bitmap array is the head of, or -1 for anything else.
 * The divide names a candidate; the compare against that slot's own address
 * is what decides, so this stays exact whatever the stride works out to be
 * and a pointer into the middle of a slot is never taken for one. */
static s32 snap_strip_slot_of(Bitmap* bm) {
    u32 base;
    u32 off;
    s32 slot;

    if (bm == NULL) {
        return -1;
    }
    base = (u32) &snap_strip_pool[0].bitmap[0];
    if ((u32) bm < base) {
        return -1;
    }
    off = (u32) bm - base;
    slot = (s32) (off / sizeof(SnapStripSlot));
    if (slot >= SNAP_STRIP_SLOTS) {
        return -1;
    }
    if (bm != &snap_strip_pool[slot].bitmap[0]) {
        return -1;
    }
    return slot;
}

/* Frees every slot nothing on screen points at.
 *
 * Liveness is read off the one thing that actually matters. omGObjAddSprite
 * copies the whole Sprite into the SObj (sobj->sprite = *sprite, 0x44
 * bytes), so the Sprite half of a slot is dead the moment that call returns
 * -- but the copy's sprite.bitmap still points into the slot, and the sprite
 * library re-reads that array every time the strip is drawn. So a slot is
 * live exactly while some live SObj names it, and that is what this walks.
 *
 * The walk is complete. omAddGObj links every GObj into omGObjListHead[link]
 * at creation and omDeleteGObj frees the SObjs (ohRemoveSprite) BEFORE it
 * unlinks the GObj, so there is no window in which a live SObj hangs off a
 * GObj this loop cannot reach -- including the deferred case where
 * omDeleteGObj refuses to delete omCurrentObject and returns with the object
 * still listed, which this conservatively keeps. Nothing else can hold a
 * slot: a freed SObj went onto omFreeSObjList, which this never visits, and
 * omFreeSObj overwrites only its nextFree word, so its stale sprite.bitmap
 * is exactly the dangling reference this design must not trust.
 *
 * That is the whole guarantee the requirement asks for: a slot is reused
 * only when no SObj points at it, because "no SObj points at it" is the
 * test. */
static void snap_strip_sweep(void) {
    s32 i;
    s32 slot;

    for (i = 0; i < SNAP_STRIP_SLOTS; i++) {
        snap_strip_used[i] = 0;
    }
    for (i = 0; i < 32; i++) {
        GObj* obj = omGObjListHead[i];
        while (obj != NULL) {
            if (obj->type == 2) {
                SObj* sobj = obj->data.sobj;
                while (sobj != NULL) {
                    slot = snap_strip_slot_of(sobj->sprite.bitmap);
                    if (slot >= 0) {
                        snap_strip_used[slot] = 1;
                    }
                    sobj = sobj->next;
                }
            }
            obj = obj->next;
        }
    }
    slot = snap_strip_slot_of(snap_strip_pending);
    if (slot >= 0) {
        snap_strip_used[slot] = 1;
    }

    snap_strip_live = 0;
    for (i = 0; i < SNAP_STRIP_SLOTS; i++) {
        if (snap_strip_used[i]) {
            snap_strip_live++;
        }
    }
    /* The high-water mark is taken here and nowhere else. Counted as slots
     * are handed out it would only ever reach the table size, because
     * nothing is reclaimed until the table is full; counted after a sweep it
     * is the number of strips genuinely reachable at once, which is what
     * says whether the table is big enough. */
    if ((u32) snap_strip_live > MBOX_POOL_PEAK) {
        MBOX_POOL_PEAK = (u32) snap_strip_live;
    }
}

/* A free slot, sweeping once if the table looks full. Returns NULL rather
 * than failing: every call site already treats NULL as "this strip does not
 * appear", so the worst a dry pool can do is leave a row blank, and the
 * count it bumps says so in the log. */
static SnapStripSlot* snap_strip_take(void) {
    s32 pass;
    s32 tries;
    s32 i;

    for (pass = 0; pass < 2; pass++) {
        for (tries = 0; tries < SNAP_STRIP_SLOTS; tries++) {
            i = snap_strip_next;
            snap_strip_next = i + 1;
            if (snap_strip_next >= SNAP_STRIP_SLOTS) {
                snap_strip_next = 0;
            }
            if (!snap_strip_used[i]) {
                snap_strip_used[i] = 1;
                snap_strip_pending = &snap_strip_pool[i].bitmap[0];
                snap_strip_live++;
                return &snap_strip_pool[i];
            }
        }
        if (pass == 0) {
            snap_strip_sweep();
        }
    }
    MBOX_POOL_FAIL = MBOX_POOL_FAIL + 1;
    return NULL;
}

static Sprite* snap_build_sprite(s32 id, s32 x, s32 y, u8 fmt, u8 siz) {
    SnapStripSlot* slot;
    Sprite* sp;
    Bitmap* bm;
    s32 w, h, chunks, i;
    s32 texelBytes;
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
    /* Clamped to the slot, as the swap path is: both paths carry the same
     * six now, so the two can no longer disagree about how much of a wide
     * string is drawn. Nothing staged today is past three (the widest are
     * the help lines at four), and the stager warns at stage time if one
     * ever spills past what the pages draw. */
    if (chunks > SNAP_STRIP_CHUNKS) {
        chunks = SNAP_STRIP_CHUNKS;
    }

    /* No heap. A free slot, or nothing -- and nothing is a strip that does
     * not appear, which every caller already handles. */
    slot = snap_strip_take();
    if (slot == NULL) {
        return NULL;
    }
    sp = &slot->sprite;
    bm = &slot->bitmap[0];

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
    sp->ndisplist = 24 + 12 * SNAP_STRIP_CHUNKS;
    sp->bmheight = h;
    sp->bmHreal = h;
    sp->bmfmt = fmt;
    sp->bmsiz = siz;
    sp->bitmap = bm;
    sp->rsp_dl = NULL;
    sp->rsp_dl_next = NULL;
    sp->frac_s = 0;
    sp->frac_t = 0;

    /* Every bitmap of the slot gets valid fields, including the spares a
     * wider swapped-in string will use. A block is 64 texels wide by the
     * strip's height, its texels two bytes or, for the legend, four. */
    texelBytes = (siz == G_IM_SIZ_32b) ? 4 : 2;
    for (i = 0; i < SNAP_STRIP_CHUNKS; i++) {
        bm[i].width = 64;
        bm[i].width_img = 64;
        bm[i].s = 0;
        bm[i].t = 0;
        bm[i].buf = (void*) (pixels + ((i < chunks) ? i : 0) * 64 * h * texelBytes);
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
    if (chunks > SNAP_STRIP_CHUNKS) {
        chunks = SNAP_STRIP_CHUNKS;
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
        snap_sprite_rgb(gobj->data.sobj, r, g, b);
    }
}

/* Collects the six item labels in display order: the five stock label
 * sprites plus the staged "Graphics" label the screen loop created (whose
 * GObj is remembered in scratch). Display order: Screen, Graphics, Sound,
 * Z Button, Control Stick, Return. */
#define SCRATCH_GRAPHICS_GOBJ (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x18))
#define SCRATCH_HELP_ITEM     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x1C))
/* The CONTROLS item's label and help line, in the hole above SCRATCH_ARRAYS
 * (+0x50 is the title's, +0x60 the CONTROLS bank's sequence word). */
#define SCRATCH_CONTROLS_GOBJ (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x54))
#define SCRATCH_HELP_CONTROLS (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x58))
#define SCRATCH_EXIT_GOBJ     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x70))
#define SCRATCH_HELP_EXIT     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x74))
#define SCRATCH_HELP_EXIT2    (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x78))
#define SCRATCH_MODS_GOBJ     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x5C))
#define SCRATCH_HELP_MODS     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x84))
#define MBOX_QUIT_REQ         (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0x3A))
/* The CONTROLS bank of the mailbox: its sequence word and four value bytes
 * (mouse aim, the speed index, the zoom index, the tilt), seeded and read
 * by the host (src/menu_assets.cpp). */
#define CTL_SEQ      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x60))
#define CTL_FIELD(i) (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x64 + (i)))
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
        default: return 15;   /* Jynx Recolor */
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
    if (snap_page_ctx != 0) {
        return NULL;   /* the overlay these live in is not loaded in a course */
    }
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
        case 4:  return 8;   /* Frame Rate: Original, Display, then 60, 90,
                              * 120, 144, 165 and 240 held by interpolation
                              * (the Manual mode with that target) */
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
        case 4: return (v == 0) ? STR_ORIGINAL
                     : (v == 1) ? STR_DISPLAY
                     : (v == 2) ? (STR_VOL0 + 6)      /* 60 */
                     : (v == 3) ? (STR_VOL0 + 9)      /* 90 */
                     : (STR_FPS120 + (v - 4));         /* 120, 144, 165, 240 */
        case 5: return (v == 0) ? STR_CLASSIC : (v == 1) ? STR_AUTO : STR_SHARP;
        case 6: return (v == 0) ? STR_POINT : (v == 1) ? STR_SMOOTH : STR_CRISP;
        case 7: return (v == 0) ? STR_AUTHENTIC : STR_SMOOTH;
        case 8: return (v == 0) ? STR_AUTO : (v == 1) ? STR_STANDARD : STR_HIGH;
        case 9: return (v == 0) ? STR_DOUBLE : STR_TRIPLE;
        default: return v ? STR_ON : STR_OFF;  /* Widescreen, Dither, Fullscreen,
                                                * Overscan Crop, Cutscene Fix,
                                                * Photo Detail, Jynx Recolor */
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

/* Fills LIST_LABEL() with the item labels in display order: Screen,
 * Graphics, Sound, Controls, Mods (on the Return row, while its strip is
 * staged; Return otherwise), Exit Game, and returns how many there are:
 * six with the Exit Game strip, five without it. The stock chain holds
 * Screen, Sound, Z Button, Control Stick and Return; the middle two are
 * hidden (they are rows of the CONTROLS page now) and the staged strips
 * take their places. */
static s32 snap_option_labels(void) {
    SObj* sobj = D_800E8340_A0F8D0->data.sobj;
    SObj* stock[5];
    s32 n = 0;
    GObj* graphics = (GObj*) SCRATCH_GRAPHICS_GOBJ;
    GObj* controls = (GObj*) SCRATCH_CONTROLS_GOBJ;
    GObj* exitItem = (GObj*) SCRATCH_EXIT_GOBJ;
    GObj* mods = (GObj*) SCRATCH_MODS_GOBJ;

    while ((sobj != NULL) && (n < 5)) {
        stock[n] = sobj;
        n++;
        sobj = sobj->next;
    }
    while (n < 5) {
        stock[n] = stock[0];
        n++;
    }
    LIST_LABEL(0) = (u32) stock[0];
    LIST_LABEL(OPT_GRAPHICS) =
        ((graphics != NULL) && (graphics->data.sobj != NULL)) ? (u32) graphics->data.sobj : (u32) stock[0];
    LIST_LABEL(OPT_SOUND) = (u32) stock[1];
    LIST_LABEL(OPT_CONTROLS) =
        ((controls != NULL) && (controls->data.sobj != NULL)) ? (u32) controls->data.sobj : (u32) stock[0];
    LIST_LABEL(OPT_RETURN) =
        ((mods != NULL) && (mods->data.sobj != NULL)) ? (u32) mods->data.sobj : (u32) stock[4];
    if ((exitItem != NULL) && (exitItem->data.sobj != NULL)) {
        LIST_LABEL(OPT_EXIT) = (u32) exitItem->data.sobj;
        return OPT_ITEMS;
    }
    LIST_LABEL(OPT_EXIT) = (u32) stock[4];
    return OPT_ITEMS - 1;
}

/* The GRAPHICS page: the Option screen's own dress -- island background,
 * header and rules kept, the item rows hidden, eight settings in the same
 * label-and-<value> style, the help box explaining the controls. */
static void snap_graphics_page(void) {
    UnkStruct800BEDF8* input;
    s32 navUp;
    s32 navDown;
    s32 navLeft;
    s32 navRight;
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
    for (i = 0; (i < 12) && (snap_page_ctx == 0); i++) {
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
        /* Both staged rows have to go, not just this page's own: the Option
         * list's CONTROLS label is the patch's too, it is not in any of the
         * chains the loop above walks, and left behind it drew on top of the
         * fourth row here (a Deck found it on the Graphics and Sound pages,
         * 2026-09-06). */
        mine = (GObj*) SCRATCH_CONTROLS_GOBJ;
        if ((mine != NULL) && (mine->data.sobj != NULL) &&
            !(mine->data.sobj->sprite.attr & SP_HIDDEN) && (hiddenCount < 64)) {
            mine->data.sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) mine->data.sobj;
            hiddenCount++;
        }
        /* And the sixth row, Exit Game, the patch's own as well: left
         * behind, it stayed on screen under every page (a Deck found it
         * on the Graphics page, 2026-09-11). */
        mine = (GObj*) SCRATCH_EXIT_GOBJ;
        if ((mine != NULL) && (mine->data.sobj != NULL) &&
            !(mine->data.sobj->sprite.attr & SP_HIDDEN) && (hiddenCount < 64)) {
            mine->data.sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) mine->data.sobj;
            hiddenCount++;
        }
        /* And the MODS item on the Return row, the patch's as well: left
         * behind, it drew over the Frame Rate row (2026-09-17). */
        mine = (GObj*) SCRATCH_MODS_GOBJ;
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
    if (snap_page_ctx == 0) {
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
        snap_vr_header();
        {
            s32 pointed=snap_vr_row(sel, top, PAGE_VISIBLE, PAGE_ITEMS);
            if(pointed!=sel) {
                snap_tint((GObj*)PAGE_LABEL(sel),0xFF,0xFF,0xFF);
                sel=pointed; pulseState=0;
                snap_swap_strip(descStrip,snap_row_desc(sel));
            }
        }
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
            snap_ui_sound(SND_BACK);
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

        if ((gContInputPressedButtons & A_BUTTON) || snap_start_closes()) {
            /* A accepts what is on screen and leaves, matching the SOUND
             * page. A used to cycle the selected row's value instead --
             * which silently edited AND saved a setting on the button
             * everyone presses to mean "yes": that is how a player's 2D
             * Detail ended up on Classic without them knowing, reported as
             * a pixelation bug. Edits belong to the stick alone. */
            snap_ui_sound(SND_OK);
            break;
        }

        {
            s32 sx = gContInputStickX;
            s32 sy = gContInputStickY;
            s32 mx = (sx < 0) ? -sx : sx;
            s32 my = (sy < 0) ? -sy : sy;
            s32 dirV = 0;
            s32 dirH = 0;
            navUp = navDown = navLeft = navRight = 0;
            if ((my >= 24) && (my >= mx)) {
                dirV = (sy > 0) ? 1 : -1;
            }
            else if ((mx >= 40) && (mx > 2 * my)) {
                dirH = (sx > 0) ? 1 : -1;
            }
            if (dirV != snap_nav_dir_v) {
                snap_nav_dir_v = dirV;
                snap_nav_repeat_v = 15;
                if (dirV > 0) {
                    navUp = 1;
                }
                else if (dirV < 0) {
                    navDown = 1;
                }
            }
            else if ((dirV != 0) && (--snap_nav_repeat_v <= 0)) {
                snap_nav_repeat_v = 6;
                if (dirV > 0) {
                    navUp = 1;
                }
                else {
                    navDown = 1;
                }
            }
            if (dirH != snap_nav_dir_h) {
                snap_nav_dir_h = dirH;
                snap_nav_repeat_h = 20;
                if (dirH > 0) {
                    navRight = 1;
                }
                else if (dirH < 0) {
                    navLeft = 1;
                }
            }
            else if ((dirH != 0) && (--snap_nav_repeat_h <= 0)) {
                snap_nav_repeat_h = 12;
                if (dirH > 0) {
                    navRight = 1;
                }
                else {
                    navLeft = 1;
                }
            }
        }

        if (navUp) {
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
            snap_ui_sound(SND_MOVE);
        }
        else if (navDown) {
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
            snap_ui_sound(SND_MOVE);
        }
        else if (navRight) {
            field = snap_row_field(sel);
            v = MBOX_FIELD(field) + 1;
            if (v >= snap_value_count(sel)) {
                v = 0;
            }
            MBOX_FIELD(field) = v;
            moved = 1;
        }
        else if (navLeft) {
            field = snap_row_field(sel);
            v = MBOX_FIELD(field) - 1;
            if (v < 0) {
                v = snap_value_count(sel) - 1;
            }
            MBOX_FIELD(field) = v;
            moved = 1;
        }

        if (moved) {
            snap_ui_sound(SND_MOVE);
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
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        snap_sprite_gray(sobj, 0x80);
                        pulseState = 1;
                    }
                    break;
                case 1:
                    if (sobj->sprite.red < 0xE2) {
                        sobj->sprite.red += 0x1E;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        pulseCounter = 0;
                        snap_sprite_gray(sobj, 0xFF);
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
    GObj* helpControlsObj;
    GObj* helpExitObj;
    GObj* helpExitAskObj;
    GObj* helpModsObj;
    s32 helpCount;
    s32 nItems;
    s32 armed;
    s32 i;
    s8 sel;
    s8 shownHelp;
    u8 pulseState;
    u8 pulseCounter;

    nItems = snap_option_labels();
    armed = 0;

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
    helpControlsObj = (GObj*) SCRATCH_HELP_CONTROLS;
    if ((helpControlsObj != NULL) && (helpControlsObj->data.sobj != NULL)) {
        helpControlsObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    helpExitObj = (GObj*) SCRATCH_HELP_EXIT;
    if ((helpExitObj != NULL) && (helpExitObj->data.sobj != NULL)) {
        helpExitObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    helpExitAskObj = (GObj*) SCRATCH_HELP_EXIT2;
    if ((helpExitAskObj != NULL) && (helpExitAskObj->data.sobj != NULL)) {
        helpExitAskObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    helpModsObj = (GObj*) SCRATCH_HELP_MODS;
    if ((helpModsObj != NULL) && (helpModsObj->data.sobj != NULL)) {
        helpModsObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }

    pulseState = 0;
    pulseCounter = 0;
    shownHelp = -1;
    ohWait(1);

    while (1) {
        temp_v0_2 = func_800AA38C(0);
        snap_vr_header();
        for(i=0;i<nItems;i++) {
            if(snap_vr_sprite((SObj*)LIST_LABEL(i)) && i!=MBOX_SEL) {
                snap_sprite_gray((SObj*)LIST_LABEL(MBOX_SEL),0xFF);
                MBOX_SEL=i; pulseState=0; armed=0;
            }
        }
        if (gContInputPressedButtons & A_BUTTON) {
            snap_ui_sound(SND_OK);
            if ((MBOX_SEL == OPT_EXIT) && !armed) {
                /* Exit Game asks first: the help line becomes the question
                 * and the next A answers it. B, or moving off the row,
                 * withdraws it. One press must not close the program. */
                armed = 1;
                shownHelp = -1;
                ohWait(1);
                continue;
            }
            pressedB = 0;
            break;
        } else if (gContInputPressedButtons & B_BUTTON) {
            snap_ui_sound(SND_BACK);
            if (armed) {
                armed = 0;
                shownHelp = -1;
                ohWait(1);
                continue;
            }
            pressedB = 1;
            break;
        } else {
            sel = MBOX_SEL;
            if (temp_v0_2->pressedButtons & STICK_SLOW_UP) {
                snap_ui_sound(SND_MOVE);
                snap_sprite_gray((SObj*) LIST_LABEL(sel), 0xFF);
                sel--;
                if (sel < 0) {
                    sel = nItems - 1;
                }
                MBOX_SEL = sel;
                pulseState = 0;
                armed = 0;
            } else if (temp_v0_2->pressedButtons & STICK_SLOW_DOWN) {
                snap_ui_sound(SND_MOVE);
                snap_sprite_gray((SObj*) LIST_LABEL(sel), 0xFF);
                sel++;
                sel %= nItems;
                MBOX_SEL = sel;
                pulseState = 0;
                armed = 0;
            }

            /* The help line follows the selection: the staged strips for
             * Graphics and Controls, the stock sprites for the rest. The
             * stock help sprites run Screen, Sound, Z Button, Control
             * Stick, Return. */
            sel = MBOX_SEL;
            if (sel != shownHelp) {
                for (i = 0; i < helpCount; i++) {
                    ((SObj*) LIST_HELP(i))->sprite.attr |= SP_HIDDEN;
                }
                if ((helpItemObj != NULL) && (helpItemObj->data.sobj != NULL)) {
                    helpItemObj->data.sobj->sprite.attr |= SP_HIDDEN;
                }
                if ((helpControlsObj != NULL) && (helpControlsObj->data.sobj != NULL)) {
                    helpControlsObj->data.sobj->sprite.attr |= SP_HIDDEN;
                }
                if ((helpExitObj != NULL) && (helpExitObj->data.sobj != NULL)) {
                    helpExitObj->data.sobj->sprite.attr |= SP_HIDDEN;
                }
                if ((helpExitAskObj != NULL) && (helpExitAskObj->data.sobj != NULL)) {
                    helpExitAskObj->data.sobj->sprite.attr |= SP_HIDDEN;
                }
                if ((helpModsObj != NULL) && (helpModsObj->data.sobj != NULL)) {
                    helpModsObj->data.sobj->sprite.attr |= SP_HIDDEN;
                }
                if (sel == OPT_GRAPHICS) {
                    if ((helpItemObj != NULL) && (helpItemObj->data.sobj != NULL)) {
                        helpItemObj->data.sobj->sprite.attr &= ~SP_HIDDEN;
                    }
                }
                else if (sel == OPT_CONTROLS) {
                    if ((helpControlsObj != NULL) && (helpControlsObj->data.sobj != NULL)) {
                        helpControlsObj->data.sobj->sprite.attr &= ~SP_HIDDEN;
                    }
                }
                else if (sel == OPT_EXIT) {
                    GObj* line = armed ? helpExitAskObj : helpExitObj;
                    if ((line != NULL) && (line->data.sobj != NULL)) {
                        line->data.sobj->sprite.attr &= ~SP_HIDDEN;
                    }
                }
                else if ((sel == OPT_RETURN) && (helpModsObj != NULL) && (helpModsObj->data.sobj != NULL)) {
                    /* The MODS item on the Return row: its own line. */
                    helpModsObj->data.sobj->sprite.attr &= ~SP_HIDDEN;
                }
                else {
                    i = (sel == 0) ? 0 : (sel == OPT_SOUND) ? 1 : 4;
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
                            snap_sprite_gray(sobj, sobj->sprite.red);
                        } else {
                            snap_sprite_gray(sobj, 0x80);
                            pulseState = 1;
                        }
                        break;
                    case 1:
                        if (sobj->sprite.red < 0xE2) {
                            sobj->sprite.red += 0x1E;
                            snap_sprite_gray(sobj, sobj->sprite.red);
                        } else {
                            pulseCounter = 0;
                            snap_sprite_gray(sobj, 0xFF);
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
    if ((helpControlsObj != NULL) && (helpControlsObj->data.sobj != NULL)) {
        helpControlsObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    if ((helpExitObj != NULL) && (helpExitObj->data.sobj != NULL)) {
        helpExitObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    if ((helpExitAskObj != NULL) && (helpExitAskObj->data.sobj != NULL)) {
        helpExitAskObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    if ((helpModsObj != NULL) && (helpModsObj->data.sobj != NULL)) {
        helpModsObj->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    snap_sprite_gray((SObj*) LIST_LABEL(MBOX_SEL), 0xFF);
    ohWait(1);
    snap_sprite_rgb((SObj*) LIST_LABEL(MBOX_SEL), 0xFF, 0x82, 0x41);
    ohWait(1);
    if (!pressedB) {
        if ((MBOX_SEL == OPT_RETURN) && (SCRATCH_MODS_GOBJ != 0)) {
            return OPT_MODS;   /* A on the MODS item, which stands on the Return row */
        }
        return MBOX_SEL;
    }
    return OPT_RETURN;   /* B backs out through Return */
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

/* The Stereo/Mono save flag (PFID_9, zero for Stereo): the Option screen's
 * exit path writes the overlay's mirror into it; from a course the flag is
 * written now. */
static void snap_stereo_flag_set(s32 v) {
    if (snap_page_ctx != 0) {
        setPlayerFlag(PFID_9, v);
    } else {
        D_800E8394_A0F924 = v;
    }
}

static void snap_sound_page(void) {
    UnkStruct800BEDF8* input;
    s32 navUp;
    s32 navDown;
    s32 navLeft;
    s32 navRight;
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
    for (i = 0; (i < 12) && (snap_page_ctx == 0); i++) {
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
        /* Both staged rows have to go, not just this page's own: the Option
         * list's CONTROLS label is the patch's too, it is not in any of the
         * chains the loop above walks, and left behind it drew on top of the
         * fourth row here (a Deck found it on the Graphics and Sound pages,
         * 2026-09-06). */
        mine = (GObj*) SCRATCH_CONTROLS_GOBJ;
        if ((mine != NULL) && (mine->data.sobj != NULL) &&
            !(mine->data.sobj->sprite.attr & SP_HIDDEN) && (hiddenCount < 64)) {
            mine->data.sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) mine->data.sobj;
            hiddenCount++;
        }
        /* And the sixth row, Exit Game, the patch's own as well: left
         * behind, it stayed on screen under every page (a Deck found it
         * on the Graphics page, 2026-09-11). */
        mine = (GObj*) SCRATCH_EXIT_GOBJ;
        if ((mine != NULL) && (mine->data.sobj != NULL) &&
            !(mine->data.sobj->sprite.attr & SP_HIDDEN) && (hiddenCount < 64)) {
            mine->data.sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) mine->data.sobj;
            hiddenCount++;
        }
        /* And the MODS item on the Return row, the patch's as well: left
         * behind, it drew over the Frame Rate row (2026-09-17). */
        mine = (GObj*) SCRATCH_MODS_GOBJ;
        if ((mine != NULL) && (mine->data.sobj != NULL) &&
            !(mine->data.sobj->sprite.attr & SP_HIDDEN) && (hiddenCount < 64)) {
            mine->data.sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) mine->data.sobj;
            hiddenCount++;
        }
    }
    if (snap_page_ctx == 0) {
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
        snap_vr_header();
        {
            s32 pointed=snap_vr_row(sel, 0, 6, 6);
            if(pointed!=sel) {
                snap_tint((GObj*)PAGE_LABEL(sel),0xFF,0xFF,0xFF);
                sel=pointed; pulseState=0;
                snap_swap_strip(descStrip,STR_SND_DESC + sel);
            }
        }
        moved = 0;

        if (gContInputPressedButtons & B_BUTTON) {
            snap_ui_sound(SND_BACK);
            for (i = 0; i < 6; i++) {
                SND_FIELD(i) = entryFields[i];
            }
            SND_SEQ = SND_SEQ + 1;
            snap_apply_music_volume();
            auSetSoundQuality(entryFields[4]);
            snap_stereo_flag_set(entryFields[4] ? 0 : 1);
            break;
        }

        if ((gContInputPressedButtons & A_BUTTON) || snap_start_closes()) {
            /* A accepts what is on screen and leaves, as the header says. */
            snap_ui_sound(SND_OK);
            break;
        }

        {
            s32 sx = gContInputStickX;
            s32 sy = gContInputStickY;
            s32 mx = (sx < 0) ? -sx : sx;
            s32 my = (sy < 0) ? -sy : sy;
            s32 dirV = 0;
            s32 dirH = 0;
            navUp = navDown = navLeft = navRight = 0;
            if ((my >= 24) && (my >= mx)) {
                dirV = (sy > 0) ? 1 : -1;
            }
            else if ((mx >= 40) && (mx > 2 * my)) {
                dirH = (sx > 0) ? 1 : -1;
            }
            if (dirV != snap_nav_dir_v) {
                snap_nav_dir_v = dirV;
                snap_nav_repeat_v = 15;
                if (dirV > 0) {
                    navUp = 1;
                }
                else if (dirV < 0) {
                    navDown = 1;
                }
            }
            else if ((dirV != 0) && (--snap_nav_repeat_v <= 0)) {
                snap_nav_repeat_v = 6;
                if (dirV > 0) {
                    navUp = 1;
                }
                else {
                    navDown = 1;
                }
            }
            if (dirH != snap_nav_dir_h) {
                snap_nav_dir_h = dirH;
                snap_nav_repeat_h = 20;
                if (dirH > 0) {
                    navRight = 1;
                }
                else if (dirH < 0) {
                    navLeft = 1;
                }
            }
            else if ((dirH != 0) && (--snap_nav_repeat_h <= 0)) {
                snap_nav_repeat_h = 12;
                if (dirH > 0) {
                    navRight = 1;
                }
                else {
                    navLeft = 1;
                }
            }
        }

        if (navUp) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel == 0) ? 5 : (sel - 1);
            pulseState = 0;
            snap_swap_strip(descStrip, STR_SND_DESC + sel);
            snap_ui_sound(SND_MOVE);
        }
        else if (navDown) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel + 1) % 6;
            pulseState = 0;
            snap_swap_strip(descStrip, STR_SND_DESC + sel);
            snap_ui_sound(SND_MOVE);
        }
        else if (navRight) {
            v = (sel < 4) ? (SND_FIELD(sel) / 10) : SND_FIELD(sel);
            v = (v + 1) % snap_snd_value_count(sel);
            SND_FIELD(sel) = (sel < 4) ? (v * 10) : v;
            moved = 1;
        }
        else if (navLeft) {
            v = (sel < 4) ? (SND_FIELD(sel) / 10) : SND_FIELD(sel);
            v = (v == 0) ? (snap_snd_value_count(sel) - 1) : (v - 1);
            SND_FIELD(sel) = (sel < 4) ? (v * 10) : v;
            moved = 1;
        }

        if (moved) {
            snap_ui_sound(SND_MOVE);
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
                snap_stereo_flag_set(v ? 0 : 1);
            }
        }

        /* The stock label pulse, on the selected row. */
        if (PAGE_LABEL(sel) != 0) {
            SObj* sobj = ((GObj*) PAGE_LABEL(sel))->data.sobj;
            switch (pulseState) {
                case 0:
                    if (sobj->sprite.red >= 0x84) {
                        sobj->sprite.red -= 4;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        snap_sprite_gray(sobj, 0x80);
                        pulseState = 1;
                    }
                    break;
                case 1:
                    if (sobj->sprite.red < 0xE2) {
                        sobj->sprite.red += 0x1E;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        pulseCounter = 0;
                        snap_sprite_gray(sobj, 0xFF);
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

/* The CONTROLS page: six rows in the SOUND page's dress. The first two
 * are the game's own Z Button and Control Stick settings, edited in the
 * screen's own variables exactly as the stock rows edited them (the
 * screen's exit writes them to the player flags); the other four are the
 * mouse's, in the mailbox's CONTROLS bank, applied live by the host. */
/* Thirteen rows, six on screen at a time: the page scrolls for the last
 * seven the way the Graphics page scrolls, with the same edge arrows. In
 * order: the game's own Z Button and Control Stick; Button Setup, the row
 * that opens the BUTTON SETUP page, put where a player who came to change
 * the buttons sees it without scrolling, with no value (the Option list's
 * own Screen row has none) and a help line that says what A does; Pad
 * Sticks, which stick aims and which works the C buttons; Dead Zone, how
 * far the aiming stick moves before the game sees it; Fast Forward and
 * Slow Motion, how much faster or slower the game runs while their keys
 * are held; then the mouse and gyro dials. The settings are numbered in
 * the order the strings and the CONTROLS bank were laid out in (the six
 * mouse and gyro settings first, Pad Sticks, Dead Zone, Fast Forward and
 * Slow Motion last as the seventh to tenth fields), and snap_ctl_setting
 * maps a row to its setting. */
#define CTL_ROWS 13
#define CTL_VISIBLE 6
#define CTL_ROW_BUTTONS 2
#define CTL_ROW_STICKS 3
#define CTL_ROW_DEADZONE 4
#define CTL_ROW_FAST 5
#define CTL_ROW_SLOW 6
#define CTL_SETTING_STICKS 8
#define CTL_SETTING_DEADZONE 9
#define CTL_SETTING_FAST 10
#define CTL_SETTING_SLOW 11

/* A row's setting: 0 and 1 the game's own, 2..7 the six mouse and gyro
 * settings (CONTROLS bank fields 0..5), 8 Pad Sticks (field 6), 9 Dead
 * Zone (field 7), 10 Fast Forward (field 8), 11 Slow Motion (field 9); -1
 * for the Button Setup row, which has none. */
static s32 snap_ctl_setting(s32 row) {
    if (row == CTL_ROW_BUTTONS) {
        return -1;
    }
    if (row == CTL_ROW_STICKS) {
        return CTL_SETTING_STICKS;
    }
    if (row == CTL_ROW_DEADZONE) {
        return CTL_SETTING_DEADZONE;
    }
    if (row == CTL_ROW_FAST) {
        return CTL_SETTING_FAST;
    }
    if (row == CTL_ROW_SLOW) {
        return CTL_SETTING_SLOW;
    }
    return (row > CTL_ROW_SLOW) ? (row - 5) : row;
}

static s32 snap_ctl_value_count(s32 row) {
    if (row == CTL_ROW_BUTTONS) {
        return 1;
    }
    switch (snap_ctl_setting(row)) {
        case 3:  return 11;   /* Mouse Speed */
        case 4:  return 4;    /* Zoom Speed */
        case 6:  return 3;    /* Gyro Aim: off, on, zoomed */
        case 7:  return 11;   /* Gyro Speed */
        case CTL_SETTING_DEADZONE: return 9;   /* 0, 5, 10 .. 40 */
        case CTL_SETTING_FAST: return 4;       /* Off, 2x, 3x, 4x */
        case CTL_SETTING_SLOW: return 3;       /* Off, 2x, 4x slower */
        default: return 2;    /* the on/off pairs, Pad Sticks among them */
    }
}

static s32 snap_ctl_label_str(s32 row) {
    s32 setting;
    if (row == CTL_ROW_BUTTONS) {
        return STR_BTN_LABEL;
    }
    setting = snap_ctl_setting(row);
    if (setting == CTL_SETTING_STICKS) {
        return STR_STICKS_LABEL;
    }
    if (setting == CTL_SETTING_DEADZONE) {
        return STR_DZ_LABEL;
    }
    if (setting == CTL_SETTING_FAST) {
        return STR_FF_LABEL;
    }
    if (setting == CTL_SETTING_SLOW) {
        return STR_SM_LABEL;
    }
    return (setting < 6) ? (STR_CTL_LABEL + setting) : (STR_GYRO_LABEL + (setting - 6));
}

static s32 snap_ctl_desc_str(s32 row) {
    s32 setting;
    if (row == CTL_ROW_BUTTONS) {
        return STR_BTN_DESC;
    }
    setting = snap_ctl_setting(row);
    if (setting == CTL_SETTING_STICKS) {
        return STR_STICKS_DESC;
    }
    if (setting == CTL_SETTING_DEADZONE) {
        return STR_DZ_DESC;
    }
    if (setting == CTL_SETTING_FAST) {
        return STR_FF_DESC;
    }
    if (setting == CTL_SETTING_SLOW) {
        return STR_SM_DESC;
    }
    return (setting < 6) ? (STR_CTL_DESC + setting) : (STR_GYRO_DESC + (setting - 6));
}

/* Rows [top, top+CTL_VISIBLE) sit at the fixed slots, the rest hide; the
 * edge arrows say which way the hidden rows lie. */
static void snap_ctl_layout(s32 top) {
    s32 i;
    for (i = 0; i < CTL_ROWS; i++) {
        GObj* label = (GObj*) PAGE_LABEL(i);
        GObj* value = (GObj*) PAGE_VALUE(i);
        const s32 shown = (i >= top) && (i < top + CTL_VISIBLE);
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
            if (top + CTL_VISIBLE < CTL_ROWS) {
                dnArrow->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                dnArrow->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
    }
}

static s32 snap_ctl_value_str(s32 row, s32 v) {
    if (row == CTL_ROW_BUTTONS) {
        return -1;   /* no value: snap_make_strip makes nothing of it */
    }
    switch (snap_ctl_setting(row)) {
        case CTL_SETTING_STICKS: return v ? STR_SWAPPED : STR_NORMAL;
        case CTL_SETTING_DEADZONE:
            /* 0, 10, 20, 30 and 40 are the volume steps; 25 a mouse speed. */
            switch (v) {
                case 0:  return STR_VOL0;
                case 1:  return STR_DZ5;
                case 2:  return STR_VOL0 + 1;
                case 3:  return STR_DZ15;
                case 4:  return STR_VOL0 + 2;
                case 5:  return STR_CTL_SPEED;
                case 6:  return STR_VOL0 + 3;
                case 7:  return STR_DZ35;
                default: return STR_VOL0 + 4;
            }
        case CTL_SETTING_FAST: return (v == 0) ? STR_OFF : (STR_1X + v);   /* Off, then 2x, 3x, 4x */
        case CTL_SETTING_SLOW: return (v == 0) ? STR_OFF : ((v == 1) ? (STR_1X + 1) : (STR_1X + 3));   /* Off, 2x, 4x */
        case 0:  return v ? STR_SWITCH : STR_HOLD;
        case 1:  return v ? STR_REVERSE : STR_NORMAL;
        case 2:  return v ? STR_ON : STR_OFF;
        case 3:  return STR_CTL_SPEED + v;
        case 4:  return STR_CTL_SPEED + v;   /* 25, 50, 75, 100: the first four steps */
        case 6:  return (v == 0) ? STR_OFF : ((v == 1) ? STR_ON : STR_ZOOMED);
        case 7:  return STR_CTL_SPEED + v;
        default: return v ? STR_REVERSE : STR_NORMAL;
    }
}

/* The game's Z Button and Control Stick settings. On the Option screen
 * they are the overlay's mirrors, which the screen's exit path writes into
 * the player flags (the dispatcher below); from a course the overlay is not
 * loaded and the flags are read and written directly, in force at once. */
s32 checkPlayerFlag(s32 pfid);   /* .more_funcs; also declared below */
static s32 snap_ctl_flag_get(s32 which) {
    if (snap_page_ctx != 0) {
        return checkPlayerFlag((which == 0) ? PFID_ZOOM_SWITCH : PFID_INVERTED_Y) ? 1 : 0;
    }
    return ((which == 0) ? D_800E8395_A0F925 : D_800E8396_A0F926) ? 1 : 0;
}

static void snap_ctl_flag_set(s32 which, s32 v) {
    if (snap_page_ctx != 0) {
        setPlayerFlag((which == 0) ? PFID_ZOOM_SWITCH : PFID_INVERTED_Y, v);
        return;
    }
    if (which == 0) {
        D_800E8395_A0F925 = (s8) v;
    } else {
        D_800E8396_A0F926 = (s8) v;
    }
}

static s32 snap_ctl_get(s32 row) {
    if (row == CTL_ROW_BUTTONS) {
        return 0;
    }
    switch (snap_ctl_setting(row)) {
        case 0:  return snap_ctl_flag_get(0);
        case 1:  return snap_ctl_flag_get(1);
        default: return CTL_FIELD(snap_ctl_setting(row) - 2);   /* Pad Sticks is field 6, Dead Zone 7, Fast Forward 8, Slow Motion 9 */
    }
}

static void snap_ctl_set(s32 row, s32 v) {
    if (row == CTL_ROW_BUTTONS) {
        return;
    }
    switch (snap_ctl_setting(row)) {
        case 0:  snap_ctl_flag_set(0, v); break;
        case 1:  snap_ctl_flag_set(1, v); break;
        default: CTL_FIELD(snap_ctl_setting(row) - 2) = (u8) v; break;
    }
}

/* Hides the Option list's rows, the port's own items on it and the stock
 * title, remembering in PAGE_HIDDEN what was visible for a page's teardown
 * to restore, and hides the two item help lines (the selection loop shows
 * the right one again). The CONTROLS and BUTTON SETUP pages take their list
 * from here; the GRAPHICS and SOUND pages do the same inline. Returns how
 * many sprites were hidden. */
static s32 snap_hide_option_list(void) {
    s32 i;
    s32 hiddenCount = 0;
    GObj* mine;
    GObj* chain;
    SObj* sobj;

    if (snap_page_ctx != 0) {
        return 0;   /* over the pause menu there is no list to hide */
    }
    for (i = 0; i < 12; i++) {
        chain = snap_chain(i);
        sobj = (chain != NULL) ? chain->data.sobj : NULL;
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
    /* The three staged items: none of them is in the chains above, and
     * one left behind draws on top of a page's rows (a Deck found the
     * CONTROLS and Exit Game items doing so, 2026-09-06 and 2026-09-11). */
    for (i = 0; i < 4; i++) {
        mine = (i == 0) ? (GObj*) SCRATCH_GRAPHICS_GOBJ
             : (i == 1) ? (GObj*) SCRATCH_CONTROLS_GOBJ
             : (i == 2) ? (GObj*) SCRATCH_MODS_GOBJ : (GObj*) SCRATCH_EXIT_GOBJ;
        if ((mine != NULL) && (mine->data.sobj != NULL) &&
            !(mine->data.sobj->sprite.attr & SP_HIDDEN) && (hiddenCount < 64)) {
            mine->data.sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) mine->data.sobj;
            hiddenCount++;
        }
    }
    /* The stock "Options" title (the y=40 sprite of its chain; the OK and
     * Cancel hints at y=41 stay) gives way to the page's own heading. */
    chain = snap_chain(2);
    sobj = (chain != NULL) ? chain->data.sobj : NULL;
    while (sobj != NULL) {
        if ((sobj->sprite.y == 40) && !(sobj->sprite.attr & SP_HIDDEN) &&
            (hiddenCount < 64)) {
            sobj->sprite.attr |= SP_HIDDEN;
            PAGE_HIDDEN(hiddenCount) = (u32) sobj;
            hiddenCount++;
        }
        sobj = sobj->next;
    }
    mine = (GObj*) SCRATCH_HELP_ITEM;
    if ((mine != NULL) && (mine->data.sobj != NULL)) {
        mine->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    mine = (GObj*) SCRATCH_HELP_CONTROLS;
    if ((mine != NULL) && (mine->data.sobj != NULL)) {
        mine->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    mine = (GObj*) SCRATCH_HELP_MODS;
    if ((mine != NULL) && (mine->data.sobj != NULL)) {
        mine->data.sobj->sprite.attr |= SP_HIDDEN;
    }
    return hiddenCount;
}

/* Returns 1 when A was pressed on the Button Setup row: the dispatcher opens
 * the BUTTON SETUP page and comes back here. 0 when the page was left. */
static s32 snap_controls_page(void) {
    UnkStruct800BEDF8* input;
    s32 navUp;
    s32 navDown;
    s32 navLeft;
    s32 navRight;
    GObj* hdrStrip;
    GObj* descStrip;
    s32 sel, i, moved, hiddenCount;
    s32 v;
    s32 top;
    s32 openButtons;
    u8 pulseState, pulseCounter, bobTick;
    u8 nudgeUp, nudgeDn;
    u8 entry[CTL_ROWS];

    if (DIR_MAGIC != 0x53474130) {
        return 0;
    }
    openButtons = 0;

    for (i = 0; i < CTL_ROWS; i++) {
        v = snap_ctl_get(i);
        if ((v < 0) || (v >= snap_ctl_value_count(i))) {
            v = 0;
            snap_ctl_set(i, 0);
        }
        entry[i] = (u8) v;
    }

    if (snap_list_hidden_carry > 0) {
        /* Back from the BUTTON SETUP page: the list is still hidden. */
        hiddenCount = snap_list_hidden_carry;
        snap_list_hidden_carry = 0;
    } else {
        hiddenCount = snap_hide_option_list();
    }
    hdrStrip = snap_make_strip(STR_CTL_HDR, 45, 41);
    {
        GObj* itemHelp = (GObj*) SCRATCH_HELP_ITEM;
        if ((itemHelp != NULL) && (itemHelp->data.sobj != NULL)) {
            itemHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
        itemHelp = (GObj*) SCRATCH_HELP_CONTROLS;
        if ((itemHelp != NULL) && (itemHelp->data.sobj != NULL)) {
            itemHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
    }
    descStrip = snap_make_strip(snap_ctl_desc_str(0), 49, 171);

    for (i = 0; i < CTL_ROWS; i++) {
        PAGE_LABEL(i) = (u32) snap_make_strip(snap_ctl_label_str(i), 50, PAGE_TOP_Y + i * PAGE_PITCH);
        PAGE_VALUE(i) = (u32) snap_make_strip(snap_ctl_value_str(i, entry[i]), 163, PAGE_TOP_Y + i * PAGE_PITCH);
        snap_tint((GObj*) PAGE_VALUE(i), SEL_R, SEL_G, SEL_B);
    }
    PAGE_ARROW_UP = (u32) snap_make_strip_fmt(STR_SCROLL_UP, ARROW_X, ARROW_UP_Y, G_IM_FMT_RGBA);
    PAGE_ARROW_DN = (u32) snap_make_strip_fmt(STR_SCROLL_DN, ARROW_X, ARROW_DN_Y, G_IM_FMT_RGBA);

    /* On the Button Setup row when the BUTTON SETUP page has just
     * closed; the help line is that row's, not the first row's the strip
     * was built for (a capture showed the Z Button's there, 2026-09-13). */
    sel = snap_ctl_reopen_row;
    snap_ctl_reopen_row = 0;
    if ((sel < 0) || (sel >= CTL_ROWS)) {
        sel = 0;
    }
    snap_swap_strip(descStrip, snap_ctl_desc_str(sel));
    top = (sel >= CTL_VISIBLE) ? (sel - (CTL_VISIBLE - 1)) : 0;
    snap_ctl_layout(top);
    pulseState = 0;
    pulseCounter = 0;
    bobTick = 0;
    nudgeUp = 0;
    nudgeDn = 0;

    ohWait(2);

    while (1) {
        input = func_800AA38C(0);
        snap_vr_header();
        {
            s32 pointed=snap_vr_row(sel, top, CTL_VISIBLE, CTL_ROWS);
            if(pointed!=sel) {
                snap_tint((GObj*)PAGE_LABEL(sel),0xFF,0xFF,0xFF);
                sel=pointed; pulseState=0;
                snap_swap_strip(descStrip,snap_ctl_desc_str(sel));
            }
        }
        moved = 0;

        if (gContInputPressedButtons & B_BUTTON) {
            /* Cancel: every row back to what it was at entry, the mouse
             * rows re-published so the host applies the old values. */
            snap_ui_sound(SND_BACK);
            for (i = 0; i < CTL_ROWS; i++) {
                if (snap_ctl_get(i) != entry[i]) {
                    snap_ctl_set(i, entry[i]);
                    if (snap_ctl_setting(i) >= 2) {
                        moved = 1;
                    }
                }
            }
            if (moved) {
                CTL_SEQ = CTL_SEQ + 1;
            }
            break;
        }

        if ((gContInputPressedButtons & A_BUTTON) || snap_start_closes()) {
            snap_ui_sound(SND_OK);
            if ((sel == CTL_ROW_BUTTONS) && !snap_close_all) {
                /* Opens the BUTTON SETUP page; the edits made here stand, as
                 * A always keeps them. */
                openButtons = 1;
                snap_ctl_reopen_row = sel;
            }
            break;
        }

        {
            s32 sx = gContInputStickX;
            s32 sy = gContInputStickY;
            s32 mx = (sx < 0) ? -sx : sx;
            s32 my = (sy < 0) ? -sy : sy;
            s32 dirV = 0;
            s32 dirH = 0;
            navUp = navDown = navLeft = navRight = 0;
            if ((my >= 24) && (my >= mx)) {
                dirV = (sy > 0) ? 1 : -1;
            }
            else if ((mx >= 40) && (mx > 2 * my)) {
                dirH = (sx > 0) ? 1 : -1;
            }
            if (dirV != snap_nav_dir_v) {
                snap_nav_dir_v = dirV;
                snap_nav_repeat_v = 15;
                if (dirV > 0) {
                    navUp = 1;
                }
                else if (dirV < 0) {
                    navDown = 1;
                }
            }
            else if ((dirV != 0) && (--snap_nav_repeat_v <= 0)) {
                snap_nav_repeat_v = 6;
                if (dirV > 0) {
                    navUp = 1;
                }
                else {
                    navDown = 1;
                }
            }
            if (dirH != snap_nav_dir_h) {
                snap_nav_dir_h = dirH;
                snap_nav_repeat_h = 20;
                if (dirH > 0) {
                    navRight = 1;
                }
                else if (dirH < 0) {
                    navLeft = 1;
                }
            }
            else if ((dirH != 0) && (--snap_nav_repeat_h <= 0)) {
                snap_nav_repeat_h = 12;
                if (dirH > 0) {
                    navRight = 1;
                }
                else {
                    navLeft = 1;
                }
            }
        }

        if (navUp) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel == 0) ? (CTL_ROWS - 1) : (sel - 1);
            pulseState = 0;
            if (sel < top) {
                top = sel;
                snap_ctl_layout(top);
                nudgeUp = 12;
            } else if (sel >= top + CTL_VISIBLE) {
                top = sel - (CTL_VISIBLE - 1);
                snap_ctl_layout(top);
                nudgeUp = 12;
            }
            snap_swap_strip(descStrip, snap_ctl_desc_str(sel));
            snap_ui_sound(SND_MOVE);
        }
        else if (navDown) {
            snap_tint((GObj*) PAGE_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel + 1) % CTL_ROWS;
            pulseState = 0;
            if (sel < top) {
                top = sel;
                snap_ctl_layout(top);
                nudgeDn = 12;
            } else if (sel >= top + CTL_VISIBLE) {
                top = sel - (CTL_VISIBLE - 1);
                snap_ctl_layout(top);
                nudgeDn = 12;
            }
            snap_swap_strip(descStrip, snap_ctl_desc_str(sel));
            snap_ui_sound(SND_MOVE);
        }
        else if (navRight && (snap_ctl_value_count(sel) > 1)) {
            v = snap_ctl_get(sel) + 1;
            if (v >= snap_ctl_value_count(sel)) {
                v = 0;
            }
            snap_ctl_set(sel, v);
            moved = 1;
        }
        else if (navLeft && (snap_ctl_value_count(sel) > 1)) {
            v = snap_ctl_get(sel) - 1;
            if (v < 0) {
                v = snap_ctl_value_count(sel) - 1;
            }
            snap_ctl_set(sel, v);
            moved = 1;
        }

        if (moved) {
            snap_ui_sound(SND_MOVE);
            snap_swap_strip((GObj*) PAGE_VALUE(sel), snap_ctl_value_str(sel, snap_ctl_get(sel)));
            if (snap_ctl_setting(sel) >= 2) {
                CTL_SEQ = CTL_SEQ + 1;
            }
        }

        if (PAGE_LABEL(sel) != 0) {
            SObj* sobj = ((GObj*) PAGE_LABEL(sel))->data.sobj;
            switch (pulseState) {
                case 0:
                    if (sobj->sprite.red >= 0x84) {
                        sobj->sprite.red -= 4;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        snap_sprite_gray(sobj, 0x80);
                        pulseState = 1;
                    }
                    break;
                case 1:
                    if (sobj->sprite.red < 0xE2) {
                        sobj->sprite.red += 0x1E;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        pulseCounter = 0;
                        snap_sprite_gray(sobj, 0xFF);
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

        /* The arrows breathe and hop exactly as the Graphics page's do
         * (the block above snap_graphics_page's wait): the eased four-phase
         * sway, and the two-pixel-further hop when the list scrolls. */
        bobTick++;
        if (nudgeUp > 0) {
            nudgeUp--;
        }
        if (nudgeDn > 0) {
            nudgeDn--;
        }
        {
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

    for (i = 0; i < CTL_ROWS; i++) {
        if (PAGE_LABEL(i) != 0) {
            omDeleteGObj((GObj*) PAGE_LABEL(i));
            PAGE_LABEL(i) = 0;
        }
        if (PAGE_VALUE(i) != 0) {
            omDeleteGObj((GObj*) PAGE_VALUE(i));
            PAGE_VALUE(i) = 0;
        }
    }
    if (PAGE_ARROW_UP != 0) {
        omDeleteGObj((GObj*) PAGE_ARROW_UP);
        PAGE_ARROW_UP = 0;
    }
    if (PAGE_ARROW_DN != 0) {
        omDeleteGObj((GObj*) PAGE_ARROW_DN);
        PAGE_ARROW_DN = 0;
    }
    if (hdrStrip != NULL) {
        omDeleteGObj(hdrStrip);
    }
    if (descStrip != NULL) {
        omDeleteGObj(descStrip);
    }
    if (openButtons) {
        /* Handed to the BUTTON SETUP page hidden as they are, and back again
         * when it returns, so the list never shows between the two. */
        snap_list_hidden_carry = hiddenCount;
    } else {
        for (i = 0; i < hiddenCount; i++) {
            SObj* sobj = (SObj*) PAGE_HIDDEN(i);
            sobj->sprite.attr &= ~SP_HIDDEN;
        }
    }
    ohWait(1);
    return openButtons;
}

/* =========================================================================
 * The BUTTON SETUP page: what presses each of the game's inputs.
 *
 * Twenty rows in the CONTROLS page's dress, six on screen: a Device row
 * (Keyboard, Mouse, Controller; Left and Right pick it), the eighteen
 * inputs -- the fourteen buttons and the four stick directions -- each
 * with a help line saying what it does in the game, and Restore Defaults
 * last. An input row's value is what presses it on the device shown,
 * composed live by the host from the binding table (src/input.cpp,
 * input_bind_display) into a bank of staged ids; the host turns the bank
 * with BIND_GEN and the page swaps its strips to the bank named, never
 * reading a strip the host is writing. A on an input row asks the host to
 * listen for the next press of that device (BIND_REQ, operation 1) and
 * waits for the answer while the value blinks and the help line says
 * what to do; the host hands the game no input while it listens, and none
 * until every key and button is let go after, so neither the press nor the
 * key just bound can reach this loop as a button. Z clears the row on the
 * device shown (operation 2), refused when nothing else would press the
 * input. A on Restore Defaults asks, and a second A puts the shipped
 * sources of the device shown back on every input (operation 3). Every
 * change is in force at once and reaches the settings file by the host's
 * debounced write; B leaves. There is no Cancel: what is on screen is what
 * is bound, as on the other recompilations' binding pages.
 * ========================================================================= */
#define BIND_ROWS      20
#define BIND_VISIBLE   6
#define BIND_INPUTS    18
/* Device, the eighteen inputs, then Restore Defaults last (1.0.7; 1.0.6
 * had it second, seen without scrolling). Last is where every list a
 * player knows keeps its reset, after what it resets; the row under the
 * device is the page's most looked-at slot and belongs to the first
 * binding; a quick Down-and-A no longer lands on it; and the first
 * screen shows five inputs, not four. Up from the top row wraps to it. A
 * stray A cannot fire it either way: it asks first. */
#define BIND_ROW_FIRST 1
#define BIND_ROW_RESET (BIND_ROW_FIRST + BIND_INPUTS)
#define BIND_INPUT(row) ((row) - BIND_ROW_FIRST)
#define BIND_IS_INPUT(row) (((row) >= BIND_ROW_FIRST) && ((row) < BIND_ROW_RESET))
#define BIND_REQ     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xA0))
#define BIND_ACK     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xA4))
#define BIND_GEN     (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xA8))
#define BIND_DEVICE  (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xAC))
#define BIND_OPEN    (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xAD))
#define BIND_PAD     (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xAE))
/* Twenty rows need arrays of their own: the shared PAGE_LABEL and
 * PAGE_VALUE slots hold sixteen. Past PAGE_ENTRY (+0x1C8..+0x1D7), in the
 * scratch block nothing else uses. */
#define BIND_LABEL(i) (*(volatile u32*) (SCRATCH_ARRAYS + 0x200 + (i) * 4))   /* GObj*, 20 */
#define BIND_VALUE(i) (*(volatile u32*) (SCRATCH_ARRAYS + 0x250 + (i) * 4))   /* GObj*, 20 */
#define BIND_HINT(i)  (*(volatile u32*) (SCRATCH_ARRAYS + 0x2A0 + (i) * 4))   /* SObj*, 4: the header's hints */
/* The host's answers (BIND_ACK bits 24..31). */
#define BIND_DONE      1
#define BIND_CANCELLED 2
#define BIND_JOB       3
#define BIND_KEEP      4
#define BIND_TIMEOUT   5

static s32 snap_bind_label_str(s32 row) {
    if (row == 0) {
        return STR_BIND_DEVICE;
    }
    if (row == BIND_ROW_RESET) {
        return STR_BIND_RESET;
    }
    return STR_BIND_INPUT + BIND_INPUT(row);
}

static s32 snap_bind_value_str(s32 row, s32 device, u32 gen) {
    if (row == 0) {
        return STR_BIND_KEYBOARD + device;
    }
    if (row == BIND_ROW_RESET) {
        return -1;   /* no value; its help line says what A does */
    }
    return STR_BIND_DYN + ((s32) (gen & 1)) * BIND_INPUTS + BIND_INPUT(row);
}

/* Each input row's line says what that input does in the game. */
static s32 snap_bind_desc_str(s32 row) {
    if (row == 0) {
        return STR_BIND_DESC_DEVICE;
    }
    if (row == BIND_ROW_RESET) {
        return STR_BIND_DESC_RESET;
    }
    return STR_BIND_DESC_INPUT + BIND_INPUT(row);
}

/* Rows [top, top+BIND_VISIBLE) sit at the fixed slots, the rest hide; the
 * edge arrows say which way the hidden rows lie. */
static void snap_bind_layout(s32 top) {
    s32 i;
    for (i = 0; i < BIND_ROWS; i++) {
        GObj* label = (GObj*) BIND_LABEL(i);
        GObj* value = (GObj*) BIND_VALUE(i);
        const s32 shown = (i >= top) && (i < top + BIND_VISIBLE);
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
            if (top + BIND_VISIBLE < BIND_ROWS) {
                dnArrow->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                dnArrow->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
    }
}

/* One request to the host, waited for; the value blinks while the host
 * listens for a press. The host's answer, or BIND_CANCELLED after sixteen
 * seconds of silence (the host itself stops listening after six). */
static s32 snap_bind_request(u32 req, GObj* value) {
    s32 frames;
    s32 result = BIND_CANCELLED;
    u32 ack;

    BIND_ACK = 0;
    BIND_REQ = req;
    for (frames = 0; frames < 480; frames++) {
        ack = BIND_ACK;
        if ((ack & 0xFFFFFF) == req) {
            result = (s32) (ack >> 24);
            break;
        }
        if (value != NULL) {
            if ((frames & 15) < 8) {
                snap_tint(value, 0xFF, 0xFF, 0xFF);
            } else {
                snap_tint(value, SEL_R, SEL_G, SEL_B);
            }
        }
        ohWait(1);
    }
    BIND_REQ = 0;
    if (value != NULL) {
        snap_tint(value, SEL_R, SEL_G, SEL_B);
    }
    return result;
}

static void snap_bind_page(void) {
    UnkStruct800BEDF8* input;
    s32 navUp;
    s32 navDown;
    s32 navLeft;
    s32 navRight;
    GObj* hdrStrip;
    GObj* descStrip;
    s32 sel, i, hiddenCount;
    s32 top;
    s32 device;
    s32 result;
    s32 flash;
    s32 armed;
    s32 hintCount;
    u32 gen;
    u8 pulseState, pulseCounter, bobTick;
    u8 nudgeUp, nudgeDn;

    if (DIR_MAGIC != 0x53474130) {
        return;
    }

    if (snap_list_hidden_carry > 0) {
        /* From the CONTROLS page: the list is still hidden. */
        hiddenCount = snap_list_hidden_carry;
        snap_list_hidden_carry = 0;
    } else {
        hiddenCount = snap_hide_option_list();
    }
    hdrStrip = snap_make_strip(STR_BIND_HDR, 45, 41);

    /* The header's A OK and B Cancel hints (the y=41 sprites) come down
     * on this page: A changes a row here and B goes back with every change
     * kept, and "Cancel" would promise an undo there is none of. Put back
     * at the exit, ahead of the CONTROLS page, whose B does cancel. */
    hintCount = 0;
    for (i = 0; (i < 12) && (snap_page_ctx == 0); i++) {
        GObj* chain = snap_chain(i);
        SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
        while (sobj != NULL) {
            if ((sobj->sprite.y == 41) && !(sobj->sprite.attr & SP_HIDDEN) && (hintCount < 4)) {
                sobj->sprite.attr |= SP_HIDDEN;
                BIND_HINT(hintCount) = (u32) sobj;
                hintCount++;
            }
            sobj = sobj->next;
        }
    }

    /* The device shown first: the pad when one is attached, else the
     * keyboard. The C and stick rows also name the sticks that always work
     * them under Controller (src/input.cpp, input_bind_display). The host composes the row values for it once it sees the
     * page open, and the strips are built from the bank it then names --
     * after a short wait for that turn (one tick, in practice), so the
     * first frame shows this visit's values and not the last one's. */
    device = BIND_PAD ? 2 : 0;
    BIND_DEVICE = (u8) device;
    BIND_REQ = 0;
    gen = BIND_GEN;
    BIND_OPEN = 1;
    for (i = 0; (i < 10) && (BIND_GEN == gen); i++) {
        ohWait(1);
    }
    gen = BIND_GEN;

    descStrip = snap_make_strip(snap_bind_desc_str(0), 49, 171);
    for (i = 0; i < BIND_ROWS; i++) {
        BIND_LABEL(i) = (u32) snap_make_strip(snap_bind_label_str(i), 50, PAGE_TOP_Y);
        BIND_VALUE(i) = (u32) snap_make_strip(snap_bind_value_str(i, device, gen), 163, PAGE_TOP_Y);
        snap_tint((GObj*) BIND_VALUE(i), SEL_R, SEL_G, SEL_B);
    }
    PAGE_ARROW_UP = (u32) snap_make_strip_fmt(STR_SCROLL_UP, ARROW_X, ARROW_UP_Y, G_IM_FMT_RGBA);
    PAGE_ARROW_DN = (u32) snap_make_strip_fmt(STR_SCROLL_DN, ARROW_X, ARROW_DN_Y, G_IM_FMT_RGBA);

    sel = 0;
    top = 0;
    snap_bind_layout(top);
    pulseState = 0;
    pulseCounter = 0;
    bobTick = 0;
    nudgeUp = 0;
    nudgeDn = 0;
    flash = 0;
    armed = 0;

    ohWait(2);

    while (1) {
        input = func_800AA38C(0);
        snap_vr_header();
        {
            s32 pointed=snap_vr_row(sel, top, BIND_VISIBLE, BIND_ROWS);
            if(pointed!=sel) {
                snap_tint((GObj*)BIND_LABEL(sel),0xFF,0xFF,0xFF);
                sel=pointed; pulseState=0;
                snap_swap_strip(descStrip,snap_bind_desc_str(sel)); armed=0;flash=0;
            }
        }

        /* The host turned the bank: the row values are new. */
        if (BIND_GEN != gen) {
            gen = BIND_GEN;
            for (i = BIND_ROW_FIRST; i < BIND_ROW_RESET; i++) {
                snap_swap_strip((GObj*) BIND_VALUE(i), snap_bind_value_str(i, device, gen));
            }
        }

        if (gContInputPressedButtons & B_BUTTON) {
            snap_ui_sound(SND_BACK);
            if (armed) {
                /* Restore Defaults withdrawn, the page stays. */
                armed = 0;
                snap_swap_strip(descStrip, snap_bind_desc_str(sel));
                ohWait(1);
                continue;
            }
            break;
        }

        if (gContInputPressedButtons & A_BUTTON) {
            if (BIND_IS_INPUT(sel)) {
                snap_ui_sound(SND_OK);
                snap_swap_strip(descStrip, STR_BIND_DESC_LISTEN);
                result = snap_bind_request((1u << 16) | (((u32) device) << 8) | (u32) (BIND_INPUT(sel) + 1),
                                           (GObj*) BIND_VALUE(sel));
                flash = 0;
                if (result == BIND_JOB) {
                    snap_ui_sound(SND_BACK);
                    snap_swap_strip(descStrip, STR_BIND_DESC_JOB);
                    flash = 180;
                } else if (result == BIND_TIMEOUT) {
                    snap_swap_strip(descStrip, STR_BIND_DESC_TIMEOUT);
                    flash = 180;
                } else {
                    if (result == BIND_DONE) {
                        snap_ui_sound(SND_OK);
                    }
                    snap_swap_strip(descStrip, snap_bind_desc_str(sel));
                }
                ohWait(1);
                continue;
            }
            if (sel == BIND_ROW_RESET) {
                /* Asks first, the way Exit Game asks: the help line becomes
                 * the question and the next A answers it; B or moving off
                 * the row withdraws it. Restores the device shown. */
                snap_ui_sound(SND_OK);
                if (!armed) {
                    armed = 1;
                    snap_swap_strip(descStrip, STR_BIND_DESC_CONFIRM);
                } else {
                    armed = 0;
                    snap_bind_request((3u << 16) | (((u32) device) << 8), NULL);
                    snap_swap_strip(descStrip, STR_BIND_DESC_RESET);
                }
                ohWait(1);
                continue;
            }
        }

        if ((gContInputPressedButtons & Z_TRIG) && BIND_IS_INPUT(sel)) {
            result = snap_bind_request((2u << 16) | (((u32) device) << 8) | (u32) (BIND_INPUT(sel) + 1), NULL);
            flash = 0;
            if (result == BIND_KEEP) {
                snap_ui_sound(SND_BACK);
                snap_swap_strip(descStrip, STR_BIND_DESC_KEEP);
                flash = 180;
            } else {
                snap_ui_sound(SND_MOVE);
            }
            ohWait(1);
            continue;
        }

        {
            s32 sx = gContInputStickX;
            s32 sy = gContInputStickY;
            s32 mx = (sx < 0) ? -sx : sx;
            s32 my = (sy < 0) ? -sy : sy;
            s32 dirV = 0;
            s32 dirH = 0;
            navUp = navDown = navLeft = navRight = 0;
            if ((my >= 24) && (my >= mx)) {
                dirV = (sy > 0) ? 1 : -1;
            }
            else if ((mx >= 40) && (mx > 2 * my)) {
                dirH = (sx > 0) ? 1 : -1;
            }
            if (dirV != snap_nav_dir_v) {
                snap_nav_dir_v = dirV;
                snap_nav_repeat_v = 15;
                if (dirV > 0) {
                    navUp = 1;
                }
                else if (dirV < 0) {
                    navDown = 1;
                }
            }
            else if ((dirV != 0) && (--snap_nav_repeat_v <= 0)) {
                snap_nav_repeat_v = 6;
                if (dirV > 0) {
                    navUp = 1;
                }
                else {
                    navDown = 1;
                }
            }
            if (dirH != snap_nav_dir_h) {
                snap_nav_dir_h = dirH;
                snap_nav_repeat_h = 20;
                if (dirH > 0) {
                    navRight = 1;
                }
                else if (dirH < 0) {
                    navLeft = 1;
                }
            }
            else if ((dirH != 0) && (--snap_nav_repeat_h <= 0)) {
                snap_nav_repeat_h = 12;
                if (dirH > 0) {
                    navRight = 1;
                }
                else {
                    navLeft = 1;
                }
            }
        }

        if (navUp) {
            snap_tint((GObj*) BIND_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel == 0) ? (BIND_ROWS - 1) : (sel - 1);
            pulseState = 0;
            armed = 0;
            if (sel < top) {
                top = sel;
                snap_bind_layout(top);
                nudgeUp = 12;
            } else if (sel >= top + BIND_VISIBLE) {
                top = sel - (BIND_VISIBLE - 1);
                snap_bind_layout(top);
                nudgeUp = 12;
            }
            flash = 0;
            snap_swap_strip(descStrip, snap_bind_desc_str(sel));
            snap_ui_sound(SND_MOVE);
        }
        else if (navDown) {
            snap_tint((GObj*) BIND_LABEL(sel), 0xFF, 0xFF, 0xFF);
            sel = (sel + 1) % BIND_ROWS;
            pulseState = 0;
            armed = 0;
            if (sel < top) {
                top = sel;
                snap_bind_layout(top);
                nudgeDn = 12;
            } else if (sel >= top + BIND_VISIBLE) {
                top = sel - (BIND_VISIBLE - 1);
                snap_bind_layout(top);
                nudgeDn = 12;
            }
            flash = 0;
            snap_swap_strip(descStrip, snap_bind_desc_str(sel));
            snap_ui_sound(SND_MOVE);
        }
        else if ((navRight || navLeft) && (sel == 0)) {
            /* The device: the host recomposes the values for it and turns
             * the bank, which the swap at the top of the loop follows. */
            device = navRight ? ((device + 1) % 3) : ((device + 2) % 3);
            BIND_DEVICE = (u8) device;
            snap_swap_strip((GObj*) BIND_VALUE(0), STR_BIND_KEYBOARD + device);
            snap_ui_sound(SND_MOVE);
        }

        /* A refusal's or a time-out's line stays three seconds (the Option
         * screen runs at sixty frames), then the row's returns. */
        if (flash > 0) {
            flash--;
            if (flash == 0) {
                snap_swap_strip(descStrip, snap_bind_desc_str(sel));
            }
        }

        if (BIND_LABEL(sel) != 0) {
            SObj* sobj = ((GObj*) BIND_LABEL(sel))->data.sobj;
            switch (pulseState) {
                case 0:
                    if (sobj->sprite.red >= 0x84) {
                        sobj->sprite.red -= 4;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        snap_sprite_gray(sobj, 0x80);
                        pulseState = 1;
                    }
                    break;
                case 1:
                    if (sobj->sprite.red < 0xE2) {
                        sobj->sprite.red += 0x1E;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        pulseCounter = 0;
                        snap_sprite_gray(sobj, 0xFF);
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

        bobTick++;
        if (nudgeUp > 0) {
            nudgeUp--;
        }
        if (nudgeDn > 0) {
            nudgeDn--;
        }
        {
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

    BIND_OPEN = 0;
    BIND_REQ = 0;
    for (i = 0; i < BIND_ROWS; i++) {
        if (BIND_LABEL(i) != 0) {
            omDeleteGObj((GObj*) BIND_LABEL(i));
            BIND_LABEL(i) = 0;
        }
        if (BIND_VALUE(i) != 0) {
            omDeleteGObj((GObj*) BIND_VALUE(i));
            BIND_VALUE(i) = 0;
        }
    }
    if (PAGE_ARROW_UP != 0) {
        omDeleteGObj((GObj*) PAGE_ARROW_UP);
        PAGE_ARROW_UP = 0;
    }
    if (PAGE_ARROW_DN != 0) {
        omDeleteGObj((GObj*) PAGE_ARROW_DN);
        PAGE_ARROW_DN = 0;
    }
    if (hdrStrip != NULL) {
        omDeleteGObj(hdrStrip);
    }
    if (descStrip != NULL) {
        omDeleteGObj(descStrip);
    }
    for (i = 0; i < hintCount; i++) {
        SObj* sobj = (SObj*) BIND_HINT(i);
        sobj->sprite.attr &= ~SP_HIDDEN;
    }
    /* Back to the CONTROLS page, which takes the list as it is. */
    snap_list_hidden_carry = hiddenCount;
    ohWait(1);
}

/* =========================================================================
 * The MODS page (Options > Mods).
 *
 * One row per mod in the mods folder, six on screen, the value On or Off.
 * A turns the selected mod on or off: the host records it in mods.json at
 * once (MODS_REQ, operation 1) and the row's value and help line follow. A
 * mod's code is loaded when the game starts, so the help line says the
 * change takes effect at the next start. The names and help lines are the
 * host's: it composes the six of the window the page shows (MODS_TOP) into
 * the bank MODS_GEN does not name and turns the generation, and the page
 * swaps its strips to the bank named, never reading a strip the host is
 * writing -- the BUTTON SETUP page's arrangement, with a window in place
 * of a device. B leaves; nothing is undone, and there is nothing to cancel.
 * ========================================================================= */
#define MODS_REQ      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xB0))
#define MODS_ACK      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xB4))
#define MODS_GEN      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xB8))
#define MODS_COUNT    (*(volatile u16*) (SNAP_GFX_MAILBOX + 0xBC))
#define MODS_OPEN     (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xBE))
#define MODS_TOP      (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xBF))
#define MODS_STATE(b) (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xC0 + (b)))
#define MODS_HASOPT(b) (*(volatile u8*) (SNAP_GFX_MAILBOX + 0xC2 + (b)))   /* the window's rows with options */
#define MODS_MODCOUNT (*(volatile u16*) (SNAP_GFX_MAILBOX + 0xC4))          /* the mods; the rows after are actions */
#define MODS_VISIBLE  6
#define MODS_BANK     12   /* ids per bank: six names, then six help lines */
/* The BUTTON SETUP page's arrays; the two pages are never open at once. */
#define MODS_LABEL(i) BIND_LABEL(i)
#define MODS_VALUE(i) BIND_VALUE(i)
#define MODS_HINT(i)  BIND_HINT(i)
/* The host's answers (MODS_ACK bits 24..31). */
#define MODS_ON       1
#define MODS_OFF      2
#define MODS_NONE     3
#define MODS_DONE     4

static s32 snap_mods_name_str(s32 slot, u32 gen) {
    return STR_MODS_DYN + ((s32) (gen & 1)) * MODS_BANK + slot;
}

static s32 snap_mods_help_str(s32 slot, u32 gen) {
    return STR_MODS_DYN + ((s32) (gen & 1)) * MODS_BANK + MODS_VISIBLE + slot;
}

/* The six slots hold rows top..top+5; a slot past the last row hides, a
 * value slot past the last mod hides (the action rows have none), and the
 * edge arrows say which way the rows off screen lie. `base` is the slot
 * bank: 0 for the Mods page, 6 for a mod's options page, which keeps the
 * Mods page's strips while it is up. */
static void snap_window_layout(s32 base, s32 count, s32 valueCount, s32 top) {
    s32 i;
    for (i = 0; i < MODS_VISIBLE; i++) {
        GObj* label = (GObj*) BIND_LABEL(base + i);
        GObj* value = (GObj*) BIND_VALUE(base + i);
        const s32 shown = (top + i) < count;
        const s32 valueShown = (top + i) < valueCount;
        if ((label != NULL) && (label->data.sobj != NULL)) {
            if (shown) {
                label->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                label->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
        if ((value != NULL) && (value->data.sobj != NULL)) {
            if (valueShown) {
                value->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                value->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
    }
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
            if (top + MODS_VISIBLE < count) {
                dnArrow->data.sobj->sprite.attr &= ~SP_HIDDEN;
            } else {
                dnArrow->data.sobj->sprite.attr |= SP_HIDDEN;
            }
        }
    }
}

/* The Mods page's window from the bank the generation names: the six
 * names, the six values from the bank's state byte, the selected row's
 * help line. */
static void snap_mods_swap(s32 sel, s32 top, u32 gen, GObj* descStrip) {
    const u8 state = MODS_STATE(gen & 1);
    s32 i;
    for (i = 0; i < MODS_VISIBLE; i++) {
        snap_swap_strip((GObj*) MODS_LABEL(i), snap_mods_name_str(i, gen));
        snap_swap_strip((GObj*) MODS_VALUE(i), ((state >> i) & 1) ? STR_ON : STR_OFF);
    }
    snap_swap_strip(descStrip, snap_mods_help_str(sel - top, gen));
}

/* One request to the host on a bank, waited for a second at most. */
static s32 snap_bank_request(volatile u32* req, volatile u32* ack, u32 word) {
    s32 frames;
    s32 result = MODS_NONE;
    u32 got;

    *ack = 0;
    *req = word;
    for (frames = 0; frames < 60; frames++) {
        got = *ack;
        if ((got & 0xFFFFFF) == word) {
            result = (s32) (got >> 24);
            break;
        }
        ohWait(1);
    }
    *req = 0;
    return result;
}

static s32 snap_mods_request(u32 req) {
    return snap_bank_request(&MODS_REQ, &MODS_ACK, req);
}

/* The hint at the header's right, where the Option screen's A OK and B
 * Cancel stand: which of the page's other buttons apply to the row. */
static s32 snap_mods_hint_str(s32 hasOpt, s32 canOrder) {
    if (hasOpt && canOrder) {
        return STR_MODS_HINT_BOTH;
    }
    if (hasOpt) {
        return STR_MODS_HINT_OPT;
    }
    if (canOrder) {
        return STR_MODS_HINT_ORDER;
    }
    return -1;
}

static void snap_mods_hint_show(GObj* hint, s32 sel, s32 top, u32 gen, s32 modCount) {
    s32 hasOpt = 0;
    s32 canOrder = 0;
    s32 id;
    if (sel < modCount) {
        hasOpt = (MODS_HASOPT(gen & 1) >> (sel - top)) & 1;
        canOrder = modCount > 1;
    }
    id = snap_mods_hint_str(hasOpt, canOrder);
    if ((hint == NULL) || (hint->data.sobj == NULL)) {
        return;
    }
    if (id < 0) {
        hint->data.sobj->sprite.attr |= SP_HIDDEN;
    } else {
        snap_swap_strip(hint, id);
        hint->data.sobj->sprite.attr &= ~SP_HIDDEN;
    }
}

/* The stick, four ways, with the pages' dead band, edge and repeat. */
static void snap_nav_read(s32* navUp, s32* navDown, s32* navLeft, s32* navRight) {
    s32 sy = gContInputStickY;
    s32 sx = gContInputStickX;
    s32 my = (sy < 0) ? -sy : sy;
    s32 mx = (sx < 0) ? -sx : sx;
    s32 dirV = 0;
    s32 dirH = 0;
    *navUp = *navDown = *navLeft = *navRight = 0;
    if ((my >= 24) && (my >= mx)) {
        dirV = (sy > 0) ? 1 : -1;
    }
    else if ((mx >= 40) && (mx > 2 * my)) {
        dirH = (sx > 0) ? 1 : -1;
    }
    if (dirV != snap_nav_dir_v) {
        snap_nav_dir_v = dirV;
        snap_nav_repeat_v = 15;
        if (dirV > 0) {
            *navUp = 1;
        }
        else if (dirV < 0) {
            *navDown = 1;
        }
    }
    else if ((dirV != 0) && (--snap_nav_repeat_v <= 0)) {
        snap_nav_repeat_v = 6;
        if (dirV > 0) {
            *navUp = 1;
        }
        else {
            *navDown = 1;
        }
    }
    if (dirH != snap_nav_dir_h) {
        snap_nav_dir_h = dirH;
        snap_nav_repeat_h = 20;
        if (dirH > 0) {
            *navRight = 1;
        }
        else if (dirH < 0) {
            *navLeft = 1;
        }
    }
    else if ((dirH != 0) && (--snap_nav_repeat_h <= 0)) {
        snap_nav_repeat_h = 8;
        if (dirH > 0) {
            *navRight = 1;
        }
        else {
            *navLeft = 1;
        }
    }
}

/* The stock pulse on a selected label. */
static void snap_pulse_label(GObj* gobj, u8* pulseState, u8* pulseCounter) {
    SObj* sobj;
    if ((gobj == NULL) || (gobj->data.sobj == NULL)) {
        return;
    }
    sobj = gobj->data.sobj;
    switch (*pulseState) {
        case 0:
            if (sobj->sprite.red >= 0x84) {
                sobj->sprite.red -= 4;
                snap_sprite_gray(sobj, sobj->sprite.red);
            } else {
                snap_sprite_gray(sobj, 0x80);
                *pulseState = 1;
            }
            break;
        case 1:
            if (sobj->sprite.red < 0xE2) {
                sobj->sprite.red += 0x1E;
                snap_sprite_gray(sobj, sobj->sprite.red);
            } else {
                *pulseCounter = 0;
                snap_sprite_gray(sobj, 0xFF);
                *pulseState = 2;
            }
            break;
        case 2:
            if ((*pulseCounter)++ > 30) {
                *pulseState = 0;
            }
            break;
    }
}

/* The scroll arrows' sway, and their hop when the window moves. */
static void snap_arrows_bob(u8* bobTick, u8* nudgeUp, u8* nudgeDn) {
    s16 sway, hopUp, hopDn, offUp, offDn;
    GObj* upArrow = (GObj*) PAGE_ARROW_UP;
    GObj* dnArrow = (GObj*) PAGE_ARROW_DN;
    (*bobTick)++;
    if (*nudgeUp > 0) {
        (*nudgeUp)--;
    }
    if (*nudgeDn > 0) {
        (*nudgeDn)--;
    }
    sway = (s16) ((((*bobTick >> 3) & 3) == 3) ? 1 : ((*bobTick >> 3) & 3));
    hopUp = (s16) ((*nudgeUp >= 7) ? 4 : ((*nudgeUp >= 3) ? 2 : 1));
    hopDn = (s16) ((*nudgeDn >= 7) ? 4 : ((*nudgeDn >= 3) ? 2 : 1));
    offUp = (*nudgeUp > 0) ? hopUp : sway;
    offDn = (*nudgeDn > 0) ? hopDn : sway;
    if ((upArrow != NULL) && (upArrow->data.sobj != NULL)) {
        upArrow->data.sobj->sprite.y = ARROW_UP_Y - offUp;
    }
    if ((dnArrow != NULL) && (dnArrow->data.sobj != NULL)) {
        dnArrow->data.sobj->sprite.y = ARROW_DN_Y + offDn;
    }
}

/* -------------------------------------------------------------------------
 * A mod's options page, from the Mods page's Z: the options the mod's own
 * manifest declares (the other recompilations' mod menus show the same
 * schema), one row each, label and < value >, Left and Right changing it
 * -- an Enum to its next name, a Bool on or off, a Number by its step
 * within its range; a text option is shown and left to its file. The host
 * composes the window's names, values and help lines into a bank as it
 * does for the Mods page (src/menu_assets.cpp, poll_opt_bank) and writes
 * every change to the mod's own settings file at once, the way the mod
 * menu of the other recompilations does.
 * ---------------------------------------------------------------------- */
#define OPT_REQ      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xC8))
#define OPT_ACK      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xCC))
#define OPT_GEN      (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xD0))
#define OPT_COUNT    (*(volatile u16*) (SNAP_GFX_MAILBOX + 0xD4))
#define OPT_OPEN     (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xD6))
#define OPT_TOP      (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xD7))
#define OPT_ROW      (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0xD8))
#define OPT_VISIBLE  6
#define OPT_BANK     18   /* ids per bank: six names, six values, six help lines */
#define OPT_SLOT     6    /* the strip slots after the Mods page's own six */
#define OPT_LABEL(i) BIND_LABEL(OPT_SLOT + (i))
#define OPT_VALUE(i) BIND_VALUE(OPT_SLOT + (i))

static s32 snap_opt_name_str(s32 slot, u32 gen) {
    return STR_OPT_DYN + ((s32) (gen & 1)) * OPT_BANK + slot;
}

static s32 snap_opt_value_str(s32 slot, u32 gen) {
    return STR_OPT_DYN + ((s32) (gen & 1)) * OPT_BANK + OPT_VISIBLE + slot;
}

static s32 snap_opt_help_str(s32 slot, u32 gen) {
    return STR_OPT_DYN + ((s32) (gen & 1)) * OPT_BANK + 2 * OPT_VISIBLE + slot;
}

static void snap_opt_swap(s32 sel, s32 top, u32 gen, GObj* descStrip) {
    s32 i;
    for (i = 0; i < OPT_VISIBLE; i++) {
        snap_swap_strip((GObj*) OPT_LABEL(i), snap_opt_name_str(i, gen));
        snap_swap_strip((GObj*) OPT_VALUE(i), snap_opt_value_str(i, gen));
    }
    snap_swap_strip(descStrip, snap_opt_help_str(sel - top, gen));
}

static void snap_mod_options_page(s32 modRow) {
    UnkStruct800BEDF8* input;
    GObj* hdrStrip;
    GObj* descStrip;
    s32 sel, top, count, i, result;
    s32 navUp, navDown, navLeft, navRight;
    u32 gen;
    u8 pulseState, pulseCounter, bobTick;
    u8 nudgeUp, nudgeDn;

    OPT_REQ = 0;
    OPT_ROW = (u8) modRow;
    OPT_TOP = 0;
    gen = OPT_GEN;
    OPT_OPEN = 1;
    for (i = 0; (i < 10) && (OPT_GEN == gen); i++) {
        ohWait(1);
    }
    gen = OPT_GEN;
    count = OPT_COUNT;
    if (count > 250) {
        count = 250;
    }

    hdrStrip = snap_make_strip(STR_OPT_HDR, 45, 41);
    descStrip = snap_make_strip((count > 0) ? snap_opt_help_str(0, gen) : STR_OPT_NONE, 49, 171);
    for (i = 0; i < OPT_VISIBLE; i++) {
        const s16 y = PAGE_TOP_Y + i * PAGE_PITCH;
        OPT_LABEL(i) = (u32) snap_make_strip(snap_opt_name_str(i, gen), 50, y);
        OPT_VALUE(i) = (u32) snap_make_strip(snap_opt_value_str(i, gen), 163, y);
        snap_tint((GObj*) OPT_VALUE(i), SEL_R, SEL_G, SEL_B);
    }
    sel = 0;
    top = 0;
    snap_window_layout(OPT_SLOT, count, count, top);
    pulseState = 0;
    pulseCounter = 0;
    bobTick = 0;
    nudgeUp = 0;
    nudgeDn = 0;
    snap_nav_dir_v = 0;
    snap_nav_dir_h = 0;
    ohWait(2);

    while (1) {
        input = func_800AA38C(0);
        snap_vr_header();
        {
            s32 pointed=snap_vr_row(sel, top, OPT_VISIBLE, count);
            if(pointed!=sel) {
                snap_tint((GObj*)OPT_LABEL(sel-top),0xFF,0xFF,0xFF);
                sel=pointed; pulseState=0;
                snap_swap_strip(descStrip,snap_opt_help_str(sel-top,gen));
            }
        }

        if (OPT_GEN != gen) {
            gen = OPT_GEN;
            snap_opt_swap(sel, top, gen, descStrip);
        }

        if ((gContInputPressedButtons & B_BUTTON) || snap_start_closes()) {
            snap_ui_sound(SND_BACK);
            break;
        }

        snap_nav_read(&navUp, &navDown, &navLeft, &navRight);

        if ((navLeft || navRight) && (count > 0)) {
            result = snap_bank_request(&OPT_REQ, &OPT_ACK,
                                       (6u << 16) | ((u32) (navRight ? 1 : 0xFF) << 8) | (u32) sel);
            if (result == MODS_DONE) {
                snap_ui_sound(SND_MOVE);
            }
        }

        if ((navUp || navDown) && (count > 0)) {
            snap_tint((GObj*) OPT_LABEL(sel - top), 0xFF, 0xFF, 0xFF);
            if (navUp) {
                sel = (sel == 0) ? (count - 1) : (sel - 1);
            } else {
                sel = (sel + 1) % count;
            }
            pulseState = 0;
            if (sel < top) {
                top = sel;
            } else if (sel >= top + OPT_VISIBLE) {
                top = sel - (OPT_VISIBLE - 1);
            }
            if (top != (s32) OPT_TOP) {
                OPT_TOP = (u8) top;
                snap_window_layout(OPT_SLOT, count, count, top);
                if (navUp) {
                    nudgeUp = 12;
                } else {
                    nudgeDn = 12;
                }
            }
            snap_swap_strip(descStrip, snap_opt_help_str(sel - top, gen));
            snap_ui_sound(SND_MOVE);
        }

        if (count > 0) {
            snap_pulse_label((GObj*) OPT_LABEL(sel - top), &pulseState, &pulseCounter);
        }
        snap_arrows_bob(&bobTick, &nudgeUp, &nudgeDn);
        ohWait(1);
    }

    OPT_OPEN = 0;
    OPT_REQ = 0;
    for (i = 0; i < OPT_VISIBLE; i++) {
        if (OPT_LABEL(i) != 0) {
            omDeleteGObj((GObj*) OPT_LABEL(i));
            OPT_LABEL(i) = 0;
        }
        if (OPT_VALUE(i) != 0) {
            omDeleteGObj((GObj*) OPT_VALUE(i));
            OPT_VALUE(i) = 0;
        }
    }
    if (hdrStrip != NULL) {
        omDeleteGObj(hdrStrip);
    }
    if (descStrip != NULL) {
        omDeleteGObj(descStrip);
    }
    ohWait(1);
}

/* The Mods page's own strips off while a mod's options page is up, and on
 * again after it. */
static void snap_mods_strips_show(s32 show, GObj* hdrStrip, GObj* descStrip, GObj* hintStrip,
                                  s32 count, s32 modCount, s32 top, s32 sel, u32 gen) {
    s32 i;
    if (show) {
        snap_window_layout(0, count, modCount, top);
        if (hdrStrip != NULL) {
            snap_course_gobj_show((u32) hdrStrip, 1);
        }
        if (descStrip != NULL) {
            snap_course_gobj_show((u32) descStrip, 1);
        }
        snap_mods_hint_show(hintStrip, sel, top, gen, modCount);
        return;
    }
    for (i = 0; i < MODS_VISIBLE; i++) {
        snap_course_gobj_show(MODS_LABEL(i), 0);
        snap_course_gobj_show(MODS_VALUE(i), 0);
    }
    snap_course_gobj_show((u32) hdrStrip, 0);
    snap_course_gobj_show((u32) descStrip, 0);
    snap_course_gobj_show((u32) hintStrip, 0);
}

/* -------------------------------------------------------------------------
 * The Mods page: one row per mod in the folder, six on screen, the value
 * On or Off; A, Left or Right turns the selected mod on or off (a code
 * mod's change takes effect at the next start, and the help line says so).
 * Under the mods, two rows the other recompilations' mod menus have as
 * buttons: Open the mods folder, and Restart the game. L and R move the
 * selected mod up or down the order the mods are loaded in, as their
 * menus let a mod be dragged; Z opens a mod's own options when its
 * manifest declares any. The hint at the header's right says which of
 * those apply to the row.
 * ---------------------------------------------------------------------- */
static void snap_mods_page(void) {
    UnkStruct800BEDF8* input;
    GObj* hdrStrip;
    GObj* descStrip;
    GObj* hintStrip;
    s32 sel, top, count, modCount, i, hiddenCount, hintCount, result;
    s32 navUp, navDown, navLeft, navRight;
    u32 gen;
    u8 pulseState, pulseCounter, bobTick;
    u8 nudgeUp, nudgeDn;

    if (DIR_MAGIC != 0x53474130) {
        return;
    }

    hiddenCount = snap_hide_option_list();
    hdrStrip = snap_make_strip(STR_MODS_HDR, 45, 41);

    /* The header's A OK and B Cancel hints come down, as on the BUTTON
     * SETUP page, and the page's own hint stands in their place. Put back
     * at the exit. Over a course or any other screen the legend is the
     * list's dress, taken down the same way. */
    hintCount = 0;
    for (i = 0; (i < 12) && (snap_page_ctx == 0); i++) {
        GObj* chain = snap_chain(i);
        SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
        while (sobj != NULL) {
            if ((sobj->sprite.y == 41) && !(sobj->sprite.attr & SP_HIDDEN) && (hintCount < 4)) {
                sobj->sprite.attr |= SP_HIDDEN;
                MODS_HINT(hintCount) = (u32) sobj;
                hintCount++;
            }
            sobj = sobj->next;
        }
    }
    if (snap_page_ctx != 0) {
        snap_course_legend_show(0);
    }
    hintStrip = snap_make_strip(STR_MODS_HINT_BOTH, 161, 41);
    snap_course_gobj_show((u32) hintStrip, 0);

    /* The host lists the folder and composes the first window once it sees
     * the page open, and the strips are built from the bank it then names,
     * after a short wait for that turn (one tick, in practice). */
    MODS_TOP = 0;
    MODS_REQ = 0;
    gen = MODS_GEN;
    MODS_OPEN = 1;
    for (i = 0; (i < 10) && (MODS_GEN == gen); i++) {
        ohWait(1);
    }
    gen = MODS_GEN;
    count = MODS_COUNT;
    if (count > 250) {
        count = 250;   /* MODS_TOP is a byte */
    }
    modCount = MODS_MODCOUNT;
    if (modCount > count) {
        modCount = count;
    }

    descStrip = snap_make_strip((count > 0) ? snap_mods_help_str(0, gen) : STR_MODS_NONE, 49, 171);
    for (i = 0; i < MODS_VISIBLE; i++) {
        const s16 y = PAGE_TOP_Y + i * PAGE_PITCH;
        MODS_LABEL(i) = (u32) snap_make_strip(snap_mods_name_str(i, gen), 50, y);
        /* The values sit further right than the other pages' (163): On and
         * Off are short, and a mod's name wants the room. The host cuts the
         * names to end before this column (src/menu_assets.cpp, ModsNameInkWidth). */
        MODS_VALUE(i) = (u32) snap_make_strip(((MODS_STATE(gen & 1) >> i) & 1) ? STR_ON : STR_OFF, 212, y);
        snap_tint((GObj*) MODS_VALUE(i), SEL_R, SEL_G, SEL_B);
    }
    PAGE_ARROW_UP = (u32) snap_make_strip_fmt(STR_SCROLL_UP, ARROW_X, ARROW_UP_Y, G_IM_FMT_RGBA);
    PAGE_ARROW_DN = (u32) snap_make_strip_fmt(STR_SCROLL_DN, ARROW_X, ARROW_DN_Y, G_IM_FMT_RGBA);

    sel = 0;
    top = 0;
    snap_window_layout(0, count, modCount, top);
    snap_mods_hint_show(hintStrip, sel, top, gen, modCount);
    pulseState = 0;
    pulseCounter = 0;
    bobTick = 0;
    nudgeUp = 0;
    nudgeDn = 0;
    snap_nav_dir_v = 0;
    snap_nav_dir_h = 0;

    ohWait(2);

    while (1) {
        input = func_800AA38C(0);
        snap_vr_header();
        {
            s32 pointed=snap_vr_row(sel, top, MODS_VISIBLE, count);
            if(pointed!=sel) {
                snap_tint((GObj*)MODS_LABEL(sel-top),0xFF,0xFF,0xFF);
                sel=pointed; pulseState=0;
                snap_swap_strip(descStrip,snap_mods_help_str(sel-top,gen)); snap_mods_hint_show(hintStrip,sel,top,gen,modCount);
            }
        }

        /* The host turned the bank: the window's names and values are new. */
        if (MODS_GEN != gen) {
            gen = MODS_GEN;
            snap_mods_swap(sel, top, gen, descStrip);
            snap_mods_hint_show(hintStrip, sel, top, gen, modCount);
        }

        if ((gContInputPressedButtons & B_BUTTON) || snap_start_closes()) {
            snap_ui_sound(SND_BACK);
            break;
        }

        snap_nav_read(&navUp, &navDown, &navLeft, &navRight);

        if ((count > 0) && ((gContInputPressedButtons & A_BUTTON) || navLeft || navRight)) {
            if (sel < modCount) {
                result = snap_mods_request((1u << 16) | (u32) sel);
                if ((result == MODS_ON) || (result == MODS_OFF)) {
                    snap_ui_sound(SND_OK);
                } else {
                    snap_ui_sound(SND_BACK);
                }
            } else if (gContInputPressedButtons & A_BUTTON) {
                /* The action rows: the folder, or the restart. */
                result = snap_mods_request(((u32) (4 + (sel - modCount)) << 16) | (u32) sel);
                snap_ui_sound((result == MODS_DONE) ? SND_OK : SND_BACK);
            }
            ohWait(1);
            continue;
        }

        if ((gContInputPressedButtons & Z_TRIG) && (sel < modCount) &&
            ((MODS_HASOPT(gen & 1) >> (sel - top)) & 1)) {
            snap_ui_sound(SND_OK);
            snap_tint((GObj*) MODS_LABEL(sel - top), 0xFF, 0xFF, 0xFF);
            snap_mods_strips_show(0, hdrStrip, descStrip, hintStrip, count, modCount, top, sel, gen);
            ohWait(1);
            snap_mod_options_page(sel);
            if (snap_close_all) {
                break;
            }
            gen = MODS_GEN;
            snap_mods_swap(sel, top, gen, descStrip);
            snap_mods_strips_show(1, hdrStrip, descStrip, hintStrip, count, modCount, top, sel, gen);
            snap_nav_dir_v = 0;
            snap_nav_dir_h = 0;
            pulseState = 0;
            ohWait(1);
            continue;
        }

        if ((gContInputPressedButtons & (L_TRIG | R_TRIG)) && (sel < modCount) && (modCount > 1)) {
            const s32 down = (gContInputPressedButtons & R_TRIG) != 0;
            if ((down && (sel < modCount - 1)) || (!down && (sel > 0))) {
                result = snap_mods_request(((u32) (down ? 3 : 2) << 16) | (u32) sel);
                if (result == MODS_DONE) {
                    /* The mod moved; the selection goes with it. */
                    snap_tint((GObj*) MODS_LABEL(sel - top), 0xFF, 0xFF, 0xFF);
                    sel += down ? 1 : -1;
                    if (sel < top) {
                        top = sel;
                    } else if (sel >= top + MODS_VISIBLE) {
                        top = sel - (MODS_VISIBLE - 1);
                    }
                    if (top != (s32) MODS_TOP) {
                        MODS_TOP = (u8) top;
                        snap_window_layout(0, count, modCount, top);
                    }
                    pulseState = 0;
                    snap_ui_sound(SND_OK);
                } else {
                    snap_ui_sound(SND_BACK);
                }
            }
            ohWait(1);
            continue;
        }

        if ((navUp || navDown) && (count > 0)) {
            snap_tint((GObj*) MODS_LABEL(sel - top), 0xFF, 0xFF, 0xFF);
            if (navUp) {
                sel = (sel == 0) ? (count - 1) : (sel - 1);
            } else {
                sel = (sel + 1) % count;
            }
            pulseState = 0;
            if (sel < top) {
                top = sel;
            } else if (sel >= top + MODS_VISIBLE) {
                top = sel - (MODS_VISIBLE - 1);
            }
            if (top != (s32) MODS_TOP) {
                /* A new window: the host recomposes it and turns the bank,
                 * which the swap at the top of the loop follows. */
                MODS_TOP = (u8) top;
                snap_window_layout(0, count, modCount, top);
                if (navUp) {
                    nudgeUp = 12;
                } else {
                    nudgeDn = 12;
                }
            }
            snap_swap_strip(descStrip, snap_mods_help_str(sel - top, gen));
            snap_mods_hint_show(hintStrip, sel, top, gen, modCount);
            snap_ui_sound(SND_MOVE);
        }

        if (count > 0) {
            snap_pulse_label((GObj*) MODS_LABEL(sel - top), &pulseState, &pulseCounter);
        }
        snap_arrows_bob(&bobTick, &nudgeUp, &nudgeDn);
        ohWait(1);
    }

    MODS_OPEN = 0;
    MODS_REQ = 0;
    for (i = 0; i < MODS_VISIBLE; i++) {
        if (MODS_LABEL(i) != 0) {
            omDeleteGObj((GObj*) MODS_LABEL(i));
            MODS_LABEL(i) = 0;
        }
        if (MODS_VALUE(i) != 0) {
            omDeleteGObj((GObj*) MODS_VALUE(i));
            MODS_VALUE(i) = 0;
        }
    }
    if (PAGE_ARROW_UP != 0) {
        omDeleteGObj((GObj*) PAGE_ARROW_UP);
        PAGE_ARROW_UP = 0;
    }
    if (PAGE_ARROW_DN != 0) {
        omDeleteGObj((GObj*) PAGE_ARROW_DN);
        PAGE_ARROW_DN = 0;
    }
    if (hdrStrip != NULL) {
        omDeleteGObj(hdrStrip);
    }
    if (descStrip != NULL) {
        omDeleteGObj(descStrip);
    }
    if (hintStrip != NULL) {
        omDeleteGObj(hintStrip);
    }
    for (i = 0; i < hintCount; i++) {
        SObj* sobj = (SObj*) MODS_HINT(i);
        sobj->sprite.attr &= ~SP_HIDDEN;
    }
    if (snap_page_ctx != 0) {
        snap_course_legend_show(1);
    }
    /* Put the Option list back exactly as it was; the next selection loop
     * re-shows the right item help line. */
    for (i = 0; i < hiddenCount; i++) {
        SObj* sobj = (SObj*) PAGE_HIDDEN(i);
        sobj->sprite.attr &= ~SP_HIDDEN;
    }
    ohWait(1);
}


/* =========================================================================
 * The pages from a course (pause_menu_patch.inc).
 *
 * The pause menu's OPTIONS pill opens this list over the paused, dimmed
 * course, in the Option screen's own dress: the rules above and below the
 * heading, the header's A OK / B Cancel, the help box with its rules and
 * sides, all at that screen's own coordinates (src/main_menu/A0D4D0.c) from
 * strips the host staged of the very same sprites. Five rows, Graphics,
 * Sound, Controls, Mods and Exit Game, each the item the Option screen has,
 * from the first row down as the stock list runs (Screen, the cartridge's
 * picture-position page, is main menu code and stays on the title's
 * screen). The pages run with snap_page_ctx set, so they leave the Option
 * screen's sprites alone (there are none here) and use the course's sounds;
 * everything else about them is unchanged, the settings mailbox included,
 * so a change made here applies as it would from the title. B leaves the
 * list for the pause menu; Start, here or on any page but Button Setup
 * (where Start is a button to bind), keeps what is on screen, closes
 * everything and resumes the ride -- the key that opened the menu closes
 * it, as the other recompilations' menus do.
 * ========================================================================= */
#define COURSE_ITEMS       5
#define COURSE_OPT_EXIT    4
#define COURSE_DRESS_COUNT 7
#define COURSE_LABEL(i) (*(volatile u32*) (SCRATCH_ARRAYS + 0x2B0 + (i) * 4))   /* GObj*, 5: after the hints */
#define COURSE_DRESS(i) (*(volatile u32*) (SCRATCH_ARRAYS + 0x2C4 + (i) * 4))   /* GObj*, 7: rules, sides, legend */
#define COURSE_ROW_Y(i) (73 + (i) * 16)

static void snap_exit_game(void);

static s32 snap_course_label_str(s32 i) {
    return (i == 0) ? STR_ITEM_LABEL : (i == 1) ? STR_SND_ITEM : (i == 2) ? STR_CTL_ITEM
         : (i == 3) ? STR_MODS_ITEM : STR_EXIT_ITEM;
}

static s32 snap_course_help_str(s32 i) {
    return (i == 0) ? STR_ITEM_HELP : (i == 1) ? STR_SND_ITEM_HELP : (i == 2) ? STR_CTL_ITEM_HELP
         : (i == 3) ? STR_MODS_ITEM_HELP : STR_EXIT_HELP;
}

static void snap_course_gobj_show(u32 word, s32 show) {
    GObj* gobj = (GObj*) word;
    if ((gobj != NULL) && (gobj->data.sobj != NULL)) {
        snap_sprite_show(gobj->data.sobj, (u8) show);
    }
}

/* The rows off while a page is up; the dress stays, as the Option screen's
 * own stays under its pages. */
static void snap_course_show(s32 show) {
    s32 i;
    for (i = 0; i < COURSE_ITEMS; i++) {
        snap_course_gobj_show(COURSE_LABEL(i), show);
    }
}

static void snap_course_legend_show(s32 show) {
    snap_course_gobj_show(COURSE_DRESS(6), show);
}

/* The Option screen's dress at its own coordinates: a rule over and under
 * the heading, the help box's rules with a side at each end, the legend. */
static void snap_course_dress_make(void) {
    COURSE_DRESS(0) = (u32) snap_make_strip(STR_DRESS_RULE, 40, 36);
    COURSE_DRESS(1) = (u32) snap_make_strip(STR_DRESS_RULE, 40, 54);
    COURSE_DRESS(2) = (u32) snap_make_strip(STR_DRESS_RULE, 40, 166);
    COURSE_DRESS(3) = (u32) snap_make_strip(STR_DRESS_RULE, 40, 197);
    COURSE_DRESS(4) = (u32) snap_make_strip(STR_DRESS_SIDE, 39, 168);
    COURSE_DRESS(5) = (u32) snap_make_strip(STR_DRESS_SIDE, 278, 168);
    COURSE_DRESS(6) = (u32) snap_make_strip_32(STR_DRESS_LEGEND, 161, 41);
}

static void snap_course_delete(void) {
    s32 i;
    for (i = 0; i < COURSE_ITEMS; i++) {
        if (COURSE_LABEL(i) != 0) {
            omDeleteGObj((GObj*) COURSE_LABEL(i));
            COURSE_LABEL(i) = 0;
        }
    }
    for (i = 0; i < COURSE_DRESS_COUNT; i++) {
        if (COURSE_DRESS(i) != 0) {
            omDeleteGObj((GObj*) COURSE_DRESS(i));
            COURSE_DRESS(i) = 0;
        }
    }
}

/* A strip of the pause menu's own pill (pause_menu_patch.inc): RGBA16, the
 * pills' own format. */
static GObj* snap_strip_make_rgba(s32 id, s32 x, s32 y) {
    if (DIR_MAGIC != 0x53474130) {
        return NULL;
    }
    return snap_make_strip_fmt(id, x, y, G_IM_FMT_RGBA);
}

/* The list, in the context given: 1 over a course's pause (the course's
 * sounds), 2 over any other screen (anywhere_patch.inc; the menu's). */
static void snap_port_pages(s32 ctx) {
    UnkStruct800BEDF8* input;
    GObj* hdrStrip;
    GObj* helpStrip;
    s32 sel, i, navUp, navDown, open, armed;
    u8 pulseState, pulseCounter;

    if (DIR_MAGIC != 0x53474130) {
        return;
    }
    snap_page_ctx = ctx;
    snap_close_all = 0;
    MBOX_PAGES_OPEN = 1;
    snap_course_dress_make();
    /* The heading is the screen's own "Options" sprite, at its place. */
    hdrStrip = snap_make_strip(STR_COURSE_HDR, 43, 40);
    for (i = 0; i < COURSE_ITEMS; i++) {
        COURSE_LABEL(i) = (u32) snap_make_strip(snap_course_label_str(i), 43, COURSE_ROW_Y(i));
    }
    helpStrip = snap_make_strip(snap_course_help_str(0), 49, 171);
    sel = 0;
    armed = 0;
    pulseState = 0;
    pulseCounter = 0;
    snap_nav_dir_v = 0;
    ohWait(1);

    while (1) {
        input = func_800AA38C(0);
        snap_vr_header();
        {
            s32 pointed=snap_vr_row(sel, 0, COURSE_ITEMS, COURSE_ITEMS);
            if(pointed!=sel) {
                snap_tint((GObj*)COURSE_LABEL(sel),0xFF,0xFF,0xFF);
                sel=pointed; pulseState=0;
                snap_swap_strip(helpStrip,snap_course_help_str(sel)); armed=0;
            }
        }

        if (snap_start_closes()) {
            break;
        }
        if (gContInputPressedButtons & B_BUTTON) {
            snap_ui_sound(SND_BACK);
            if (armed) {
                /* The Exit Game question withdrawn, the list stays. */
                armed = 0;
                snap_swap_strip(helpStrip, snap_course_help_str(sel));
                ohWait(1);
                continue;
            }
            break;
        }

        open = 0;
        if (gContInputPressedButtons & A_BUTTON) {
            if ((sel == COURSE_OPT_EXIT) && !armed) {
                /* Exit Game asks first, as it does on the Option screen:
                 * the help line becomes the question, the next A answers
                 * it, B or a move withdraws it. */
                snap_ui_sound(SND_OK);
                armed = 1;
                snap_swap_strip(helpStrip, STR_EXIT_CONFIRM);
                ohWait(1);
                continue;
            }
            open = 1;
        }

        {
            s32 sy = gContInputStickY;
            s32 sx = gContInputStickX;
            s32 my = (sy < 0) ? -sy : sy;
            s32 mx = (sx < 0) ? -sx : sx;
            s32 dirV = 0;
            navUp = navDown = 0;
            if ((my >= 24) && (my >= mx)) {
                dirV = (sy > 0) ? 1 : -1;
            }
            if (dirV != snap_nav_dir_v) {
                snap_nav_dir_v = dirV;
                snap_nav_repeat_v = 15;
                if (dirV > 0) {
                    navUp = 1;
                }
                else if (dirV < 0) {
                    navDown = 1;
                }
            }
            else if ((dirV != 0) && (--snap_nav_repeat_v <= 0)) {
                snap_nav_repeat_v = 6;
                if (dirV > 0) {
                    navUp = 1;
                }
                else {
                    navDown = 1;
                }
            }
        }

        if (open) {
            snap_ui_sound(SND_OK);
            snap_sprite_gray(((GObj*) COURSE_LABEL(sel))->data.sobj, 0xFF);
            if (sel == COURSE_OPT_EXIT) {
                /* Confirmed: the host closes the program within a frame or
                 * two; one that has not answered leaves the list up. */
                snap_exit_game();
                armed = 0;
                snap_swap_strip(helpStrip, snap_course_help_str(sel));
                ohWait(1);
                continue;
            }
            /* The list steps aside for the page and comes back after it;
             * the pages hide nothing of the pause menu's on their own. */
            snap_course_show(0);
            snap_course_gobj_show((u32) hdrStrip, 0);
            snap_course_gobj_show((u32) helpStrip, 0);
            ohWait(1);
            switch (sel) {
                case 0:
                    snap_graphics_page();
                    break;
                case 1:
                    snap_sound_page();
                    break;
                case 2:
                    while (snap_controls_page()) {
                        snap_bind_page();
                    }
                    break;
                default:
                    snap_mods_page();
                    break;
            }
            if (snap_close_all) {
                break;   /* Start on the page: everything closes at once */
            }
            snap_course_show(1);
            snap_course_gobj_show((u32) hdrStrip, 1);
            snap_course_gobj_show((u32) helpStrip, 1);
            snap_nav_dir_v = 0;
            pulseState = 0;
            ohWait(1);
            continue;
        }

        if (navUp || navDown) {
            snap_sprite_gray(((GObj*) COURSE_LABEL(sel))->data.sobj, 0xFF);
            if (navUp) {
                sel = (sel == 0) ? (COURSE_ITEMS - 1) : (sel - 1);
            } else {
                sel = (sel + 1) % COURSE_ITEMS;
            }
            armed = 0;
            pulseState = 0;
            snap_swap_strip(helpStrip, snap_course_help_str(sel));
            snap_ui_sound(SND_MOVE);
        }

        /* The stock pulse on the selected label, as the Option list has. */
        {
            SObj* sobj = ((GObj*) COURSE_LABEL(sel))->data.sobj;
            switch (pulseState) {
                case 0:
                    if (sobj->sprite.red >= 0x84) {
                        sobj->sprite.red -= 4;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        snap_sprite_gray(sobj, 0x80);
                        pulseState = 1;
                    }
                    break;
                case 1:
                    if (sobj->sprite.red < 0xE2) {
                        sobj->sprite.red += 0x1E;
                        snap_sprite_gray(sobj, sobj->sprite.red);
                    } else {
                        pulseCounter = 0;
                        snap_sprite_gray(sobj, 0xFF);
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

    snap_course_delete();
    if (hdrStrip != NULL) {
        omDeleteGObj(hdrStrip);
    }
    if (helpStrip != NULL) {
        omDeleteGObj(helpStrip);
    }
    snap_page_ctx = 0;
    MBOX_PAGES_OPEN = 0;
    ohWait(1);
}

/* Exit Game, chosen and confirmed: the host is asked to close the program
 * and does so within a frame or two (src/menu_assets.cpp,
 * poll_menu_mailbox, the same path as the window's close button). Waited
 * for here rather than returned from -- the screen must not start its fade
 * back to the title under a quit in progress -- but not forever: a host
 * that has not answered in two seconds leaves the player on the list. */
static void snap_exit_game(void) {
    s32 i;

    MBOX_QUIT_REQ = 1;
    for (i = 0; i < 120; i++) {
        ohWait(1);
    }
    MBOX_QUIT_REQ = 0;
}

/* Replaces the Option screen loop: makes room for the port's items,
 * creates their labels and help lines, retires the stock Z Button and
 * Control Stick rows to the CONTROLS page, and dispatches. */
void func_800E7F98_A0F528(void) {
    s32 cond;
    u32 sel;
    s32 i;
    GObj* graphicsLabel;
    GObj* itemHelp;
    GObj* controlsLabel;
    GObj* controlsHelp;
    GObj* exitLabel;
    GObj* exitHelp;
    GObj* exitAsk;
    GObj* modsLabel;
    GObj* modsHelp;

    func_800E71DC_A0E76C();
    MBOX_PAGES_OPEN = 1;   /* the Option screen is up: Esc is Start here, not the pages */

    graphicsLabel = NULL;
    itemHelp = NULL;
    controlsLabel = NULL;
    controlsHelp = NULL;
    exitLabel = NULL;
    exitHelp = NULL;
    exitAsk = NULL;
    modsLabel = NULL;
    modsHelp = NULL;
    SCRATCH_GRAPHICS_GOBJ = 0;
    SCRATCH_HELP_ITEM = 0;
    SCRATCH_CONTROLS_GOBJ = 0;
    SCRATCH_HELP_CONTROLS = 0;
    SCRATCH_EXIT_GOBJ = 0;
    SCRATCH_HELP_EXIT = 0;
    SCRATCH_HELP_EXIT2 = 0;
    SCRATCH_MODS_GOBJ = 0;
    SCRATCH_HELP_MODS = 0;
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

        /* The stock Z Button and Control Stick rows move to the CONTROLS
         * page: after the shift above they sit at y=121 and y=137 (labels
         * and their dots), their colons at (158,123) and (158,139), and
         * their value pairs are whole chains. All hidden, never deleted:
         * the screen's teardown owns them. Return, at y=153 after the
         * shift, comes up to the fourth slot's rhythm at 137, and the
         * CONTROLS item takes the third at 121. */
        for (i = 0; i < 12; i++) {
            GObj* chain = snap_chain(i);
            SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
            while (sobj != NULL) {
                if ((sobj->sprite.y == 121) || (sobj->sprite.y == 137) ||
                    ((sobj->sprite.x == 158) && ((sobj->sprite.y == 123) || (sobj->sprite.y == 139)))) {
                    sobj->sprite.attr |= SP_HIDDEN;
                }
                sobj = sobj->next;
            }
        }
        if ((D_800E835C_A0F8EC != NULL) && (D_800E835C_A0F8EC->data.sobj != NULL)) {
            SObj* pair = D_800E835C_A0F8EC->data.sobj;
            while (pair != NULL) {
                pair->sprite.attr |= SP_HIDDEN;
                pair = pair->next;
            }
        }
        if ((D_800E8360_A0F8F0 != NULL) && (D_800E8360_A0F8F0->data.sobj != NULL)) {
            SObj* pair = D_800E8360_A0F8F0->data.sobj;
            while (pair != NULL) {
                pair->sprite.attr |= SP_HIDDEN;
                pair = pair->next;
            }
        }
        for (i = 0; i < 12; i++) {
            GObj* chain = snap_chain(i);
            SObj* sobj = (chain != NULL) ? chain->data.sobj : NULL;
            while (sobj != NULL) {
                if (sobj->sprite.y == 153) {
                    sobj->sprite.y = 137;
                }
                sobj = sobj->next;
            }
        }
        controlsLabel = snap_make_strip(STR_CTL_ITEM, 43, 121);
        controlsHelp = snap_make_strip(STR_CTL_ITEM_HELP, 49, 171);
        if (controlsHelp != NULL) {
            controlsHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
        SCRATCH_CONTROLS_GOBJ = (u32) controlsLabel;
        SCRATCH_HELP_CONTROLS = (u32) controlsHelp;

        /* The sixth row, at the slot Return vacated: the list's own
         * cadence puts it at 153, the last row the help box leaves room
         * for. Its help line and the question it turns into share the
         * help slot with the others and start hidden. */
        exitLabel = snap_make_strip(STR_EXIT_ITEM, 43, 153);
        exitHelp = snap_make_strip(STR_EXIT_HELP, 49, 171);
        if (exitHelp != NULL) {
            exitHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
        exitAsk = snap_make_strip(STR_EXIT_CONFIRM, 49, 171);
        if (exitAsk != NULL) {
            exitAsk->data.sobj->sprite.attr |= SP_HIDDEN;
        }
        SCRATCH_EXIT_GOBJ = (u32) exitLabel;
        SCRATCH_HELP_EXIT = (u32) exitHelp;
        SCRATCH_HELP_EXIT2 = (u32) exitAsk;

        /* The MODS item takes the Return row (y=137 after the shifts above):
         * B returns from this screen already, as the stock help line says,
         * and the list has no seventh slot above the help box. The stock
         * Return label, the chain's fifth sprite, is hidden and never
         * deleted; it is the fourth item again wherever nothing is staged. */
        {
            SObj* ret = D_800E8340_A0F8D0->data.sobj;
            for (i = 0; (i < 4) && (ret != NULL); i++) {
                ret = ret->next;
            }
            modsLabel = snap_make_strip(STR_MODS_ITEM, 43, 137);
            if ((modsLabel != NULL) && (ret != NULL)) {
                ret->sprite.attr |= SP_HIDDEN;
            }
        }
        modsHelp = snap_make_strip(STR_MODS_ITEM_HELP, 49, 171);
        if (modsHelp != NULL) {
            modsHelp->data.sobj->sprite.attr |= SP_HIDDEN;
        }
        SCRATCH_MODS_GOBJ = (u32) modsLabel;
        SCRATCH_HELP_MODS = (u32) modsHelp;
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
                case OPT_CONTROLS:
                    /* The Button Setup row opens the BUTTON SETUP page, and the
                     * CONTROLS page comes back on that row when it closes. */
                    while (snap_controls_page()) {
                        snap_bind_page();
                    }
                    break;
                case OPT_MODS:
                    snap_mods_page();
                    break;
                case OPT_EXIT:
                    snap_exit_game();
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

    MBOX_PAGES_OPEN = 0;
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
    if (controlsLabel != NULL) {
        omDeleteGObj(controlsLabel);
    }
    if (controlsHelp != NULL) {
        omDeleteGObj(controlsHelp);
    }
    if (exitLabel != NULL) {
        omDeleteGObj(exitLabel);
    }
    if (exitHelp != NULL) {
        omDeleteGObj(exitHelp);
    }
    if (exitAsk != NULL) {
        omDeleteGObj(exitAsk);
    }
    if (modsLabel != NULL) {
        omDeleteGObj(modsLabel);
    }
    if (modsHelp != NULL) {
        omDeleteGObj(modsHelp);
    }
    SCRATCH_GRAPHICS_GOBJ = 0;
    SCRATCH_HELP_ITEM = 0;
    SCRATCH_CONTROLS_GOBJ = 0;
    SCRATCH_HELP_CONTROLS = 0;
    SCRATCH_EXIT_GOBJ = 0;
    SCRATCH_HELP_EXIT = 0;
    SCRATCH_HELP_EXIT2 = 0;
    SCRATCH_MODS_GOBJ = 0;
    SCRATCH_HELP_MODS = 0;
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
    snap_sprite_attr(sobj, SP_TEXSHUF | SP_SCALE | SP_TRANSPARENT);
    if (arg0 == 0xD) {
        snap_sprite_rgb(sobj, 0xFF, 0xFF, 0xFF);
    } else if (arg0 == 0xC) {
        snap_sprite_rgb(sobj, 0x80, 0x80, 0x80);
    }

    /* The badge rides the background's own sprite chain, so it is drawn
     * with the title and torn down with the title, wherever the title
     * goes. Dimmed with the background when the title sits behind a menu. */
    badge = snap_build_sprite(STR_LOGO, 197, 107, G_IM_FMT_RGBA, G_IM_SIZ_16b);
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

    snap_sprite_move(sobj, 35, 35);
    snap_sprite_attr(sobj, SP_TEXSHUF | SP_TRANSPARENT);
    omGObjAddSprite(gobj, (Sprite*) 0x802F82C8);

    sobj = sobj->next;
    /* The stock seat, exactly: Nintendo's copyright block draws where the
     * ROM draws it (src/main_menu/A08E30.c, func_800E2058_A095E8). */
    snap_sprite_move(sobj, 74, 198);
    snap_sprite_attr(sobj, SP_TEXSHUF | SP_TRANSPARENT);

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
        Sprite* credits = snap_build_sprite(STR_CREDITS, 160 - (DIR_W(STR_CREDITS) / 2), 219, G_IM_FMT_RGBA, G_IM_SIZ_16b);
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
/* Set to 1 whenever the title menu is built: the host attaches the Snap
 * Station (setting on) only once this has happened, because the game's boot
 * tests port 4 for the printer once (func_8009B2BC) and goes straight to the
 * printer's display if it finds one; a station that appeared on a timer
 * raced that test on a slow boot. Cleared by the host when read. */
#define MBOX_TITLE_SEEN    (*(volatile u8*)  (SNAP_GFX_MAILBOX + 0x3C))
#define SCRATCH_TITLE_GOBJ (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x50))

/* Five rows at the stock four-row pitch of 18. The block sits so the
 * letters' cores clear the logo's lowest stroke (near 100) above and the
 * copyright block's cores (from 197) below by the same eight pixels: cores
 * from 107 to 189. The label strip is 128 wide with its text centred, so
 * its x centres it on the column the stock words share. */
#define TITLE_ROW_Y(i)   (106 + (i) * 18)
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
    sp = snap_build_sprite(STR_TITLE_STATION, TITLE_STATION_X, TITLE_ROW_Y(3), G_IM_FMT_IA, G_IM_SIZ_16b);
    if (sp == NULL) {
        return NULL;
    }
    gobj = ohCreateSprite(0xE, ohUpdateDefault, 0, 0x80000000, renDrawSprite, 1, 0x80000000, -1,
                          sp, 0, NULL, 1);
    if (gobj == NULL) {
        return NULL;
    }
    snap_sprite_attr(gobj->data.sobj, SP_HIDDEN | SP_TRANSPARENT);
    snap_sprite_rgb(gobj->data.sobj, 0x80, 0x80, 0x80);
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
    snap_sprite_move(gobj->data.sobj, x, y);
    snap_sprite_show(gobj->data.sobj, 1);
    snap_sprite_rgb(gobj->data.sobj, 0xC0, 0xC0, 0);
}

/* Stock for two, three and four items (the y values are the game's table,
 * D_800E80E4_A0F674), and five when the Snap Station label exists. */
u8 func_800E33C8_A0A958(GObj** gobjs) {
    GObj* station;

    MBOX_TITLE_SEEN = 1;
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
                snap_ui_sound(SND_OK);
                func_800E1B78_A09108(1);
                return 6;
            case 1:
                snap_ui_sound(SND_OK);
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
                        snap_ui_sound(SND_OK);
                        func_800E1B78_A09108(0);
                        return 9;
                    } else if (station && (arg1 == 3)) {
                        MBOX_TITLE_REQ = 1;
                        snap_ui_sound(SND_OK);
                        func_800E1B78_A09108(0);
                        return 9;
                    } else if (arg1 == (station ? 4 : 3)) {
                        snap_ui_sound(SND_OK);
                        func_800E1B78_A09108(1);
                        return 8;
                    }
                } else {
                    snap_ui_sound(SND_OK);
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
        for(i=0;i<temp_s3;i++) {
            if(snap_vr_sprite(sp54[i]->data.sobj) && i!=var_s0) {
                ohEndAllObjectProcesses(sp54[var_s0]);
                func_800E18E0_A08E70(sp54[var_s0]->data.sobj,0xC0,0xC0,0);
                var_s0=i;
                omCreateProcess(sp54[var_s0],func_800E3240_A0A7D0,0,1);
            }
        }

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
            snap_ui_sound(SND_MOVE);
            ohEndAllObjectProcesses(sp54[var_s0]);
            snap_sprite_rgb((sp54[var_s0])->data.sobj, 0xC0, 0xC0, 0);
            var_s0--;
            if (var_s0 < 0) {
                var_s0 = temp_s3 - 1;
            }
            omCreateProcess(sp54[var_s0], func_800E3240_A0A7D0, 0, 1);
        } else if (temp_v0->pressedButtons & STICK_SLOW_DOWN) {
            snap_ui_sound(SND_MOVE);
            ohEndAllObjectProcesses(sp54[var_s0]);
            snap_sprite_rgb(sp54[var_s0]->data.sobj, 0xC0, 0xC0, 0);
            var_s0++;
            var_s0 %= temp_s3;
            omCreateProcess(sp54[var_s0], func_800E3240_A0A7D0, 0, 1);
        } else if (temp_v0->pressedButtons & B_BUTTON) {
            snap_ui_sound(SND_BACK);
            ohEndAllObjectProcesses(sp54[var_s0]);
            for (i = 0; i < temp_s3; i++) {
                snap_sprite_show(sp54[i]->data.sobj, 0);
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

/* The pause menu's OPTIONS item and the pages from a course: one translation
 * unit with the pages, for the reason the file says. */
#include "pause_menu_patch.inc"
#include "anywhere_patch.inc"
