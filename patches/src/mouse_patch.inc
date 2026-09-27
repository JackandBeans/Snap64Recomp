/**
 * The mouse on the game's own menus.
 *
 * A click used to be A wherever the pointer was, so on any menu it chose
 * whatever the menu had highlighted: Continue on the title, the Beach on the
 * course list. The menus below take the mouse (snap_mouse_take in
 * graphics_menu_patch.c): the item under the pointer is selected as the
 * pointer moves onto it, a click on an item chooses it, a click anywhere
 * else does nothing, the right button is B, and the wheel steps up and
 * down. Each replacement is the game's own function with the mouse added
 * before its stock handling: the pointer moves the selection the way the
 * stick would, and a click on an item is A laid into the frame's input
 * record, which the game rebuilds every tick (src/app_render/53AD0.c,
 * func_800AA38C), so the menu's own A path runs exactly once.
 *
 * Compiled as part of graphics_menu_patch.c (included at its end), which
 * holds the helpers.
 */

/* ---- Oak's lab: the side panel ----------------------------------------
 * The lab's list (Go to Course, PKMN Report, the save) and the course list
 * share one function (src/oaks_lab/8A8A50.c): the selection passed in by
 * pointer, eight slots, a slot of id 35 empty. The panel's buttons stand
 * at x 0, y 25 + 24i (src/window/layout.c, UILayout_CreateButtons), and the
 * focus mark frames 62 by 13 at 22, 29 + 24i. The lab also calls it with
 * an empty record while Oak is talking (func_800AA740): the mouse is taken
 * only with the live one, so a click then does what a press would, nothing. */
extern UIButton* D_80206B44_9CC364;
void FocusMark_SetPos(s32 x, s32 y);
void FocusMark_SetSize(s32 width, s32 height);
s32 FocusMark_SetTargetPos(s32 x, s32 y);

static SnapMouse snap_lab_mouse;

/* The panel slot under the pointer that holds a button, or -1. */
static s32 snap_panel_hit(UIButton* buttons) {
    s32 i;

    if (buttons == NULL) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        if ((buttons[i].id != 35) && snap_mouse_in(8, 25 + i * 24, 92, 22)) {
            return i;
        }
    }
    return -1;
}

s32 func_800E3264_8A8A84(UnkStruct800BEDF8* arg0, s32* arg1) {
    s32 ev, hit;

    if (arg0 == NULL) {
        if (arg1 != NULL) {
            FocusMark_SetPos(0x16, (*arg1 * 24) + 29);
            FocusMark_SetSize(0x3E, 0xD);
        }
        snap_mouse_begin(&snap_lab_mouse);
        return 0;
    }

    if (arg0 == func_800AA38C(0)) {
        ev = snap_mouse_take(&snap_lab_mouse);
        hit = snap_panel_hit(D_80206B44_9CC364);
        if ((ev & (SNAP_MOUSE_MOVED | SNAP_MOUSE_CLICK)) && (hit >= 0) && (hit != *arg1)) {
            *arg1 = hit;
            auPlaySound(SOUND_ID_65);
        }
        if ((ev & SNAP_MOUSE_CLICK) && (hit >= 0)) {
            arg0->pressedButtons |= A_BUTTON;
        }
        if (ev & SNAP_MOUSE_BACK) {
            arg0->pressedButtons |= B_BUTTON;
        }
        if (ev & SNAP_MOUSE_UP) {
            arg0->pressedButtons |= STICK_SLOW_UP;
        }
        if (ev & SNAP_MOUSE_DOWN) {
            arg0->pressedButtons |= STICK_SLOW_DOWN;
        }
    }

    FocusMark_SetTargetPos(0x16, (*arg1 * 24) + 29);

    if ((arg0->pressedButtons & STICK_SLOW_UP) && !(arg0->currentButtons & (STICK_SLOW_RIGHT | STICK_SLOW_LEFT))) {
        do {
            *arg1 = (*arg1 + 7) % 8;
        } while (D_80206B44_9CC364[*arg1].id == 35);

        auPlaySound(SOUND_ID_65);
    }

    if ((arg0->pressedButtons & STICK_SLOW_DOWN) && !(arg0->currentButtons & (STICK_SLOW_RIGHT | STICK_SLOW_LEFT))) {
        do {
            *arg1 = (*arg1 + 1) % 8;
        } while (D_80206B44_9CC364[*arg1].id == 35);

        auPlaySound(SOUND_ID_65);
    }

    if (arg0->pressedButtons & B_BUTTON) {
        return -1;
    }

    return 0;
}

/* ---- The name entry ----------------------------------------------------
 * A new game's photographer card (src/menu_new_game/A5D5B0.c): the letters
 * in five columns 13 apart from x 25 and nineteen rows 10 apart from y 22
 * (the corner marks frame each at that place), and under them a row of
 * three, two at the columns' places and End at 63. The cursor is two of
 * the file's own words, and its screen loop is the game's below with the
 * mouse added: the letter under the pointer is where the cursor goes, a
 * click enters it (A), the right button is B (a letter back), the wheel
 * steps up and down. */
#define NG_CURSOR_X (*(volatile s32*) 0x80168120)   /* menu_new_game_CursorX */
#define NG_CURSOR_Y (*(volatile s32*) 0x80168124)   /* menu_new_game_CursorY */
#define NG_STATE    (*(volatile u8*) 0x80168144)    /* 2 when the card is done */
#define NG_LETTER   ((u8*) 0x80168140)              /* the letter under the cursor, as text */
void func_800E23B0_A5D760(void);
void func_800E2E00_A5E1B0(void);
void func_800E2A84_A5DE34(u8* text);
void func_800E2590_A5D940(UnkStruct800BEDF8* input);
void func_800E2800_A5DBB0(void);
void func_800E270C_A5DABC(void);
void func_800E2314_A5D6C4(void);
void func_800E2200_A5D5B0(void);

/* The cell under the pointer as y * 8 + x, or -1. */
static s32 snap_name_hit(void) {
    s32 x, y;
    for (y = 0; y < 19; y++) {
        for (x = 0; x < 5; x++) {
            if (snap_mouse_in(25 + 13 * x - 5, 22 + 10 * y - 3, 13, 10)) {
                return y * 8 + x;
            }
        }
    }
    if (snap_mouse_in(20, 22 + 190 - 3, 13, 10)) {
        return 19 * 8 + 0;
    }
    if (snap_mouse_in(33, 22 + 190 - 3, 13, 10)) {
        return 19 * 8 + 1;
    }
    if (snap_mouse_in(58, 22 + 190 - 3, 26, 10)) {
        return 19 * 8 + 2;
    }
    return -1;
}

void func_800E2ED0_A5E280(void) {
    SnapMouse mouse;
    s32 ev, hit;

    func_800E23B0_A5D760();
    func_800E2E00_A5E1B0();
    func_800E2A84_A5DE34(NG_LETTER);
    snap_mouse_begin(&mouse);
    while (1) {
        UnkStruct800BEDF8* temp_v0 = func_800AA38C(0);
        s32 temp_v1 = temp_v0->pressedButtons;

        ev = snap_mouse_take(&mouse);
        hit = snap_name_hit();
        if ((ev & (SNAP_MOUSE_MOVED | SNAP_MOUSE_CLICK)) && (hit >= 0) &&
            (((hit % 8) != NG_CURSOR_X) || ((hit / 8) != NG_CURSOR_Y))) {
            NG_CURSOR_X = hit % 8;
            NG_CURSOR_Y = hit / 8;
            func_800E2200_A5D5B0();
            auPlaySound((NG_CURSOR_Y >= 19) ? SOUND_ID_65 : 69);
            func_800E2A84_A5DE34(NG_LETTER);
        }
        if ((ev & SNAP_MOUSE_CLICK) && (hit >= 0)) {
            temp_v1 |= 0x8000;
        }
        if (ev & SNAP_MOUSE_BACK) {
            temp_v1 |= 0x4000;
        }
        if (ev & SNAP_MOUSE_UP) {
            temp_v1 |= 0x10000;
        }
        if (ev & SNAP_MOUSE_DOWN) {
            temp_v1 |= 0x20000;
        }
        temp_v0->pressedButtons = temp_v1;

        if (temp_v1 & 0xF0000) {
            func_800E2590_A5D940(temp_v0);
            func_800E2A84_A5DE34(NG_LETTER);
        } else if (temp_v1 & 0x4000) {
            func_800E2800_A5DBB0();
            func_800E2A84_A5DE34(NG_LETTER);
        } else if (temp_v1 & 0x8000) {
            func_800E270C_A5DABC();
            if (NG_STATE != 2) {
                func_800E2A84_A5DE34(NG_LETTER);
            }
        } else if (temp_v1 & 0x1000) {
            func_800E2314_A5D6C4();
            func_800E2A84_A5DE34(NG_LETTER);
        }
        if (NG_STATE == 2) {
            break;
        }
        ohWait(1);
    }
}
