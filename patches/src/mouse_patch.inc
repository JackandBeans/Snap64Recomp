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
 * steps up and down.
 *
 * The keyboard types on it as well (src/input.cpp): the card claims the
 * keys that type every frame it is up, and the host hands over what was
 * typed one character at a time. A character the card has is entered as if
 * the cursor were moved onto it and A pressed, so the cursor follows the
 * typing and a full card goes to End as it does; Space is the card's right
 * arrow (a space), Backspace its left arrow (a letter back), Enter its End.
 * A character the card lacks is passed over. */
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
void func_800E2758_A5DB08(void);
u8** func_800E18B4_A5CC64(s32 arg0);
#define NG_LENGTH   (*(volatile u8*) 0x80168143)    /* the name's bytes so far, two a letter, 14 full */
#define TYPE_CLAIM  (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x9D))
#define TYPE_CHAR   (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x9E))

/* The card's two-byte code (EUC-JP's full-width forms, as its table holds
 * them) for a typed character, or 0. */
static u16 snap_type_code(u8 c) {
    static const char sym[] = "!?@#%&*()_+=-'\";:/$.,<>[]~";
    static const u16 code[] = {
        0xA1AA, 0xA1A9, 0xA1F7, 0xA1F4, 0xA1F3, 0xA1F5, 0xA1F6, 0xA1CA, 0xA1CB,
        0xA1B2, 0xA1DC, 0xA1E1, 0xA1BC, 0xA1C7, 0xA1C9, 0xA1A8, 0xA1A7, 0xA1BF,
        0xA1F0, 0xA1A5, 0xA1A4, 0xA1E3, 0xA1E4, 0xA1CE, 0xA1CF, 0xA1C1,
    };
    s32 i;

    if ((c >= 'A') && (c <= 'Z')) {
        return 0xA3C1 + (c - 'A');
    }
    if ((c >= 'a') && (c <= 'z')) {
        return 0xA3E1 + (c - 'a');
    }
    if ((c >= '0') && (c <= '9')) {
        return 0xA3B0 + (c - '0');
    }
    if (c == 0xE9) {
        return 0xA6C5;   /* e acute: the card's epsilon, which its font draws as one */
    }
    for (i = 0; sym[i] != 0; i++) {
        if ((u8) sym[i] == c) {
            return code[i];
        }
    }
    return 0;
}

/* One typed character on the card. */
static void snap_name_type(u8 c) {
    u8** table;
    u16 code;
    s32 i;

    if (c == 0x08) {
        func_800E2800_A5DBB0();
        func_800E2A84_A5DE34(NG_LETTER);
        return;
    }
    if (c == 0x0D) {
        /* End, and A on it: the name is kept and the card is done. */
        NG_CURSOR_X = 2;
        NG_CURSOR_Y = 19;
        func_800E2200_A5D5B0();
        func_800E270C_A5DABC();
        return;
    }
    if (c == ' ') {
        func_800E2758_A5DB08();
        func_800E2A84_A5DE34(NG_LETTER);
        return;
    }
    code = snap_type_code(c);
    if (code == 0) {
        return;
    }
    table = func_800E18B4_A5CC64(0);
    for (i = 0; i < 95; i++) {
        if ((table[i][0] == (code >> 8)) && (table[i][1] == (code & 0xFF))) {
            break;
        }
    }
    if (i == 95) {
        return;
    }
    if (NG_LENGTH >= 14) {
        func_800E2314_A5D6C4();
        func_800E2A84_A5DE34(NG_LETTER);
        return;
    }
    NG_CURSOR_X = i % 5;
    NG_CURSOR_Y = i / 5;
    func_800E2200_A5D5B0();
    func_800E2A84_A5DE34(NG_LETTER);
    func_800E270C_A5DABC();
    func_800E2A84_A5DE34(NG_LETTER);
}

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
    u8 typed;

    func_800E23B0_A5D760();
    func_800E2E00_A5E1B0();
    func_800E2A84_A5DE34(NG_LETTER);
    snap_mouse_begin(&mouse);
    TYPE_CHAR = 0;
    while (1) {
        UnkStruct800BEDF8* temp_v0 = func_800AA38C(0);
        s32 temp_v1 = temp_v0->pressedButtons;

        TYPE_CLAIM = 4;
        typed = TYPE_CHAR;
        if (typed != 0) {
            TYPE_CHAR = 0;
            snap_name_type(typed);
            if (NG_STATE == 2) {
                break;
            }
        }

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
        /* The D-pad (its N64 bits: up 0x0800, down 0x0400, left 0x0200,
         * right 0x0100) moves on the grid as the stick does: the keyboard's
         * arrows are the D-pad, and its W, A, S and D type here. */
        if (temp_v1 & 0x0800) {
            temp_v1 |= STICK_SLOW_UP;
        }
        if (temp_v1 & 0x0400) {
            temp_v1 |= STICK_SLOW_DOWN;
        }
        if (temp_v1 & 0x0200) {
            temp_v1 |= STICK_SLOW_LEFT;
        }
        if (temp_v1 & 0x0100) {
            temp_v1 |= STICK_SLOW_RIGHT;
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
    TYPE_CLAIM = 0;
    TYPE_CHAR = 0;
}
