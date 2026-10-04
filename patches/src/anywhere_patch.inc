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
 * honors, its processes paused the way the level's pause pauses the
 * player's -- draws the black tile over the picture at 245 of 255, and
 * runs the list in the Option screen's dress. When it
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
/* sGeneralHeap, SNAP_ARENA_START and _END and MBOX_ARENA_PTR are declared
 * with the pages' stack in graphics_menu_patch.c. */
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
 * color set, its alpha stepped by a process of its own, and a flag
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
 * picture. At the pause menu's own dim (153 of 255) the screen's own
 * words showed through the list -- the course select's names and scores
 * read as part of the page -- so the picture is all but put out: 245
 * leaves a twenty-fifth of it. The strip maker lays bitmaps out in 64-texel
 * blocks; the tile is one block of sixteen. */
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
        sp->alpha = 245;
    }
    return gobj;
}

/* The music, held with the screen. The freeze below stops the screen's
 * objects and processes, and the game's audio thread is none of them: the
 * music played on under the pages, and a screen scripted to its music --
 * the title's opening, the lab -- came back out of step. So both BGM
 * players are muted first (a volume event their sequence player takes on
 * its next turn; two ticks let the audio thread take it), then held: while
 * MBOX_BGM_HOLD is set the host skips their handler, so no event is
 * processed and the position stands (src/overlay_hook.cpp). The sound
 * player is untouched, so the pages' own sounds go on. The release clears
 * the byte and posts the volume the driver believes in, which the handler
 * takes on its first turn back, in the tick the screen thaws. */
static void snap_bgm_hold(void) {
    s32 i;
    for (i = 0; i < 2; i++) {
        if (auBGMPlayers[i] != NULL) {
            alCSPSetVol(auBGMPlayers[i], 0);
        }
    }
    ohWait(2);
    MBOX_BGM_HOLD = 1;
}

/* The sound effects, held with the screen as well. A scene's loops -- the
 * opening's birds and running water -- are sound effects, not music, and
 * played on under the pages while the scene stood still. The pages' own
 * sounds use the same player, so it keeps running: each effect playing at
 * the press is turned down to nothing on its voice, its own volume kept
 * aside, and put back when the pages close if the slot still holds the
 * same sound; an effect the scene asked for and the audio thread has not
 * yet started starts silent the same way. The slot's stored volume is
 * left alone, so nothing the game reads back changes. audio.c keeps the
 * voices' state as a file-local type; its layout is mirrored here as in
 * sfx_volume_patch.c. */
typedef struct SnapSfxVoice {
    ALLink node;
    void* pvoice;
    ALWaveTable* table;
    void* clientPrivate;
    s16 state;
    s16 priority;
    s16 fxBus;
    s16 unityPitch;
} SnapSfxVoice;

typedef struct SnapSfxState {
    SnapSfxVoice voice;
    ALSound* sound;
    s16 priority;
    f32 pitch;
    s32 state;
    s16 vol;
    ALPan pan;
    u8 fxMix;
} SnapSfxState;

extern s8* auSndpSoundId;
extern u8 auGlobalSoundVolume;
extern ALSndPlayer* auSoundPlayer;

#define SNAP_SFX_SLOTS 64
#define MBOX_SFX_HELD (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x40))   /* reported by the host at the close */
static u8 snap_sfx_held[SNAP_SFX_SLOTS];      /* 1: a voice turned down; 2: a start made silent */
static s8 snap_sfx_voice[SNAP_SFX_SLOTS];
static s32 snap_sfx_sound[SNAP_SFX_SLOTS];
static s16 snap_sfx_vol[SNAP_SFX_SLOTS];      /* the voice's volume at the press */
static u16 snap_sfx_slot_vol[SNAP_SFX_SLOTS]; /* the slot's volume of a start made silent */

static void snap_sfx_hold(void) {
    s32 i;
    s32 n = AU_NUM_SOUNDS;
    s32 held = 0;
    OSIntMask mask;

    if (n > SNAP_SFX_SLOTS) {
        n = SNAP_SFX_SLOTS;
    }
    mask = osSetIntMask(OS_IM_NONE);
    for (i = 0; i < n; i++) {
        snap_sfx_held[i] = 0;
        if (auSndpSoundId[i] != -1) {
            alSndpSetSound(auSoundPlayer, auSndpSoundId[i]);
            if (alSndpGetState(auSoundPlayer) == AL_PLAYING) {
                snap_sfx_held[i] = 1;
                snap_sfx_voice[i] = auSndpSoundId[i];
                snap_sfx_sound[i] = auPlayingSound[i];
                snap_sfx_vol[i] = ((SnapSfxState*) auSoundPlayer->sndState)[auSndpSoundId[i]].vol;
                alSndpSetVol(auSoundPlayer, 0);
                held++;
            }
        } else if (auStartingSound[i] >= 0) {
            snap_sfx_held[i] = 2;
            snap_sfx_sound[i] = auStartingSound[i];
            snap_sfx_slot_vol[i] = auSoundVolume[i];
            auSoundVolume[i] = 0;
            held++;
        }
    }
    osSetIntMask(mask);
    MBOX_SFX_HELD = (u8) held;
}

static void snap_sfx_release(void) {
    s32 i;
    s32 n = AU_NUM_SOUNDS;
    OSIntMask mask;

    if (n > SNAP_SFX_SLOTS) {
        n = SNAP_SFX_SLOTS;
    }
    mask = osSetIntMask(OS_IM_NONE);
    for (i = 0; i < n; i++) {
        if ((snap_sfx_held[i] == 1) && (auSndpSoundId[i] == snap_sfx_voice[i])
            && (auPlayingSound[i] == snap_sfx_sound[i])) {
            alSndpSetSound(auSoundPlayer, auSndpSoundId[i]);
            if (alSndpGetState(auSoundPlayer) == AL_PLAYING) {
                alSndpSetVol(auSoundPlayer, snap_sfx_vol[i]);
            }
        } else if ((snap_sfx_held[i] == 2) && (auPlayingSound[i] == snap_sfx_sound[i])) {
            auSoundVolume[i] = snap_sfx_slot_vol[i];
            if (auSndpSoundId[i] != -1) {
                SnapSfxState* st = &((SnapSfxState*) auSoundPlayer->sndState)[auSndpSoundId[i]];
                alSndpSetSound(auSoundPlayer, auSndpSoundId[i]);
                if (alSndpGetState(auSoundPlayer) == AL_PLAYING) {
                    alSndpSetVol(auSoundPlayer,
                                 (s16) ((snap_sfx_slot_vol[i] * st->sound->sampleVolume * auGlobalSoundVolume) >> 14));
                }
            }
        }
        snap_sfx_held[i] = 0;
    }
    osSetIntMask(mask);
}

static void snap_bgm_release(void) {
    s32 i;
    MBOX_BGM_HOLD = 0;
    for (i = 0; i < 2; i++) {
        if (auBGMPlayers[i] != NULL) {
            alCSPSetVol(auBGMPlayers[i], (s16) auBGMVolume[i]);
        }
    }
}

static void snap_runner_pages(GObj* obj);

void func_800BF444_5C2E4(GObj* obj) {
    /* A job started by snap_run_on_big_stack is on the object it was
     * started for; a runner the host makes is on an object of its own. */
    const s32 mode = (obj == snap_runner_obj) ? snap_runner_mode : 0;
    if (mode == 1) {
        /* The title's Option screen, on this big stack. */
        snap_runner_mode = 0;
        func_800E7F98_A0F528();
        snap_runner_busy = 0;
        omEndProcess(NULL);
        return;
    }
    if (mode == 0) {
        /* As the host makes it, on the scene's stack: the pages go to a
         * copy of this process on a stack of their own, and this one waits
         * for them. */
        snap_run_on_big_stack(obj, 2);
        omEndProcess(NULL);
        return;
    }
    snap_runner_mode = 0;
    snap_runner_pages(obj);
    snap_runner_busy = 0;
    omEndProcess(NULL);
}

static void snap_runner_pages(GObj* obj) {
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
        return;
    }
    if (MBOX_PAGES_OPEN != 0) {
        MBOX_RUNNER_NOTE = 2;
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
        return;
    }
    arenaPtr = MBOX_ARENA_PTR;
    if ((arenaPtr < SNAP_ARENA_START) || (arenaPtr > SNAP_ARENA_END)) {
        arenaPtr = SNAP_ARENA_START;
    }
    if ((SNAP_ARENA_END - arenaPtr) < 0x4000) {
        MBOX_RUNNER_NOTE = 5;
        return;
    }
    MBOX_HEAP_FREE = (u32) sGeneralHeap.end - (u32) sGeneralHeap.ptr;
    MBOX_OBJ_STAT = ((u32) omActiveObjects << 16) | (u16) omMaxObjects;
    MBOX_RUNNER_NOTE = 3;
    snap_bgm_hold();
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
    snap_sfx_hold();
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
    snap_sfx_release();
    snap_bgm_release();
    MBOX_ARENA_PTR = (u32) sGeneralHeap.ptr;
    sGeneralHeap.start = savedStart;
    sGeneralHeap.end = savedEnd;
    sGeneralHeap.ptr = savedPtr;
    omMaxObjects = savedMax;
    snap_close_all = 0;
    gContInputPressedButtons = 0;
}
