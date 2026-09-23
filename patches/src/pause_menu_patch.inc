/**
 * The Option pages from a course: an OPTIONS pill on the pause menu, and
 * Esc or Select straight to the pages.
 *
 * The port's pages -- Graphics, Sound, Controls with Button Setup, Mods --
 * lived only in the title's Option screen, so a change of frame rate or a
 * button in the middle of a ride meant leaving the course. The other
 * recompilations open their menu on a key from anywhere; this port has no
 * menu of its own outside the game, so the game's own pause is the place.
 * Two ways in: Start pauses the ride as the cartridge does, and the pause
 * menu gains a fourth pill under Continue, Quit Course and Retry -- green,
 * the one colour of the set the menu does not use, in the pills' own
 * artwork and letters (src/menu_assets.cpp composes the pair, plain and
 * selected, out of the stock pills when the strings are staged); Esc, or
 * the pad's Select, pauses the ride the same way and opens the pages at
 * once, with no menu in between (the host's word for the press is
 * MBOX_MENU_REQ), and closes them again. The pages open in the Option
 * screen's own dress over the paused, dimmed course (graphics_menu_patch.c,
 * snap_port_pages); from the pill, B brings the pause menu back as it was;
 * Start or Esc closes everything and resumes the ride.
 *
 * Four functions of src/app_level/player.c are replaced, each its original
 * body plus the item: updatePauseMenu (the per-frame handler: Start, A and
 * B; it sleeps while a page is open, so the pages own the input),
 * Pause_UpdateSelection (the stick; four items instead of three when the
 * pill exists), Pause_ShowUI (the slide-in; it shows the pill with the
 * others, and, in a second mode, runs the pages -- it is the pause menu's
 * own coroutine, and a process handed a function of this patch cannot be
 * dispatched by the runtime, only one of the game's, so the pages run in a
 * second instance of it) and Pause_HideUI (it takes the pill down first).
 * A process created here is given the game's address of its function, not
 * this patch's, for the same reason; the runtime's tables then run the
 * replacement, as they do for the game's own calls.
 *
 * The HUD's icons -- the item balls with their button badges, the Z and R
 * hints -- step aside while the pause is up, the way the game itself takes
 * them down for its cutscenes (Icons_Hide): the fourth pill stands where
 * the balls are, and even the cartridge's Retry pill lay over the B ball's
 * badge. They return as the menu slides out. The film counter stays for
 * the pause menu and goes only behind the pages, whose header rule runs
 * under it.
 *
 * The pill is two strips of the port's staged art, made afresh at every
 * pause and deleted when the pause menu goes, so no pointer outlives the
 * scene it was made in. Without the staged strings (the directory magic is
 * not set) the pause menu is the cartridge's three items.
 */

/* Compiled as part of graphics_menu_patch.c (included at its end): the
 * patch build treats every global function as a replacement of a game
 * function, so what this file shares with the pages -- the strip maker,
 * the in-course list, the close-all flag -- is static there, and this file
 * sees it. */

/* The pause menu's own (src/app_level/player.c). */
extern GObj* ObjectPauseMenu;
extern SObj* Pause_LabelPause2;
extern SObj* Pause_LabelPause;
extern SObj* Pause_QuitCourseSelected;
extern SObj* Pause_QuitCourse;
extern SObj* Pause_ContinueSelected;
extern SObj* Pause_Continue;
extern SObj* Pause_RetrySelected;
extern SObj* Pause_Retry;
extern u8 IsPaused;
extern u8 PauseCurrentSelection;
extern u8 PauseButtonPressTimeout;
extern u8 D_80382D44_523154;
extern s32 Pause_StickReleased;
extern s32 IsInputDisabled;
extern s32 gDirectionIndex;
extern GObj* gObjPlayer;
extern s32 D_80382D28_523138;
extern u8 D_80382D2C_52313C;
extern u8 D_80382D30_523140;
extern u8 D_80382D34_523144;
extern s32 D_80382D38_523148;
extern f32 D_80382D3C_52314C;
extern void (*PauseCb)(s32);

/* The HUD (src/app_level/icons.c, 4FE780.c): the icons' object and its
 * shown flag, and the film counter's object. */
extern s32 Icons_IsShown;
extern GObj* D_803B09D8_550DE8;
void Icons_Hide(void);
void Icons_Show(void);

void func_8035464C_4F4A5C(GObj* arg0);
void func_80354860_4F4C70(GObj* arg0);
void quitCourse(GObj* arg0);
void retryCourse(GObj* arg0);
void func_80357120_4F7530(GObj* arg0);
void Icons_Freeze(void);
void PokemonDetector_Disable(void);
void Msg_Reset(void);
void func_80352F20_4F3330(GObj* arg0);
void processZoomingIn(GObj* obj);
void processZoomingOut(GObj* obj);
void updateCameraZoomedIn(GObj* obj);
void updateMovementState(GObj* obj);
void handleAnalogStick(GObj* obj);
void handleCButtons(GObj* obj);
void handleItemButtonsPress(GObj* obj);
void vibrateCamera(GObj* obj);

/* The game's addresses of the three replaced coroutine and process
 * functions (patches/pokemonsnap.syms.toml): what a process is created
 * with here, so the runtime dispatches it. */
#define GAME_Pause_UpdateSelection ((GObjFunc) 0x80353D90)
#define GAME_Pause_ShowUI          ((GObjFunc) 0x80354140)
#define GAME_Pause_HideUI          ((GObjFunc) 0x803543B0)

/* The pill's place: the stock pills stand at x=116, at y=133, 156 and 179,
 * twenty-three apart; the fourth keeps the rhythm. */
#define SNAP_PAUSE_ITEM_X      116
#define SNAP_PAUSE_ITEM_Y      202
#define PAUSE_OPTION_OPTIONS   3

#define SOUND_PAUSE_MOVE   65
#define SOUND_PAUSE_CHOOSE 66
#define SOUND_PAUSE_OPEN   11

static GObj* snap_pause_item = NULL;       /* the plain OPTIONS pill while the pause menu is up */
static GObj* snap_pause_item_sel = NULL;   /* the selected one */
static s32 snap_pause_page_open = 0;       /* a page is open over the pause menu */
static s32 snap_pause_show_mode = 0;       /* 1: the next Pause_ShowUI runs the pages */
static s32 snap_pause_hid_icons = 0;       /* the HUD icons were shown, and this pause hid them */
static s32 snap_pause_direct = 0;          /* this pause went straight to the pages: no menu, no PAUSE label */

static void snap_sprite_colour(Sprite* sp, u8 red, u8 green, u8 blue, u8 alpha) {
    sp->red = red;
    sp->green = green;
    sp->blue = blue;
    sp->alpha = alpha;
}

static void snap_sprite_shown(SObj* sobj, s32 shown) {
    if (sobj == NULL) {
        return;
    }
    if (shown) {
        sobj->sprite.attr &= ~SP_HIDDEN;
    } else {
        sobj->sprite.attr |= SP_HIDDEN;
    }
}

static void snap_gobj_shown(GObj* gobj, s32 shown) {
    if (gobj != NULL) {
        snap_sprite_shown(gobj->data.sobj, shown);
    }
}

static s32 snap_pause_items(void) {
    return (snap_pause_item != NULL) ? 4 : 3;
}

/* The selected item's "pressed" sprite shown and its plain one hidden, the
 * reverse for the others -- the stock switch, the fourth pair with it. */
static void snap_pause_item_state(void) {
    const s32 sel = PauseCurrentSelection;
    snap_sprite_shown(Pause_ContinueSelected, sel == 0);
    snap_sprite_shown(Pause_Continue, sel != 0);
    snap_sprite_shown(Pause_QuitCourseSelected, sel == 1);
    snap_sprite_shown(Pause_QuitCourse, sel != 1);
    snap_sprite_shown(Pause_RetrySelected, sel == 2);
    snap_sprite_shown(Pause_Retry, sel != 2);
    if (snap_pause_item != NULL) {
        snap_gobj_shown(snap_pause_item_sel, sel == PAUSE_OPTION_OPTIONS);
        snap_gobj_shown(snap_pause_item, sel != PAUSE_OPTION_OPTIONS);
    }
}

/* Both pills off, or the pair as the selection has it. */
static void snap_pause_item_shown(s32 shown) {
    if (shown) {
        snap_pause_item_state();
    } else {
        snap_gobj_shown(snap_pause_item, 0);
        snap_gobj_shown(snap_pause_item_sel, 0);
    }
}

static void snap_pause_item_delete(void) {
    if (snap_pause_item != NULL) {
        omDeleteGObj(snap_pause_item);
        snap_pause_item = NULL;
    }
    if (snap_pause_item_sel != NULL) {
        omDeleteGObj(snap_pause_item_sel);
        snap_pause_item_sel = NULL;
    }
}

/* The HUD icons back if this pause took them down. */
static void snap_pause_icons_back(void) {
    if (snap_pause_hid_icons) {
        snap_pause_hid_icons = 0;
        Icons_Show();
    }
}

/* The resume, as the pause menu's B does it: the slide-out, the music's
 * way back up, the processes woken after the slide. */
static void snap_pause_resume(GObj* obj) {
    omCreateProcess(obj, GAME_Pause_HideUI, 0, 9);
    PauseButtonPressTimeout = 30;
    D_80382D3C_52314C = 1.0f;
    omCreateProcess(obj, func_8035464C_4F4A5C, 0, 9);
    omCreateProcess(obj, func_80354860_4F4C70, 0, 9);
    gContInputPressedButtons = 0;
}

/* Everything of the pause menu's off, the pages, then everything back as it
 * was, with the pause menu's own processes asleep in between -- or, when
 * Start closed the pages or Esc opened them, the resume straight away. */
static void snap_pause_pages(GObj* obj) {
    GObj* counter = D_803B09D8_550DE8;
    u32 counterFlags = 0;

    snap_pause_page_open = 1;
    ohPauseProcessByFunction(obj, GAME_Pause_UpdateSelection);
    snap_sprite_shown(Pause_LabelPause2, 0);
    snap_sprite_shown(Pause_LabelPause, 0);
    snap_sprite_shown(Pause_ContinueSelected, 0);
    snap_sprite_shown(Pause_Continue, 0);
    snap_sprite_shown(Pause_QuitCourseSelected, 0);
    snap_sprite_shown(Pause_QuitCourse, 0);
    snap_sprite_shown(Pause_RetrySelected, 0);
    snap_sprite_shown(Pause_Retry, 0);
    snap_pause_item_shown(0);
    if (snap_pause_direct && (obj->data.sobj != NULL)) {
        /* Straight in: the slide-in never ran, so the dim it would have
         * brought up is set here, at its full 153, for the slide-out to
         * fade from. */
        Sprite* sprite = &obj->data.sobj->sprite;
        snap_sprite_colour(sprite, D_80382D2C_52313C, D_80382D30_523140, D_80382D34_523144, 153);
        sprite->attr &= ~SP_HIDDEN;
        D_80382D28_523138 = 153;
    }
    /* The film counter sits where the header's rule runs. */
    if (counter != NULL) {
        counterFlags = counter->flags;
        counter->flags |= GOBJ_FLAG_HIDDEN;
    }

    snap_port_pages(1);

    if (counter != NULL) {
        counter->flags = counterFlags;
    }
    ohResumeProcessByFunction(obj, GAME_Pause_UpdateSelection);
    /* The button that closed the list is not the pause menu's, and a stick
     * still held is not a new move. */
    gContInputPressedButtons = 0;
    PauseButtonPressTimeout = 10;
    Pause_StickReleased = 0;
    snap_pause_page_open = 0;

    if (snap_close_all || snap_pause_direct) {
        snap_close_all = 0;
        snap_pause_resume(obj);
        return;
    }
    snap_sprite_shown(Pause_LabelPause2, 1);
    snap_pause_item_state();
}

static void snap_pause_pointer(void) {
    s32 previous = PauseCurrentSelection;
    func_800AA38C(0); /* Refresh the pointer mailbox even in the pause coroutine. */
    if(snap_vr_sprite(Pause_Continue))PauseCurrentSelection=0;
    else if(snap_vr_sprite(Pause_QuitCourse))PauseCurrentSelection=1;
    else if(snap_vr_sprite(Pause_Retry))PauseCurrentSelection=2;
    else if(snap_pause_item!=NULL && snap_vr_sprite(snap_pause_item->data.sobj))PauseCurrentSelection=3;
    if(previous!=PauseCurrentSelection)snap_pause_item_state();

}

void Pause_UpdateSelection(GObj* obj) {
    snap_pause_pointer();
    if (PauseButtonPressTimeout > 0) {
        Pause_StickReleased = 0;
    } else if ((ABS(gContInputStickY) > 20) && Pause_StickReleased) {
        Pause_StickReleased = 0;
        if (gContInputStickY > 0) {
            if (PauseCurrentSelection > 0) {
                PauseCurrentSelection--;
                auPlaySound(SOUND_PAUSE_MOVE);
            }
        } else {
            if (PauseCurrentSelection < snap_pause_items() - 1) {
                PauseCurrentSelection++;
                auPlaySound(SOUND_PAUSE_MOVE);
            }
        }
        snap_pause_item_state();
    } else if (ABS(gContInputStickY) < 5) {
        Pause_StickReleased = 1;
    }
    if (!IsPaused) {
        omEndProcess(NULL);
    }
}

void Pause_ShowUI(GObj* obj) {
    SObj* sobj = obj->data.sobj;
    Sprite* sprite = &sobj->sprite;

    if (snap_pause_show_mode == 1) {
        /* The second mode: the pages, in the pause menu's own coroutine. */
        snap_pause_show_mode = 0;
        snap_pause_pages(obj);
        omEndProcess(NULL);
    }

    D_80382D28_523138 = 0;
    snap_sprite_colour(sprite, D_80382D2C_52313C, D_80382D30_523140, D_80382D34_523144, 0);
    sprite->attr &= ~SP_HIDDEN;
    Pause_LabelPause->sprite.x = 360;
    Pause_LabelPause->sprite.y = 94;
    Pause_LabelPause->sprite.attr &= ~SP_HIDDEN;
    D_80382D38_523148 = 360;

    while (1) {
        if (D_80382D28_523138 > 153) {
            D_80382D28_523138 = 153;
            snap_sprite_colour(sprite, D_80382D2C_52313C, D_80382D30_523140, D_80382D34_523144, 153);
            Pause_LabelPause2->sprite.x = 111;
            Pause_LabelPause2->sprite.y = 94;
            Pause_LabelPause2->sprite.attr &= ~SP_HIDDEN;
            Pause_LabelPause->sprite.attr |= SP_HIDDEN;
            snap_pause_item_state();
            omCreateProcess(obj, GAME_Pause_UpdateSelection, 1, 9);
            omEndProcess(NULL);
        } else {
            Pause_LabelPause->sprite.x = (s16) D_80382D38_523148;
            Pause_LabelPause->sprite.y = 94;
            snap_sprite_colour(sprite, D_80382D2C_52313C, D_80382D30_523140, D_80382D34_523144, (u8) D_80382D28_523138);
            D_80382D28_523138 += 15;
            D_80382D38_523148 -= 24;
        }
        if (IsPaused == 0) {
            omEndProcess(NULL);
        }
        ohWait(1);
    }
}

void Pause_HideUI(GObj* obj) {
    SObj* sobj = obj->data.sobj;

    snap_pause_item_delete();
    snap_pause_icons_back();
    Pause_LabelPause2->sprite.attr |= SP_HIDDEN;
    Pause_LabelPause->sprite.x = (s16) D_80382D38_523148;
    Pause_LabelPause->sprite.y = 94;
    if (!snap_pause_direct) {
        /* The PAUSE label slides out; a pause that went straight to the
         * pages never showed it, and only the dim fades. */
        Pause_LabelPause->sprite.attr &= ~SP_HIDDEN;
    }
    Pause_QuitCourseSelected->sprite.attr |= SP_HIDDEN;
    Pause_QuitCourse->sprite.attr |= SP_HIDDEN;
    Pause_ContinueSelected->sprite.attr |= SP_HIDDEN;
    Pause_Continue->sprite.attr |= SP_HIDDEN;
    Pause_RetrySelected->sprite.attr |= SP_HIDDEN;
    Pause_Retry->sprite.attr |= SP_HIDDEN;

    while (1) {
        if (D_80382D38_523148 < -103) {
            sobj->sprite.attr |= SP_HIDDEN;
            Pause_LabelPause->sprite.attr |= SP_HIDDEN;
            snap_pause_direct = 0;
            omEndProcess(NULL);
        } else {
            Pause_LabelPause->sprite.x = (s16) D_80382D38_523148;
            Pause_LabelPause->sprite.y = 94;
            sobj->sprite.alpha = (u8) D_80382D28_523138;
            D_80382D28_523138 -= 15;
            if (D_80382D28_523138 < 0) {
                sobj->sprite.alpha = 0;
            }
            D_80382D38_523148 -= 24;
        }
        ohWait(1);
    }
}

void updatePauseMenu(GObj* arg0) {
    MBOX_PAUSE_ALIVE = 3;   /* the host routes the menu key here while this runs */
    if (snap_pause_page_open) {
        return;   /* the port's pages own the input while one is open */
    }
    if (IsInputDisabled != 1) {
        if (PauseButtonPressTimeout > 0) {
            PauseButtonPressTimeout--;
            return;
        }
        if (IsPaused) {
            snap_pause_pointer(); /* Resolve this trigger against this frame's target. */
            if ((MBOX_MENU_REQ != 0) && (MBOX_DL_SPARE < SNAP_PAGES_DL_NEED)) {
                MBOX_MENU_REQ = 0;   /* no room in this frame's display list */
            }
            if ((MBOX_MENU_REQ != 0) && (snap_pause_show_mode == 0)) {
                /* Esc or Select on the pause menu: the pages, as the pill's A. */
                MBOX_MENU_REQ = 0;
                auPlaySound(SOUND_PAUSE_CHOOSE);
                snap_pause_show_mode = 1;
                omCreateProcess(arg0, GAME_Pause_ShowUI, 0, 9);
                gContInputPressedButtons = 0;
                return;
            }
            if (gContInputPressedButtons & B_BUTTON) {
                snap_pause_resume(arg0);
            } else if ((gContInputPressedButtons & A_BUTTON) || (gContInputPressedButtons & CONT_START)) {
                if (PauseCurrentSelection == 0) {
                    snap_pause_resume(arg0);
                } else if (PauseCurrentSelection == 1) {
                    auPlaySound(SOUND_PAUSE_CHOOSE);
                    /* The scene goes, and the strips with it; the icons
                     * as the cartridge leaves them, shown. */
                    snap_pause_item = NULL;
                    snap_pause_item_sel = NULL;
                    snap_pause_icons_back();
                    omCreateProcess(arg0, quitCourse, 0, 9);
                    IsPaused = 0;
                    D_80382D44_523154 = 1;
                    omEndProcess(NULL);
                } else if (PauseCurrentSelection == PAUSE_OPTION_OPTIONS) {
                    /* The port's item: the pages, in a second Pause_ShowUI --
                     * where the frame's display list has room for them. */
                    if (MBOX_DL_SPARE >= SNAP_PAGES_DL_NEED) {
                        auPlaySound(SOUND_PAUSE_CHOOSE);
                        snap_pause_show_mode = 1;
                        omCreateProcess(arg0, GAME_Pause_ShowUI, 0, 9);
                    }
                } else {
                    auPlaySound(SOUND_PAUSE_CHOOSE);
                    snap_pause_item = NULL;
                    snap_pause_item_sel = NULL;
                    snap_pause_icons_back();
                    omCreateProcess(arg0, retryCourse, 0, 9);
                    IsPaused = 0;
                    D_80382D44_523154 = 1;
                    omEndProcess(NULL);
                }
                gContInputPressedButtons = 0;
            }
        } else if ((MBOX_MENU_REQ != 0) && (MBOX_DL_SPARE < SNAP_PAGES_DL_NEED)
                   && !(gContInputPressedButtons & CONT_START)) {
            MBOX_MENU_REQ = 0;   /* no room in this frame's display list: the key is dropped */
        } else if ((gContInputPressedButtons & CONT_START) || (MBOX_MENU_REQ != 0)) {
            /* Start: the pause menu. Esc or Select: the pause, and the pages
             * at once, with nothing of the menu shown. */
            const s32 direct = (MBOX_MENU_REQ != 0) && (MBOX_DL_SPARE >= SNAP_PAGES_DL_NEED);
            MBOX_MENU_REQ = 0;
            IsPaused = 1 - IsPaused;
            PauseCurrentSelection = 0;
            func_80357120_4F7530(arg0);
            Icons_Freeze();
            PauseCb(1);
            PokemonDetector_Disable();
            ohPauseProcessByFunction(gObjPlayer, func_80352F20_4F3330);
            Msg_Reset();
            if (gDirectionIndex < 0) {
                ohPauseProcessByFunction(gObjPlayer, processZoomingIn);
                ohPauseProcessByFunction(gObjPlayer, processZoomingOut);
                ohPauseProcessByFunction(gObjPlayer, updateCameraZoomedIn);
                ohPauseProcessByFunction(gObjPlayer, updateMovementState);
                ohPauseProcessByFunction(gObjPlayer, handleAnalogStick);
            } else {
                ohPauseProcessByFunction(gObjPlayer, updateMovementState);
                ohPauseProcessByFunction(gObjPlayer, handleAnalogStick);
                ohPauseProcessByFunction(gObjPlayer, handleCButtons);
                ohPauseProcessByFunction(gObjPlayer, handleItemButtonsPress);
            }
            ohPauseProcessByFunction(gObjPlayer, vibrateCamera);
            snap_pause_direct = direct;
            snap_pause_show_mode = direct;
            snap_pause_item = NULL;
            snap_pause_item_sel = NULL;
            if (!direct) {
                /* The pill, made afresh for this pause and hidden until the
                 * slide-in shows the others; none without the staged art. */
                snap_pause_item = snap_strip_make_rgba(STR_PAUSE_PILL, SNAP_PAUSE_ITEM_X, SNAP_PAUSE_ITEM_Y);
                snap_pause_item_sel = (snap_pause_item != NULL)
                    ? snap_strip_make_rgba(STR_PAUSE_PILL_SEL, SNAP_PAUSE_ITEM_X, SNAP_PAUSE_ITEM_Y) : NULL;
                snap_pause_item_shown(0);
            }
            /* The HUD's icons step aside for the pill, or the pages, if
             * they were up. */
            snap_pause_hid_icons = 0;
            if ((direct || (snap_pause_item != NULL)) && Icons_IsShown) {
                Icons_Hide();
                snap_pause_hid_icons = 1;
            }
            omCreateProcess(arg0, GAME_Pause_ShowUI, 0, 9);
            auPlaySound(SOUND_PAUSE_OPEN);
            PauseButtonPressTimeout = 30;
            D_80382D3C_52314C = 0.5f;
            omCreateProcess(arg0, func_8035464C_4F4A5C, 0, 9);
        }
    }
}
