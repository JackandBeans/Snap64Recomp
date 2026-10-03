/**
 * The mouse on the game's questions.
 *
 * Oak's "Shall I repeat this information?", the lab's "Shall I save your
 * progress?", the photo check's "Should I show this to Prof. Oak?", the
 * album's "Delete this picture?", the Gallery's "How's this?": each is a
 * text with an A line and a B line, and each screen reads the answer from
 * the frame's input record itself. A click used to be A wherever the
 * pointer was, so the only answer a mouse could give was the first. Now,
 * while a text with both icons is on screen, the icon under the pointer is
 * underlined in orange, with the move sound as the pointer comes onto it;
 * a click on an icon presses its button (A, B, or Z where a text offers
 * one), a click anywhere else, the words beside the icons included, does
 * nothing, and the right button is B.
 *
 * The icons are the game's button images (src/window/button.c): one object
 * each, made as the text printer reaches the "\a" or "\b", kept in a list
 * the printer's own clear deletes from, with the icon's letter (its index
 * in the game's string of them) as the object's user data. So a question
 * is on screen exactly when an A image and a B image are both in that
 * list, and each has its screen place. The underline is drawn into the
 * text box's own bitmap with the game's fill, and undrawn with the box's
 * background colour; no object is made, which a screen whose heap is spent
 * could not give.
 *
 * This runs in the frame's button filter (src/app_render/53AD0.c,
 * func_800AA28C): the record builder passes each controller's raw buttons
 * through it once a tick, and what it returns is the frame's
 * currentButtons, from which every screen's pressedButtons derive (and the
 * lab's wait reads currentButtons too). Replaced as the game wrote it,
 * with the question's answer laid into controller 0's result. Its two
 * counters are statics of the game's file, reached at their addresses.
 *
 * Compiled as part of graphics_menu_patch.c (included at its end), which
 * holds the mouse helpers.
 */

extern s32 D_800AF3AC;   /* the buttons that repeat while held */
extern s32 D_800AF3A4;   /* frames held before the first repeat */
extern s32 D_800AF3A8;   /* frames between repeats */
#define D_800BE2F0 ((s32 (*)[24]) 0x800BE2F0)   /* static s32 D_800BE2F0[4][24]: frames held */
#define D_800BE470 ((s32 (*)[24]) 0x800BE470)   /* static s32 D_800BE470[4][24]: the repeat count */

typedef struct SnapIconEntry {
    struct SnapIconEntry* next;
    struct SnapIconEntry* prev;
    GObj* obj;
} SnapIconEntry;
extern SnapIconEntry* D_80380BEC_85439C;   /* the button images' list: the sentinel's obj is NULL */
extern char D_80380BD4_854384[];           /* their letters; an image's userData is its letter's index */

/* The text box (src/window/window.h, UIElement), the fields used here. */
typedef struct SnapUIElement {
    /* 0x00 */ s32 x;
    /* 0x04 */ s32 y;
    /* 0x08 */ s32 borderWidth;
    /* 0x0C */ s32 borderHeight;
    /* 0x10 */ s32 width;
    /* 0x14 */ s32 height;
    /* 0x18 */ u32 flags;
    /* 0x1C */ u8 pad_1C[0x20];
    /* 0x3C */ struct SnapUIElement* next;
    /* 0x40 */ struct SnapUIElement* prev;
    /* 0x44 */ s32 imageWidth;
    /* 0x48 */ u8 fg[4];
    /* 0x4C */ u8 bg[4];
    /* 0x50 */ GObj* spriteObj;
} SnapUIElement;
SnapUIElement* UIElement_GetList(void);
void UIElement_FillRect(SnapUIElement* el, s32 x1, s32 y1, s32 x2, s32 y2, s32 r, s32 g, s32 b, s32 a);

#define SNAP_Q_NONE 0
#define SNAP_Q_A    1
#define SNAP_Q_B    2
#define SNAP_Q_Z    3

static s32 snap_q_active = 0;             /* a question was on screen last frame */
static s32 snap_q_hover = SNAP_Q_NONE;    /* the line underlined */
static SnapUIElement* snap_q_el = NULL;   /* the box the underline is drawn in */
static s32 snap_q_lx, snap_q_ly, snap_q_lw;
static u8 snap_q_moved, snap_q_click, snap_q_back;

/* The index of a letter in the game's string of button images, or -1. */
static s32 snap_q_letter(char c) {
    s32 i;
    for (i = 0; (i < 32) && (D_80380BD4_854384[i] != 0); i++) {
        if (D_80380BD4_854384[i] == c) {
            return i;
        }
    }
    return -1;
}

static void snap_q_undraw(void) {
    if (snap_q_el != NULL) {
        UIElement_FillRect(snap_q_el, snap_q_lx, snap_q_ly, snap_q_lx + snap_q_lw - 1, snap_q_ly, snap_q_el->bg[0],
                           snap_q_el->bg[1], snap_q_el->bg[2], snap_q_el->bg[3]);
        snap_q_el = NULL;
    }
    snap_q_hover = SNAP_Q_NONE;
}

/* No question, or the pages over it: the underline goes, when its box is
 * still there to draw in (the pages hold the screen; a question's own end
 * clears its box first). */
static void snap_q_leave(s32 boxStands) {
    if (boxStands) {
        snap_q_undraw();
    }
    snap_q_el = NULL;
    snap_q_hover = SNAP_Q_NONE;
    snap_q_active = 0;
}

/* The box an icon is printed in: the one whose rectangle holds it, as the
 * printer's own clear finds the icons it deletes. */
static SnapUIElement* snap_q_box(s32 x, s32 y) {
    SnapUIElement* el = UIElement_GetList();
    s32 n;
    for (n = 0; (el != NULL) && (n < 64); n++, el = el->next) {
        if ((el->spriteObj != NULL) && (el->spriteObj->data.sobj != NULL) &&
            (el->spriteObj->data.sobj->sprite.attr & SP_HIDDEN)) {
            continue;
        }
        if ((x >= el->x) && (x <= el->x + el->width) && (y >= el->y) && (y <= el->y + el->height)) {
            return el;
        }
    }
    return NULL;
}

/* The question's mouse, once a frame: the buttons laid into the frame's
 * record for it. */
static s32 snap_question_buttons(void) {
    SnapIconEntry* e;
    s32 ia, ib, iz, n, i, k, hit, ev, extra;
    s32 x[3], y[3], w[3], h[3];
    SnapUIElement* el;
    u8 v;

    if (MBOX_PAGES_OPEN != 0) {
        snap_q_leave(1);
        return 0;
    }
    if ((D_80380BEC_85439C == NULL) || (MBOX_SCENE_AGE < 30)) {
        snap_q_leave(0);
        return 0;
    }
    ia = snap_q_letter('a');
    ib = snap_q_letter('b');
    iz = snap_q_letter('z');
    x[0] = x[1] = x[2] = -1;
    for (e = D_80380BEC_85439C->next, n = 0; (e != NULL) && (e->obj != NULL) && (n < 32); e = e->next, n++) {
        GObj* obj = e->obj;
        SObj* so;
        Sprite* sp;
        if (((u32) obj < 0x80000000) || ((u32) obj >= 0x80800000)) {
            break;
        }
        so = obj->data.sobj;
        if ((so == NULL) || (so->sprite.attr & SP_HIDDEN)) {
            continue;
        }
        sp = &so->sprite;
        k = (s32) obj->userData;
        i = (k == ia) ? 0 : (k == ib) ? 1 : (k == iz) ? 2 : -1;
        if ((i < 0) || (x[i] >= 0) || (sp->width <= 0) || (sp->width > 64)) {
            continue;
        }
        x[i] = sp->x;
        y[i] = sp->y;
        w[i] = sp->width;
        h[i] = sp->height;
        if (sp->attr & SP_SCALE) {
            w[i] = (s32) (w[i] * sp->scalex);
            h[i] = (s32) (h[i] * sp->scaley);
        }
    }
    if ((x[0] < 0) || (x[1] < 0)) {
        snap_q_leave(0);
        return 0;
    }
    el = snap_q_box(x[0], y[0]);
    if ((el == NULL) || (el != snap_q_box(x[1], y[1]))) {
        snap_q_leave(0);
        return 0;
    }

    /* The icon itself is the target, with two pixels to spare, as the
     * Options pages' A and B are: the words beside it are not. */
    hit = SNAP_Q_NONE;
    for (i = 0; i < 3; i++) {
        if ((x[i] >= 0) && snap_mouse_in(x[i] - 2, y[i] - 2, w[i] + 4, h[i] + 4)) {
            hit = i + 1;
        }
    }

    MOUSE_CLAIM = 4;
    ev = 0;
    if (!snap_q_active) {
        /* The question's first frame: what the mouse did before it is not
         * its to answer. */
        snap_q_active = 1;
        snap_q_moved = MOUSE_MOVED;
        snap_q_click = MOUSE_CLICK;
        snap_q_back = MOUSE_BACK;
    }
    if ((v = MOUSE_MOVED) != snap_q_moved) {
        snap_q_moved = v;
        ev |= SNAP_MOUSE_MOVED;
    }
    if ((v = MOUSE_CLICK) != snap_q_click) {
        snap_q_click = v;
        ev |= SNAP_MOUSE_CLICK;
    }
    if ((v = MOUSE_BACK) != snap_q_back) {
        snap_q_back = v;
        ev |= SNAP_MOUSE_BACK;
    }

    /* The underline follows a move (a pointer resting where a question
     * appears makes no sound), and a click. */
    if ((ev & (SNAP_MOUSE_MOVED | SNAP_MOUSE_CLICK)) && (hit != snap_q_hover)) {
        snap_q_undraw();
        if (hit != SNAP_Q_NONE) {
            i = hit - 1;
            snap_q_el = el;
            snap_q_lx = x[i] - (el->x + el->borderWidth);
            snap_q_ly = y[i] - (el->y + el->borderHeight) + h[i] + 1;
            snap_q_lw = w[i];
            UIElement_FillRect(el, snap_q_lx, snap_q_ly, snap_q_lx + snap_q_lw - 1, snap_q_ly, SEL_R, SEL_G, SEL_B, 0xFF);
            snap_q_hover = hit;
            snap_ui_sound(SND_MOVE);
        }
    }

    extra = 0;
    if ((ev & SNAP_MOUSE_CLICK) && (hit != SNAP_Q_NONE)) {
        extra |= (hit == SNAP_Q_A) ? A_BUTTON : (hit == SNAP_Q_B) ? B_BUTTON : 0x2000;
    }
    if (ev & SNAP_MOUSE_BACK) {
        extra |= B_BUTTON;
    }
    return extra;
}

/* Stock (src/app_render/53AD0.c), with the question's answer laid into
 * controller 0's buttons after the repeat filter, so a click is one press. */
s32 func_800AA28C(s32 buttons, s32 contId) {
    s32 i;
    s32 v1 = 0;
    s32 mask = 1;

    for (i = 0; i < 24; i++, mask *= 2) {
        if (!(D_800AF3AC & mask)) {
            v1 |= mask & buttons;
        } else if (!(mask & buttons)) {
            D_800BE2F0[contId][i] = 0;
        } else if (D_800BE2F0[contId][i] < D_800AF3A4) {
            D_800BE2F0[contId][i]++;
            D_800BE470[contId][i] = D_800AF3A8;
            v1 |= buttons & mask;
        } else if (D_800BE470[contId][i] >= D_800AF3A8) {
            D_800BE470[contId][i] = 0;
        } else {
            D_800BE470[contId][i]++;
            v1 |= mask;
        }
    }
    if (contId == 0) {
        v1 |= snap_question_buttons();
    }
    return v1;
}
