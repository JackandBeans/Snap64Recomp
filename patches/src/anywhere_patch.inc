/**
 * The Option pages from anywhere: Esc, or the pad's Select, on any screen.
 *
 * The other recompilations open their menu on one key wherever the player
 * is. This port's pages are game-side coroutines, so opening them on a
 * screen that has no pause menu takes three things the level's pause menu
 * gave for free: a process to run them in, the screen held still beneath
 * them, and a dim. The host turns the key into an object with one process
 * on it (src/menu_assets.cpp, menu_anywhere_tick: omAddGObj and
 * omCreateProcess called from the frame's update, the process given the
 * game's address of the function below). The process freezes every other
 * object -- its update skipped by the flag the object manager already
 * honours, its processes paused the way the level's pause pauses the
 * player's -- draws the black tile over the picture at the pause menu's
 * own 153 of 255, and runs the list in the Option screen's dress. When it
 * closes, the objects wake exactly as they were, the press that closed it
 * is dropped, and the process ends. In a course the key goes to the pause
 * menu's code instead (pause_menu_patch.inc), which pauses the ride as
 * Start does and opens the pages at once.
 *
 * func_800BF444_5C2E4 is a function of the resident code that nothing
 * calls (src/more_funcs/5BF20.c, an empty body), replaced here: a process
 * must be created with the game's address of its function for the runtime
 * to dispatch it, and this address is loaded on every screen.
 */

extern GObj* omGObjListHead[32];
/* s16 in the game (sys/om.c), and the width matters: declared s32 here
 * once, the store of -1 was a four-byte write at a two-byte variable's
 * address, which left the limit as it was and put 0xFFFF into the object
 * size beside it -- the lab's pool then overflowed, or the heap drained,
 * and the game's panic parked the thread with no message (Sep 17 2026). */
extern s16 omMaxObjects;       /* -1: the scene's object pool grows from the heap */
extern s32 omActiveObjects;
extern u16 gContInputCurrentButtons;
/* The scene's general heap (sys/gtl.c): a bump allocator the object manager
 * grows its pools from when a free list runs dry, and whose overflow is the
 * game's panic -- an endless loop, which on the port parks the thread with
 * no message. A screen that has spent its heap (the lab's first frames,
 * the photo screens) cannot give the pages a single object, so while they
 * are up the heap is pointed at the port's own arena, in RDRAM beyond
 * anything the cartridge addresses, and put back after. The objects grown
 * there stay in the scene's free lists until the scene goes; the host
 * moves the arena's cursor back to its start at every scene load. */
#include "sys/ml.h"
extern DynamicBuffer sGeneralHeap;
#define SNAP_ARENA_START 0x80E00000
#define SNAP_ARENA_END   0x80E40000
#define MBOX_ARENA_PTR (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xF0))   /* the arena's cursor */
#define MBOX_HEAP_FREE (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xF4))   /* the scene heap's free bytes at the press */
#define MBOX_OBJ_STAT  (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xF8))   /* active objects << 16 | the limit */
/* The ticks the scene has run since its overlay was loaded (the host
 * counts them). A screen's entrance -- a dissolve, panels sliding in -- is
 * a process like any other, and the freeze would hold it half-way with the
 * pages drawn through it (the evaluation's first moments gave a frame of
 * static with the list in pieces): a press in a screen's first two
 * seconds waits for them to pass. */
#define MBOX_SCENE_AGE (*(volatile u32*) (SNAP_GFX_MAILBOX + 0xFC))
#define SNAP_SETTLE_TICKS 120
#include "sys/oh.h"
#include "sys/render.h"
/* The game's fade (src/app_render/52DE0.c): a full-screen veil in the
 * colour set, its alpha stepped by a process of its own, and a flag
 * while a step is under way. Between a fade out and the fade in that
 * follows, the flag is down and the veil is up. */
extern s32 D_800AF050;   /* a fade is stepping */
extern s32 D_800AF060;   /* the veil's alpha */

#define SNAP_FREEZE_PROCS 1024
#define SNAP_FREEZE_OBJS  512
/* What the freeze paused and flagged, to wake the same and only the same. */
static GObjProcess* snap_frozen_proc[SNAP_FREEZE_PROCS];
static GObj* snap_frozen_obj[SNAP_FREEZE_OBJS];
static s32 snap_frozen_proc_count = 0;
static s32 snap_frozen_obj_count = 0;

static void snap_freeze_scene(GObj* self) {
    s32 link;

    snap_frozen_proc_count = 0;
    snap_frozen_obj_count = 0;
    for (link = 0; link < 32; link++) {
        GObj* obj = omGObjListHead[link];
        while (obj != NULL) {
            if (obj != self) {
                GObjProcess* proc;
                if (!(obj->flags & GOBJ_FLAG_40) && (snap_frozen_obj_count < SNAP_FREEZE_OBJS)) {
                    obj->flags |= GOBJ_FLAG_40;
                    snap_frozen_obj[snap_frozen_obj_count++] = obj;
                }
                proc = obj->processListHead;
                while (proc != NULL) {
                    if (!proc->paused && (snap_frozen_proc_count < SNAP_FREEZE_PROCS)) {
                        proc->paused = 1;
                        snap_frozen_proc[snap_frozen_proc_count++] = proc;
                    }
                    proc = proc->next;
                }
            }
            obj = obj->next;
        }
    }
}

static void snap_thaw_scene(void) {
    s32 i;

    for (i = 0; i < snap_frozen_proc_count; i++) {
        snap_frozen_proc[i]->paused = 0;
    }
    for (i = 0; i < snap_frozen_obj_count; i++) {
        snap_frozen_obj[i]->flags &= ~GOBJ_FLAG_40;
    }
    snap_frozen_proc_count = 0;
    snap_frozen_obj_count = 0;
}

/* Whether the fade's veil is over the screen. A fade (src/app_render/52DE0.c,
 * func_800A7860) makes an object whose render function is the veil's draw
 * and raises D_800AF050 while its process steps the alpha. Neither half
 * says it alone. The alpha: a fade out leaves it at 255, and a screen that
 * follows without a fade of its own (the photo check after a ride, the
 * evaluation) keeps that 255 with no veil drawn at all. The object: the
 * title and the lab keep one in the scene long after their fade in has
 * brought the alpha to nothing. The veil is up while a step is under way,
 * or while such an object is there with an alpha to draw. */
static s32 snap_veil_up(void) {
    s32 link;

    if (D_800AF050 != 0) {
        return 1;
    }
    if (D_800AF060 == 0) {
        return 0;
    }
    for (link = 0; link < 32; link++) {
        GObj* obj = omGObjListHead[link];
        while (obj != NULL) {
            if (obj->fnRender == (GObjFunc) 0x800A750C) {
                return 1;
            }
            obj = obj->next;
        }
    }
    return 0;
}

/* The black tile (string 0, sixteen texels square) stretched over the
 * picture, at the pause menu's own dim. The strip maker lays bitmaps out
 * in 64-texel blocks; the tile is one block of sixteen. */
static GObj* snap_dim_make(void) {
    GObj* gobj = snap_make_strip(0, 0, 0);

    if ((gobj != NULL) && (gobj->data.sobj != NULL)) {
        Sprite* sp = &gobj->data.sobj->sprite;
        sp->nbitmaps = 1;
        sp->bitmap[0].width = 16;
        sp->bitmap[0].width_img = 16;
        sp->attr |= SP_SCALE;
        sp->scalex = 20.0f;
        sp->scaley = 15.0f;
        sp->alpha = 153;
    }
    return gobj;
}

void func_800BF444_5C2E4(GObj* obj) {
    GObj* dim;
    GObj* cam;
    void* savedStart;
    void* savedEnd;
    void* savedPtr;
    u32 arenaPtr;
    s16 savedMax;
    s32 i;

    if (DIR_MAGIC != 0x53474130) {
        MBOX_RUNNER_NOTE = 1;
        omEndProcess(NULL);
        return;
    }
    if (MBOX_PAGES_OPEN != 0) {
        MBOX_RUNNER_NOTE = 2;
        omEndProcess(NULL);
        return;
    }
    /* The fade's veil is drawn over everything, by a process the freeze
     * would hold: the press waits until no veil object is in the scene and
     * no step is under way, four seconds at most, and is given up under a
     * veil that stays -- a screen fading out is on its way somewhere else. */
    for (i = 0; (i < 240) && ((MBOX_SCENE_AGE < SNAP_SETTLE_TICKS) || snap_veil_up()); i++) {
        ohWait(1);
    }
    if (snap_veil_up()) {
        MBOX_RUNNER_NOTE = 4;
        omEndProcess(NULL);
        return;
    }
    arenaPtr = MBOX_ARENA_PTR;
    if ((arenaPtr < SNAP_ARENA_START) || (arenaPtr > SNAP_ARENA_END)) {
        arenaPtr = SNAP_ARENA_START;
    }
    if ((SNAP_ARENA_END - arenaPtr) < 0x4000) {
        MBOX_RUNNER_NOTE = 5;
        omEndProcess(NULL);
        return;
    }
    MBOX_HEAP_FREE = (u32) sGeneralHeap.end - (u32) sGeneralHeap.ptr;
    MBOX_OBJ_STAT = ((u32) omActiveObjects << 16) | (u16) omMaxObjects;
    MBOX_RUNNER_NOTE = 3;
    /* Some scenes fix their object pool (the lab's is 128) and the
     * manager halts the game at the limit; the pages' strips are objects,
     * up to some fifty at once. The limit is lifted while they are up --
     * the pool then grows, once, by what the peak needs, from the arena
     * -- and put back when they are down, with the objects freed. */
    savedMax = omMaxObjects;
    omMaxObjects = -1;
    savedStart = sGeneralHeap.start;
    savedEnd = sGeneralHeap.end;
    savedPtr = sGeneralHeap.ptr;
    sGeneralHeap.start = (void*) SNAP_ARENA_START;
    sGeneralHeap.end = (void*) SNAP_ARENA_END;
    sGeneralHeap.ptr = (void*) arenaPtr;
    snap_freeze_scene(obj);
    /* The pages' own camera, made as the title screen makes its sprite
     * camera, on a draw link no screen uses and at the lowest draw
     * priority, which is the last to draw: over the screen, and the same
     * on every screen whatever links its own cameras cover. */
    snap_strip_dl_link = SNAP_PAGES_DL_LINK;
    cam = ohCreateCamera(3, ohUpdateDefault, 0, 0x80000000, renSpriteCameraRender, 0,
                         1 << SNAP_PAGES_DL_LINK, -1, 1, 1, NULL, 1, 1);
    if (cam != NULL) {
        cam->data.cam->flags = CAMERA_FLAG_8;
    }
    dim = snap_dim_make();
    snap_port_pages(2);
    if (dim != NULL) {
        omDeleteGObj(dim);
    }
    if (cam != NULL) {
        omDeleteGObj(cam);
    }
    snap_strip_dl_link = 1;
    /* The press that closed the pages must not reach the screen when it
     * wakes: a screen's own edge detection would see a button held since
     * before the freeze as a new press. Every button up first, a second at
     * most. */
    for (i = 0; (i < 60) && (gContInputCurrentButtons != 0); i++) {
        ohWait(1);
    }
    snap_thaw_scene();
    MBOX_ARENA_PTR = (u32) sGeneralHeap.ptr;
    sGeneralHeap.start = savedStart;
    sGeneralHeap.end = savedEnd;
    sGeneralHeap.ptr = savedPtr;
    omMaxObjects = savedMax;
    snap_close_all = 0;
    gContInputPressedButtons = 0;
    omEndProcess(NULL);
}
