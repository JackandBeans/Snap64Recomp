/**
 * The mouse on the game's questions.
 *
 * Oak's "Shall I repeat this information?", the lab's "Shall I save your
 * progress?", the photo check's "Should I show this to Prof. Oak?", the
 * album's "Delete this picture?", the Gallery's "How's this?": each is a
 * text with an A line and a B line, and each screen reads the answer from
 * the frame's input record itself. A click used to be A wherever the
 * pointer was, so the only answer a mouse could give was the first. Now,
 * while a text with both icons is on screen, the answer under the pointer
 * -- its icon and the words after it -- is underlined in yellow, with the
 * move sound as the pointer comes onto it; a click on an answer presses
 * its button (A, B, or Z where a text offers one), a click anywhere else
 * does nothing, and the right button is B. Until 1.1.7 only the icon was a
 * target, and a click on the words did nothing; and the line was the
 * Options pages' orange, which stood out too little on the boxes the
 * questions sit in (2.3 to 1 against the camera check's red, 1.9 to 1
 * against the lab's gray-green; the yellow is 5.3 and 4.4 to 1).
 *
 * The icons are the game's button images (src/window/button.c): one object
 * each, made as the text printer reaches the "\a" or "\b", kept in a list
 * the printer's own clear deletes from, with the icon's letter (its index
 * in the game's string of them) as the object's user data. So a question
 * is on screen exactly when an A image and a B image are both in that
 * list, and each has its screen place. The words are measured from the
 * text box's own bitmap, read where and as the game's printer writes it
 * (src/window/text.c, UIText_PrintChar16 and 32): from the icon to the last
 * column of ink on the icon's own rows, ink being any pixel the box's
 * background fill did not leave, stopping before the next icon on the row
 * (the save question has both answers on one line) and at a blank wider
 * than a word space. A box whose bitmap cannot be read so keeps the icon
 * alone as the target. The underline is drawn into the same bitmap, on
 * its background only (the words' ink on that row stays), and undrawn
 * where it is still yellow; with no bitmap to read, the game's fill draws
 * it under the icon and the box's background color undraws it. No object
 * is made, which a screen whose heap is spent could not give.
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
    /* 0x54 */ u8* image;          /* the bitmap the fill and the printer write */
    /* 0x58 */ u8* altImage;
    /* 0x5C */ s32 imageBytes;
    /* 0x60 */ s32 bpp;            /* bytes a pixel */
    /* 0x64 */ u8 pad_64[0x94];    /* its two sprites, its display list and bitmaps */
    /* 0xF8 */ u8* textImage;      /* UIText: the printer's raster, set by each print */
    /* 0xFC */ s32 textWidth;      /* its row length in pixels */
    /* 0x100 */ s32 textHeight;
    /* 0x104 */ s32 textBpp;
    /* 0x108 */ s32 textUnk10;
    /* 0x10C */ s32 textScreenX;
    /* 0x110 */ s32 textScreenY;
    /* 0x114 */ s32 textFont;      /* FONT_8 or FONT_12 */
} SnapUIElement;
SnapUIElement* UIElement_GetList(void);
void UIElement_FillRect(SnapUIElement* el, s32 x1, s32 y1, s32 x2, s32 y2, s32 r, s32 g, s32 b, s32 a);

/* The box's flags that choose its bitmap's format (src/window/window.h). */
#define SNAP_UI_FLAG_10        0x010
#define SNAP_UI_FLAG_8BIT      0x200
#define SNAP_UI_FLAG_32BIT     0x400
#define SNAP_UI_FLAG_GRAYSCALE 0x800

#define SNAP_Q_NONE 0
#define SNAP_Q_A    1
#define SNAP_Q_B    2
#define SNAP_Q_Z    3

static s32 snap_q_active = 0;             /* a question was on screen last frame */
static s32 snap_q_hover = SNAP_Q_NONE;    /* the line underlined */
static SnapUIElement* snap_q_el = NULL;   /* the box the underline is drawn in */
static s32 snap_q_lx, snap_q_ly, snap_q_lw;
static s32 snap_q_tw;                     /* the target's width when the line was drawn */
static s32 snap_q_direct = 0;             /* the line was laid on the bitmap's background, pixel by pixel */
static u32 snap_q_sel_v, snap_q_bg_v;     /* then: its yellow and that background, as the bitmap holds them */

/* The line's color: the game's own highlight yellow, UIText_YellowColor
 * (src/window/text.c, 255 255 0), the color its printer's "\h" gives
 * text. Not the Options pages' orange (SEL_R/G/B), which stays theirs. */
#define SNAP_Q_LINE_R 0xFF
#define SNAP_Q_LINE_G 0xFF
#define SNAP_Q_LINE_B 0x00
static u8 snap_q_moved, snap_q_click, snap_q_back;

/* The value the game's fill writes for a color in this box's bitmap
 * (UIElement_FillRect, src/window/element.c). */
static u32 snap_q_value(SnapUIElement* el, s32 r, s32 g, s32 b, s32 a) {
    if (el->flags & SNAP_UI_FLAG_32BIT) {
        return ((u32) r << 24) | ((u32) g << 16) | ((u32) b << 8) | (u32) a;
    }
    if (el->flags & SNAP_UI_FLAG_GRAYSCALE) {
        return (el->flags & SNAP_UI_FLAG_10) ? (u32) ((a | 0xFF00) & 0xFFFF) : (u32) (((r << 8) | 0xFF) & 0xFFFF);
    }
    return (u32) (((r << 8) & 0xF800) | ((g * 8) & 0x7C0) | ((b >> 2) & 0x3E) | (a & 1));
}

/* A pixel of the box's text area, where the fill and the printer address
 * it: rows of imageWidth from the bitmap's start, past the border. Macros,
 * so the loops below call nothing: the button filter runs on whichever
 * thread asks for the frame's input, a ride's among them, whose stack is
 * small. */
#define SNAP_Q_AT(el, cx, cy) (((el)->borderHeight + (cy)) * (el)->imageWidth + (el)->borderWidth + (cx))
#define SNAP_Q_GET(el, cx, cy) \
    (((el)->bpp == 4) ? ((u32*) (el)->image)[SNAP_Q_AT(el, cx, cy)] : (u32) ((u16*) (el)->image)[SNAP_Q_AT(el, cx, cy)])
#define SNAP_Q_PUT(el, cx, cy, v)                                 \
    if ((el)->bpp == 4) {                                         \
        ((u32*) (el)->image)[SNAP_Q_AT(el, cx, cy)] = (v);        \
    } else {                                                      \
        ((u16*) (el)->image)[SNAP_Q_AT(el, cx, cy)] = (u16) (v);  \
    }

/* Whether the bitmap can be read as the printer wrote it: 16 or 32 bits a
 * pixel as its flags say, the printer's own raster where the box's text
 * area is, and all of it in RDRAM. */
static s32 snap_q_readable(SnapUIElement* el) {
    u32 lo, hi;

    if ((el->flags & SNAP_UI_FLAG_8BIT) || (el->bpp != ((el->flags & SNAP_UI_FLAG_32BIT) ? 4 : 2))) {
        return 0;
    }
    if ((el->width <= 0) || (el->height <= 0) || (el->imageWidth < el->borderWidth + el->width)) {
        return 0;
    }
    if ((el->textImage != el->image + (el->imageWidth * el->borderHeight + el->borderWidth) * el->bpp) ||
        (el->textWidth != el->imageWidth) || (el->textBpp != el->bpp)) {
        return 0;
    }
    lo = (u32) el->image;
    hi = lo + (u32) ((2 * el->borderHeight + el->height) * el->imageWidth * el->bpp);
    return (lo >= 0x80000000) && (hi > lo) && (hi <= 0x80800000) && ((lo & (u32) (el->bpp - 1)) == 0);
}

/* Where answer i ends on the screen, the last column of its target: the
 * icon's own right edge, or the last column of ink of the words after it
 * on the icon's own rows. The scan stops before the next icon on the row
 * and at a blank wider than a word space (four pixels in the twelve-pixel
 * face, two in the eight: src/window/text.c, UIText_PrintString), taken
 * as two of them for the letters' own margins and shadows; the words must
 * start within the icon's height of it, or there are none. bg is the
 * value of the box's background (snap_q_value). */
static s32 snap_q_words_end(SnapUIElement* el, u32 bg, s32 i, s32* x, s32* y, s32* w, s32* h) {
    s32 ox = el->x + el->borderWidth;
    s32 oy = el->y + el->borderHeight;
    s32 stop, gap, cx, cy, cy0, cy1, j, last, blank;

    stop = el->width;
    for (j = 0; j < 3; j++) {
        if ((j != i) && (x[j] > x[i]) && (y[j] == y[i]) && (x[j] - ox < stop)) {
            stop = x[j] - ox;
        }
    }
    gap = (el->textFont == 12) ? 8 : 4;
    cy0 = y[i] - oy;
    cy1 = cy0 + h[i];
    if (cy0 < 0) {
        cy0 = 0;
    }
    if (cy1 > el->height) {
        cy1 = el->height;
    }
    last = -1;
    blank = 0;
    for (cx = x[i] + w[i] - ox; (cx < stop) && (cy0 < cy1); cx++) {
        if (cx < 0) {
            continue;
        }
        for (cy = cy0; cy < cy1; cy++) {
            if (SNAP_Q_GET(el, cx, cy) != bg) {
                break;
            }
        }
        if (cy < cy1) {
            last = cx;
            blank = 0;
        } else if (++blank > ((last < 0) ? h[i] : gap)) {
            break;
        }
    }
    return (last < 0) ? (x[i] + w[i] - 1) : (ox + last);
}

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

/* The line's pixels that are still its yellow go back to the background
 * they were laid on; a pixel the printer wrote since is left as it is. */
static void snap_q_unline(SnapUIElement* el) {
    s32 cx;

    for (cx = snap_q_lx; cx < snap_q_lx + snap_q_lw; cx++) {
        if (SNAP_Q_GET(el, cx, snap_q_ly) == snap_q_sel_v) {
            SNAP_Q_PUT(el, cx, snap_q_ly, snap_q_bg_v);
        }
    }
}

static void snap_q_undraw(void) {
    if (snap_q_el != NULL) {
        if (snap_q_direct) {
            snap_q_unline(snap_q_el);
        } else {
            UIElement_FillRect(snap_q_el, snap_q_lx, snap_q_ly, snap_q_lx + snap_q_lw - 1, snap_q_ly, snap_q_el->bg[0],
                               snap_q_el->bg[1], snap_q_el->bg[2], snap_q_el->bg[3]);
        }
        snap_q_el = NULL;
    }
    snap_q_hover = SNAP_Q_NONE;
}

/* The line under answer i, one pixel below its icon, as wide as its target
 * (tw). On a readable bitmap it is laid on the background only, inside the
 * box as the fill would clamp it: the words' ink on that row (a letter's
 * shadow) stays over it. */
static void snap_q_line(SnapUIElement* el, s32 readable, s32 i, s32* x, s32* y, s32* tw, s32* h) {
    s32 cx, end;

    snap_q_el = el;
    snap_q_direct = readable;
    snap_q_tw = tw[i];
    snap_q_lx = x[i] - (el->x + el->borderWidth);
    snap_q_ly = y[i] - (el->y + el->borderHeight) + h[i] + 1;
    snap_q_lw = tw[i];
    if (!readable) {
        UIElement_FillRect(el, snap_q_lx, snap_q_ly, snap_q_lx + snap_q_lw - 1, snap_q_ly, SNAP_Q_LINE_R, SNAP_Q_LINE_G,
                           SNAP_Q_LINE_B, 0xFF);
        return;
    }
    if (snap_q_ly < 0) {
        snap_q_ly = 0;
    } else if (snap_q_ly >= el->height) {
        snap_q_ly = el->height - 1;
    }
    end = snap_q_lx + snap_q_lw;
    if (snap_q_lx < 0) {
        snap_q_lx = 0;
    }
    if (end > el->width) {
        end = el->width;
    }
    snap_q_lw = (end > snap_q_lx) ? (end - snap_q_lx) : 0;
    snap_q_sel_v = snap_q_value(el, SNAP_Q_LINE_R, SNAP_Q_LINE_G, SNAP_Q_LINE_B, 0xFF);
    snap_q_bg_v = snap_q_value(el, el->bg[0], el->bg[1], el->bg[2], el->bg[3]);
    for (cx = snap_q_lx; cx < end; cx++) {
        if (SNAP_Q_GET(el, cx, snap_q_ly) == snap_q_bg_v) {
            SNAP_Q_PUT(el, cx, snap_q_ly, snap_q_sel_v);
        }
    }
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

/* A question on screen, in box el with its icons at x, y, w, h: the
 * answers' targets, the mouse's events, the underline, and the buttons a
 * click lays into the frame's record. Apart from snap_question_buttons,
 * which runs every frame on whichever thread asks for the input, a ride's
 * small stack among them: this one's frame is taken only while a question
 * is up. */
static s32 snap_q_answer(SnapUIElement* el, s32* x, s32* y, s32* w, s32* h) {
    s32 tw[3];
    s32 i, hit, ev, extra, readable;
    u32 bg;
    u8 v;

    /* An answer's target is its icon and the words after it, with two
     * pixels to spare, as the Options pages' A and B have. */
    readable = snap_q_readable(el);
    bg = readable ? snap_q_value(el, el->bg[0], el->bg[1], el->bg[2], el->bg[3]) : 0;
    hit = SNAP_Q_NONE;
    for (i = 0; i < 3; i++) {
        tw[i] = 0;
        if (x[i] >= 0) {
            tw[i] = readable ? (snap_q_words_end(el, bg, i, x, y, w, h) - x[i] + 1) : w[i];
            if (snap_mouse_in(x[i] - 2, y[i] - 2, tw[i] + 4, h[i] + 4)) {
                hit = i + 1;
            }
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
            snap_q_line(el, readable, hit - 1, x, y, tw, h);
            snap_q_hover = hit;
            snap_ui_sound(SND_MOVE);
        }
    } else if ((snap_q_hover != SNAP_Q_NONE) && (snap_q_el == el) && (x[snap_q_hover - 1] >= 0) &&
               (tw[snap_q_hover - 1] != snap_q_tw)) {
        /* The answer's words are still being printed: the line grows with
         * them, without a sound. */
        i = snap_q_hover;
        snap_q_undraw();
        snap_q_line(el, readable, i - 1, x, y, tw, h);
        snap_q_hover = i;
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

/* The question's mouse, once a frame: the buttons laid into the frame's
 * record for it. */
static s32 snap_question_buttons(void) {
    SnapIconEntry* e;
    s32 ia, ib, iz, n, i, k;
    s32 x[3], y[3], w[3], h[3];
    SnapUIElement* el;

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
    return snap_q_answer(el, x, y, w, h);
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
