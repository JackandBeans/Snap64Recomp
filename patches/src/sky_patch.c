/**
 * @file sky_patch.c
 * @brief The sky's drum is drawn finer, so its joins do not show.
 *
 * Each course's sky is a six-sided drum around the camera (decomp
 * src/world/block.c, createSkyBox): six slanted faces from a little below
 * the horizon to 23 degrees up, each face showing the whole 64x32 sky
 * picture, a flat six-sided lid, and a skirt below the horizon; 36 corners
 * in all, each with a color of its own, white toward the land and a deep
 * blue toward the sea. On the console at 320x240 the drum reads as a sky.
 * At the port's resolution three kinds of line stand out on it: the
 * picture's last column meets its first at every join, and the two differ;
 * the corner colors change pace at the joins, and the eye sees the crease
 * where a color's slope breaks; and the lid's rim shows as a ceiling where
 * the faces' gradient meets the lid's flat color.
 *
 * The two functions that draw a sky (drawSkyBox1Cycle, drawSkyBox2Cycle)
 * are replaced. On the first draw of a course's sky its display list is
 * read: its corners, its triangles, and its picture. Each triangle is cut
 * into sixteen, with the picture's coordinates carried straight (the
 * picture lands exactly where it did), and each new corner takes a color
 * that varies smoothly with direction: around the drum a periodic cubic
 * through the game's own corner colors (Catmull-Rom), up the drum a cubic
 * between the rings that flattens at the lid, and across the lid a blend
 * toward the top's mean. The game's corner colors are read again on every
 * frame, so whatever paints them (a mod's night) paints the fine drum too.
 * The picture's columns either side of each join dissolve into its own
 * clearest stretch, once per load, so the joins no longer cut it. The
 * game's own triangles are turned into no-ops, its list is still run after
 * the fine drum (a mod's additions to it, and its picture state, stay), and
 * nothing else of the sky changes: same picture, same corners' colors, same
 * place in the frame, same render modes.
 *
 * A sky whose list is not a ring drum (rings of corners at equal azimuth
 * steps, the same steps on every ring) is left exactly as it was. The host
 * byte at 0x80C0004E, set from SNAP_SKY_DRUM=original, keeps every sky as
 * the cartridge draws it, for comparison. The word at 0x80C000A8 reports
 * what was found: the triangles, rings and nodes, or a reason it was left.
 */
#include "common.h"

/* Host-owned: 1 keeps the cartridge's drum (SNAP_SKY_DRUM=original); 2 draws
 * the fine drum and paints the picture's two edge columns red, so the joins
 * can be seen for what they are (SNAP_SKY_DRUM=mark, a test). */
#define SKY_DRUM_MODE (*(volatile u8*) 0x80C0004E)
#define SKY_KEEP_DRUM (SKY_DRUM_MODE == 1)
/* The patch's own report: triangles << 16 | rings << 8 | nodes, or
 * 0xFFFF0000 | reason when the sky was left alone. */
#define SKY_REPORT (*(volatile u32*) 0x80C000A8)

#define SKY_MAX_TRIS 64         /* triangles a sky list may have */
#define SKY_DIV 4               /* each edge is cut in four */
#define SKY_SUB_VTX 15          /* corners of a cut triangle: (4+1)(4+2)/2 */
#define SKY_SUB_TRI2 8          /* its sixteen triangles, two a command */
#define SKY_MAX_RINGS 4
#define SKY_MAX_NODES 8
#define SKY_MAX_STATE 128       /* commands of the list that are not geometry */
#define SKY_LIST_LEN (SKY_MAX_TRIS * (1 + SKY_SUB_TRI2) + SKY_MAX_STATE + 2)
#define SKY_MAX_CORNERS 72      /* distinct corners of the drum */
#define SKY_MAX_PICS 4
#define SKY_HIDDEN_MARK 0x534B5921u     /* 'SKY!': the second word of a triangle turned no-op */

/* The reasons a sky is left as it is. */
#define SKY_LEFT_EMPTY 1        /* no triangles, or none with a corner */
#define SKY_LEFT_LONG 2         /* too many triangles or state commands */
#define SKY_LEFT_SHAPE 3        /* a matrix or a branch the reader does not follow */
#define SKY_LEFT_RINGS 4        /* the corners are not rings at equal steps */

typedef struct SkyHidden {
    Gfx* at;
    u32 w0;
    u32 w1;
} SkyHidden;

static Vtx sky_vtx[2][SKY_MAX_TRIS * SKY_SUB_VTX];
static Gfx sky_list[2][SKY_LIST_LEN];
static Gfx sky_entry[2][2];                     /* our drum, then the game's list */
static f32 sky_az[SKY_MAX_TRIS * SKY_SUB_VTX];  /* each new corner's azimuth, degrees */
static f32 sky_y[SKY_MAX_TRIS * SKY_SUB_VTX];   /* its height */
static f32 sky_sin2[SKY_MAX_TRIS * SKY_SUB_VTX];/* the square of the sine of its elevation */
static Vtx* sky_node[SKY_MAX_RINGS][SKY_MAX_NODES];     /* the game's corner at each ring and node */
static f32 sky_ring_y[SKY_MAX_RINGS];
static f32 sky_top_sin2;
static s32 sky_rings, sky_nodes;
static f32 sky_az0, sky_step;
static SkyHidden sky_hidden[SKY_MAX_TRIS];
static s32 sky_hidden_n;
static Gfx* sky_source;                 /* the game's list the drum was built from */
static u32 sky_source_sig;              /* its first two commands, to tell a reuse of the address */
static s32 sky_sub_n;                   /* new corners in the drum */
static s32 sky_tris;
static u32 sky_sum[2];                  /* the corner colors each set was painted from */
static u8 sky_turn, sky_ready, sky_failed;

/* The azimuth, in degrees, of a direction in the horizontal plane. The
 * game's own atan2f cannot be called from a patch (its name is the C
 * library's too, and the recompiled patch would clash with it), so this
 * is a polynomial of its own, good to a hundredth of a degree. */
static f32 sky_atan_unit(f32 z) {
    f32 z2 = z * z;

    return z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f + z2 * (0.05265332f + z2 * -0.01172120f)))));
}

static f32 sky_azimuth(f32 x, f32 z) {
    f32 ax = x < 0.0f ? -x : x;
    f32 az = z < 0.0f ? -z : z;
    f32 a;

    if (ax < 0.001f && az < 0.001f) {
        return 0.0f;
    }
    /* atan2(x, z): the angle from +z toward +x. */
    if (az >= ax) {
        a = sky_atan_unit(ax / az);
    } else {
        a = 1.57079633f - sky_atan_unit(az / ax);
    }
    if (z < 0.0f) {
        a = 3.14159265f - a;
    }
    if (x < 0.0f) {
        a = -a;
    }
    return a * (180.0f / 3.14159265f);
}

/* --------------------------------------------------------------------- */
/* Reading the game's list.                                              */

static s16 sky_round(f32 v) {
    return (s16) (v >= 0.0f ? v + 0.5f : v - 0.5f);
}

/* Cuts one of the game's triangles into sixteen, into both sets. */
static s32 sky_cut(Vtx* a, Vtx* b, Vtx* c, Gfx** out0, Gfx** out1) {
    s32 base = sky_sub_n;
    s32 i, j, k, n;
    s32 idx[SKY_DIV + 1][SKY_DIV + 1];

    if (sky_tris >= SKY_MAX_TRIS) {
        return 0;
    }
    n = 0;
    for (j = 0; j <= SKY_DIV; j++) {
        for (i = 0; i + j <= SKY_DIV; i++) {
            f32 u = (f32) i / (f32) SKY_DIV;
            f32 v = (f32) j / (f32) SKY_DIV;
            f32 w = 1.0f - u - v;
            f32 x = w * a->v.ob[0] + u * b->v.ob[0] + v * c->v.ob[0];
            f32 y = w * a->v.ob[1] + u * b->v.ob[1] + v * c->v.ob[1];
            f32 z = w * a->v.ob[2] + u * b->v.ob[2] + v * c->v.ob[2];
            f32 len2 = x * x + y * y + z * z;
            Vtx* p;

            idx[i][j] = n;
            for (k = 0; k < 2; k++) {
                p = &sky_vtx[k][base + n];
                p->v.ob[0] = sky_round(x);
                p->v.ob[1] = sky_round(y);
                p->v.ob[2] = sky_round(z);
                p->v.flag = 0;
                p->v.tc[0] = sky_round(w * a->v.tc[0] + u * b->v.tc[0] + v * c->v.tc[0]);
                p->v.tc[1] = sky_round(w * a->v.tc[1] + u * b->v.tc[1] + v * c->v.tc[1]);
                p->v.cn[0] = p->v.cn[1] = p->v.cn[2] = 255;
                p->v.cn[3] = 255;
            }
            sky_az[base + n] = sky_azimuth(x, z);
            sky_y[base + n] = y;
            sky_sin2[base + n] = (len2 > 1.0f) ? (y * y) / len2 : 0.0f;
            n++;
        }
    }
    gSPVertex((*out0)++, &sky_vtx[0][base], SKY_SUB_VTX, 0);
    gSPVertex((*out1)++, &sky_vtx[1][base], SKY_SUB_VTX, 0);
    {
        s32 t[SKY_DIV * SKY_DIV][3];
        s32 m = 0;

        for (j = 0; j < SKY_DIV; j++) {
            for (i = 0; i + j < SKY_DIV; i++) {
                t[m][0] = idx[i][j];
                t[m][1] = idx[i + 1][j];
                t[m][2] = idx[i][j + 1];
                m++;
                if (i + j < SKY_DIV - 1) {
                    t[m][0] = idx[i + 1][j];
                    t[m][1] = idx[i + 1][j + 1];
                    t[m][2] = idx[i][j + 1];
                    m++;
                }
            }
        }
        for (m = 0; m < SKY_DIV * SKY_DIV; m += 2) {
            gSP2Triangles((*out0)++, t[m][0], t[m][1], t[m][2], 0, t[m + 1][0], t[m + 1][1], t[m + 1][2], 0);
            gSP2Triangles((*out1)++, t[m][0], t[m][1], t[m][2], 0, t[m + 1][0], t[m + 1][1], t[m + 1][2], 0);
        }
    }
    sky_sub_n += n;
    sky_tris++;
    return 1;
}

/* The picture meets itself at every join of the drum, its last column
 * against its first, and the two do not agree: on the Beach they differ by
 * up to eleven of 255 on some rows, and a cloud begins at the first column,
 * so each join stood as a vertical cloud edge at the port's resolution.
 * Here the ten columns either side of the join dissolve into the picture's
 * own clearest stretch (the twenty columns with the least cloud, found by
 * their brightness), laid across the join so the content runs on: at the
 * join itself both sides show the stretch's middle column, so no step is
 * left there at all.
 * RGBA16 only, and once per picture: a table of pictures already done, by
 * address and checksum, so a second build of the same list leaves it. */
#define SKY_DISSOLVE 10

typedef struct SkyPicDone {
    u32 addr;
    u32 sum;
} SkyPicDone;

static SkyPicDone sky_pic_done[SKY_MAX_PICS];
static u16 sky_pic_copy[256];       /* one row of the picture as it was */

static u32 sky_picture_sum(volatile u16* pic, s32 w, s32 h) {
    u32 sum = 0;
    s32 i;

    for (i = 0; i < w * h; i++) {
        sum = sum * 33 + pic[i];
    }
    return sum;
}

static void sky_dissolve_picture(u32 addr, s32 w, s32 h) {
    volatile u16* pic = (volatile u16*) addr;
    s32 x, y, k, o, best_o;
    u32 best;
    u32 sum;

    if (w < 4 * SKY_DISSOLVE || w > 256 || h < 1 || h > 256 || addr < 0x80000000u || addr >= 0x80800000u) {
        return;
    }
    sum = sky_picture_sum(pic, w, h);
    for (k = 0; k < SKY_MAX_PICS; k++) {
        if (sky_pic_done[k].addr == addr && sky_pic_done[k].sum == sum) {
            return;         /* this picture, as it already is */
        }
    }
    /* The clearest stretch of 2*SKY_DISSOLVE columns, clear of the join's
     * own zone: the darkest, since cloud is brighter than sky. */
    best = 0xFFFFFFFFu;
    best_o = SKY_DISSOLVE;
    for (o = SKY_DISSOLVE; o + 2 * SKY_DISSOLVE <= w - SKY_DISSOLVE; o++) {
        u32 light = 0;

        for (y = 0; y < h; y++) {
            for (x = o; x < o + 2 * SKY_DISSOLVE; x++) {
                u16 t = pic[y * w + x];

                light += ((t >> 11) & 31) + ((t >> 6) & 31) + ((t >> 1) & 31);
            }
        }
        if (light < best) {
            best = light;
            best_o = o;
        }
    }
    for (y = 0; y < h; y++) {
        volatile u16* row = &pic[y * w];
        s32 d;

        for (x = 0; x < w; x++) {
            sky_pic_copy[x] = row[x];
        }
        for (d = 0; d < SKY_DISSOLVE; d++) {
            /* The weight: 1 at the join, 0 at the zone's far edge, eased. */
            s32 wq = ((SKY_DISSOLVE - d) * 256) / SKY_DISSOLVE;
            s32 side;

            wq = (wq * wq * (3 * 256 - 2 * wq)) / (256 * 256);
            for (side = 0; side < 2; side++) {
                s32 at = side ? (w - 1 - d) : d;                                /* the column in the join's zone */
                s32 from = side ? (best_o + SKY_DISSOLVE - d) : (best_o + SKY_DISSOLVE + d);  /* its column of the stretch */
                u16 t0 = sky_pic_copy[at], t1 = sky_pic_copy[from];
                s32 c0[3], c1[3], c[3];

                c0[0] = (t0 >> 11) & 31; c0[1] = (t0 >> 6) & 31; c0[2] = (t0 >> 1) & 31;
                c1[0] = (t1 >> 11) & 31; c1[1] = (t1 >> 6) & 31; c1[2] = (t1 >> 1) & 31;
                for (k = 0; k < 3; k++) {
                    c[k] = c0[k] + (((c1[k] - c0[k]) * wq + 128) >> 8);
                    c[k] = c[k] < 0 ? 0 : c[k] > 31 ? 31 : c[k];
                }
                row[at] = (u16) ((c[0] << 11) | (c[1] << 6) | (c[2] << 1) | (t0 & 1));
            }
        }
    }
    if (SKY_DRUM_MODE == 2) {
        for (y = 0; y < h; y++) {
            pic[y * w] = (u16) ((31 << 11) | 1);
            pic[y * w + w - 1] = (u16) ((31 << 11) | 1);
        }
    }
    /* Remembered, in the oldest slot. */
    for (k = 0; k < SKY_MAX_PICS; k++) {
        if (sky_pic_done[k].addr == addr) {
            break;
        }
    }
    if (k == SKY_MAX_PICS) {
        for (k = SKY_MAX_PICS - 1; k > 0; k--) {
            sky_pic_done[k] = sky_pic_done[k - 1];
        }
        k = 0;
    }
    sky_pic_done[k].addr = addr;
    sky_pic_done[k].sum = sky_picture_sum(pic, w, h);
}

/* Groups the drum's corners into rings of nodes at equal azimuth steps. */
static s32 sky_find_rings(Vtx** corners, s32 count) {
    f32 ring_y[SKY_MAX_RINGS];
    s32 rings = 0;
    s32 i, r, m, k;

    for (i = 0; i < count; i++) {
        f32 y = corners[i]->v.ob[1];

        for (r = 0; r < rings; r++) {
            if (y > ring_y[r] - 2.0f && y < ring_y[r] + 2.0f) {
                break;
            }
        }
        if (r == rings) {
            if (rings >= SKY_MAX_RINGS) {
                return 0;
            }
            ring_y[rings++] = y;
        }
    }
    if (rings < 2) {
        return 0;
    }
    /* Lowest first. */
    for (r = 0; r < rings; r++) {
        for (m = r + 1; m < rings; m++) {
            if (ring_y[m] < ring_y[r]) {
                f32 t = ring_y[r];

                ring_y[r] = ring_y[m];
                ring_y[m] = t;
            }
        }
    }
    /* The nodes: the azimuths of the lowest ring, sorted, set the steps. */
    sky_nodes = 0;
    for (r = 0; r < rings; r++) {
        f32 az[SKY_MAX_NODES];
        Vtx* at[SKY_MAX_NODES];
        s32 n = 0;

        for (i = 0; i < count; i++) {
            Vtx* v = corners[i];
            f32 a;

            if (!(v->v.ob[1] > ring_y[r] - 2.0f && v->v.ob[1] < ring_y[r] + 2.0f)) {
                continue;
            }
            a = sky_azimuth((f32) v->v.ob[0], (f32) v->v.ob[2]);
            for (m = 0; m < n; m++) {
                f32 d = a - az[m];

                if (d < 0.0f) {
                    d = -d;
                }
                if (d < 2.0f || d > 358.0f) {
                    break;
                }
            }
            if (m == n) {
                if (n >= SKY_MAX_NODES) {
                    return 0;
                }
                az[n] = a;
                at[n] = v;
                n++;
            }
        }
        if (n < 4) {
            return 0;
        }
        for (m = 0; m < n; m++) {
            for (k = m + 1; k < n; k++) {
                if (az[k] < az[m]) {
                    f32 t = az[m];
                    Vtx* tv = at[m];

                    az[m] = az[k];
                    at[m] = at[k];
                    az[k] = t;
                    at[k] = tv;
                }
            }
        }
        if (r == 0) {
            sky_nodes = n;
            sky_az0 = az[0];
            sky_step = 360.0f / (f32) n;
        } else if (n != sky_nodes) {
            return 0;
        }
        for (m = 0; m < n; m++) {
            f32 want = sky_az0 + sky_step * (f32) m;
            f32 d = az[m] - want;

            if (d < 0.0f) {
                d = -d;
            }
            if (d > 2.0f) {
                return 0;
            }
            sky_node[r][m] = at[m];
        }
        sky_ring_y[r] = ring_y[r];
    }
    sky_rings = rings;
    {
        Vtx* v = sky_node[rings - 1][0];
        f32 x = v->v.ob[0], y = v->v.ob[1], z = v->v.ob[2];
        f32 len2 = x * x + y * y + z * z;

        sky_top_sin2 = (len2 > 1.0f) ? (y * y) / len2 : 1.0f;
    }
    return 1;
}

/* Reads the game's list and builds the fine drum from it. */
static s32 sky_build(Gfx* list) {
    Vtx* slot[32];
    Vtx* corners[SKY_MAX_CORNERS];
    Gfx* stack[4];
    Gfx* hide[SKY_MAX_TRIS];
    Gfx* out0 = sky_list[0];
    Gfx* out1 = sky_list[1];
    Gfx* cmd = list;
    u32 pic_addr[SKY_MAX_PICS];
    s32 pic_n = 0;
    s32 pic_w = 0, pic_h = 0;
    s32 sp = 0, states = 0, corner_n = 0, hide_n = 0, guard = 4096;
    s32 i, k;

    sky_sub_n = 0;
    sky_tris = 0;
    for (i = 0; i < 32; i++) {
        slot[i] = NULL;
    }
    while (guard-- > 0) {
        u32 w0 = cmd->words.w0;
        u32 w1 = cmd->words.w1;
        u32 op = w0 >> 24;

        if (w0 == 0 && w1 == SKY_HIDDEN_MARK) {
            /* A triangle this patch hid on an earlier build of the same list. */
            for (i = 0; i < sky_hidden_n; i++) {
                if (sky_hidden[i].at == cmd) {
                    w0 = sky_hidden[i].w0;
                    w1 = sky_hidden[i].w1;
                    op = w0 >> 24;
                    break;
                }
            }
        }
        if (op == G_VTX >> 24 || op == 0x01) {
            s32 n = (w0 >> 12) & 0xFF;
            s32 v0 = ((w0 >> 1) & 0x7F) - n;

            if ((w1 >> 24) != 0x80 || v0 < 0 || v0 + n > 32) {
                SKY_REPORT = 0xFFFF0000u | SKY_LEFT_SHAPE;
                return 0;
            }
            for (i = 0; i < n; i++) {
                slot[v0 + i] = (Vtx*) (w1 + 16 * i);
            }
        } else if (op == 0x05 || op == 0x06 || op == 0x07) {
            u32 words[2];
            s32 count = (op == 0x05) ? 1 : 2;

            words[0] = w0;
            words[1] = w1;
            if (hide_n >= SKY_MAX_TRIS) {
                SKY_REPORT = 0xFFFF0000u | SKY_LEFT_LONG;
                return 0;
            }
            hide[hide_n++] = cmd;
            for (k = 0; k < count; k++) {
                s32 ia = ((words[k] >> 16) & 0xFF) / 2;
                s32 ib = ((words[k] >> 8) & 0xFF) / 2;
                s32 ic = (words[k] & 0xFF) / 2;
                Vtx* tri[3];
                s32 c;

                if (ia >= 32 || ib >= 32 || ic >= 32 || slot[ia] == NULL || slot[ib] == NULL || slot[ic] == NULL) {
                    SKY_REPORT = 0xFFFF0000u | SKY_LEFT_EMPTY;
                    return 0;
                }
                tri[0] = slot[ia];
                tri[1] = slot[ib];
                tri[2] = slot[ic];
                for (c = 0; c < 3; c++) {
                    for (i = 0; i < corner_n; i++) {
                        if (corners[i]->v.ob[0] == tri[c]->v.ob[0] && corners[i]->v.ob[1] == tri[c]->v.ob[1]
                            && corners[i]->v.ob[2] == tri[c]->v.ob[2]) {
                            break;
                        }
                    }
                    if (i == corner_n) {
                        if (corner_n >= SKY_MAX_CORNERS) {
                            SKY_REPORT = 0xFFFF0000u | SKY_LEFT_LONG;
                            return 0;
                        }
                        corners[corner_n++] = tri[c];
                    }
                }
                if (!sky_cut(tri[0], tri[1], tri[2], &out0, &out1)) {
                    SKY_REPORT = 0xFFFF0000u | SKY_LEFT_LONG;
                    return 0;
                }
            }
        } else if (op == 0xDE) {
            /* A call or a branch: followed into the course's own data, left
             * alone when it reaches a mod's or the port's (those run after). */
            if (w1 >= 0x80800000u || (w1 >> 24) != 0x80) {
                /* skipped */
            } else if ((w0 >> 16) & 1) {
                cmd = (Gfx*) w1 - 1;
            } else {
                if (sp >= 4) {
                    SKY_REPORT = 0xFFFF0000u | SKY_LEFT_SHAPE;
                    return 0;
                }
                stack[sp++] = cmd;
                cmd = (Gfx*) w1 - 1;
            }
        } else if (op == 0xDF) {
            if (sp > 0) {
                cmd = stack[--sp];
            } else {
                break;
            }
        } else if (op == 0xDA || op == 0xD8 || op == 0xDC || op == 0xDB) {
            /* A matrix, a pop, a moveword or a movemem: not a plain drum. */
            SKY_REPORT = 0xFFFF0000u | SKY_LEFT_SHAPE;
            return 0;
        } else if (op == 0x03 || op == 0x04) {
            /* A cull or a branch on the game's own corners: not for the fine
             * drum's list, whose corners are elsewhere. */
        } else {
            if (states >= SKY_MAX_STATE) {
                SKY_REPORT = 0xFFFF0000u | SKY_LEFT_LONG;
                return 0;
            }
            states++;
            out0->words.w0 = w0;
            out0->words.w1 = w1;
            out0++;
            out1->words.w0 = w0;
            out1->words.w1 = w1;
            out1++;
            if (op == 0xFD) {
                /* A picture, RGBA16 only; its size may come before or after. */
                if (((w0 >> 21) & 7) == 0 && ((w0 >> 19) & 3) == 2 && pic_n < SKY_MAX_PICS) {
                    for (i = 0; i < pic_n; i++) {
                        if (pic_addr[i] == w1) {
                            break;
                        }
                    }
                    if (i == pic_n) {
                        pic_addr[pic_n++] = w1;
                    }
                }
            } else if (op == 0xF2) {
                pic_w = (s32) (((w1 >> 12) & 0xFFF) >> 2) + 1;
                pic_h = (s32) ((w1 & 0xFFF) >> 2) + 1;
            }
        }
        cmd++;
    }
    if (sky_tris == 0) {
        SKY_REPORT = 0xFFFF0000u | SKY_LEFT_EMPTY;
        return 0;
    }
    if (pic_w > 0 && pic_h > 0) {
        for (i = 0; i < pic_n; i++) {
            sky_dissolve_picture(pic_addr[i], pic_w, pic_h);
        }
    }
    if (!sky_find_rings(corners, corner_n)) {
        SKY_REPORT = 0xFFFF0000u | SKY_LEFT_RINGS;
        return 0;
    }
    gSPEndDisplayList(out0++);
    gSPEndDisplayList(out1++);
    /* The game's triangles go quiet; their words are kept, by address, for
     * the next build of the same list (a photo's world builds it again). */
    for (i = 0; i < hide_n; i++) {
        Gfx* g = hide[i];
        s32 j;

        if (g->words.w0 == 0 && g->words.w1 == SKY_HIDDEN_MARK) {
            continue;       /* hidden on an earlier build; its words are in the table */
        }
        for (j = 0; j < sky_hidden_n; j++) {
            if (sky_hidden[j].at == g) {
                break;
            }
        }
        if (j == sky_hidden_n) {
            if (sky_hidden_n < SKY_MAX_TRIS) {
                sky_hidden_n++;
            } else {
                j = 0;      /* full of another course's: the oldest goes */
            }
        }
        sky_hidden[j].at = g;
        sky_hidden[j].w0 = g->words.w0;
        sky_hidden[j].w1 = g->words.w1;
        g->words.w0 = 0;
        g->words.w1 = SKY_HIDDEN_MARK;
    }
    for (k = 0; k < 2; k++) {
        Gfx* e = sky_entry[k];

        gSPDisplayList(e++, sky_list[k]);
        gSPBranchList(e++, list);
    }
    sky_sum[0] = sky_sum[1] = 0xFFFFFFFFu;
    SKY_REPORT = ((u32) sky_tris << 16) | ((u32) sky_rings << 8) | (u32) sky_nodes;
    return 1;
}

/* --------------------------------------------------------------------- */
/* Painting the fine drum from the game's corners.                       */

static f32 sky_clamp01(f32 v) {
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/* The sum of the corner colors the drum is painted from: when it has not
 * changed, a set painted from it is still right. */
static u32 sky_color_sum(void) {
    u32 sum = 0;
    s32 r, m;

    for (r = 0; r < sky_rings; r++) {
        for (m = 0; m < sky_nodes; m++) {
            const Vtx* v = sky_node[r][m];

            sum = sum * 31 + ((u32) v->v.cn[0] << 16) + ((u32) v->v.cn[1] << 8) + v->v.cn[2];
        }
    }
    return sum;
}

static void sky_paint(s32 set) {
    f32 node[SKY_MAX_RINGS][SKY_MAX_NODES][3];
    f32 mean_top[3];
    s32 r, m, k, i;

    for (r = 0; r < sky_rings; r++) {
        for (m = 0; m < sky_nodes; m++) {
            for (k = 0; k < 3; k++) {
                node[r][m][k] = sky_node[r][m]->v.cn[k];
            }
        }
    }
    for (k = 0; k < 3; k++) {
        f32 s = 0.0f;

        for (m = 0; m < sky_nodes; m++) {
            s += node[sky_rings - 1][m][k];
        }
        mean_top[k] = s / (f32) sky_nodes;
    }
    for (i = 0; i < sky_sub_n; i++) {
        f32 ring[SKY_MAX_RINGS][3];
        f32 color[3];
        f32 f = (sky_az[i] - sky_az0) / sky_step;
        s32 seg = (s32) f;
        f32 t, t2, t3, w[4];
        s32 n[4];

        if ((f32) seg > f) {
            seg--;
        }
        t = f - (f32) seg;
        t2 = t * t;
        t3 = t2 * t;
        w[0] = (-t3 + 2.0f * t2 - t) * 0.5f;
        w[1] = (3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f;
        w[2] = (-3.0f * t3 + 4.0f * t2 + t) * 0.5f;
        w[3] = (t3 - t2) * 0.5f;
        for (k = 0; k < 4; k++) {
            s32 q = (seg - 1 + k) % sky_nodes;

            if (q < 0) {
                q += sky_nodes;
            }
            n[k] = q;
        }
        /* Around the drum: each ring's color at this azimuth. */
        for (r = 0; r < sky_rings; r++) {
            for (k = 0; k < 3; k++) {
                ring[r][k] = w[0] * node[r][n[0]][k] + w[1] * node[r][n[1]][k] + w[2] * node[r][n[2]][k] + w[3] * node[r][n[3]][k];
            }
        }
        /* Up the drum: a cubic between the rings, flat at the lowest and at
         * the lid, so no ring's level shows as a line. */
        if (sky_y[i] >= sky_ring_y[sky_rings - 1] - 1.0f) {
            f32 dome = (sky_top_sin2 < 0.999f) ? sky_clamp01((sky_sin2[i] - sky_top_sin2) / (1.0f - sky_top_sin2)) : 0.0f;

            dome = dome * dome * (3.0f - 2.0f * dome);
            for (k = 0; k < 3; k++) {
                color[k] = ring[sky_rings - 1][k] + (mean_top[k] - ring[sky_rings - 1][k]) * dome;
            }
        } else if (sky_y[i] <= sky_ring_y[0] + 1.0f) {
            for (k = 0; k < 3; k++) {
                color[k] = ring[0][k];
            }
        } else {
            f32 u, h00, h10, h01, h11, span;

            for (r = 0; r < sky_rings - 2; r++) {
                if (sky_y[i] < sky_ring_y[r + 1]) {
                    break;
                }
            }
            span = sky_ring_y[r + 1] - sky_ring_y[r];
            u = (span > 0.01f) ? sky_clamp01((sky_y[i] - sky_ring_y[r]) / span) : 0.0f;
            h00 = 2.0f * u * u * u - 3.0f * u * u + 1.0f;
            h10 = u * u * u - 2.0f * u * u + u;
            h01 = -2.0f * u * u * u + 3.0f * u * u;
            h11 = u * u * u - u * u;
            for (k = 0; k < 3; k++) {
                f32 m0 = 0.0f, m1 = 0.0f;

                if (r > 0) {
                    m0 = (ring[r + 1][k] - ring[r - 1][k]) / (sky_ring_y[r + 1] - sky_ring_y[r - 1]);
                }
                if (r + 2 < sky_rings) {
                    m1 = (ring[r + 2][k] - ring[r][k]) / (sky_ring_y[r + 2] - sky_ring_y[r]);
                }
                color[k] = h00 * ring[r][k] + h10 * m0 * span + h01 * ring[r + 1][k] + h11 * m1 * span;
            }
        }
        for (k = 0; k < 3; k++) {
            f32 c = color[k] + 0.5f;

            sky_vtx[set][i].v.cn[k] = (u8) (c < 0.0f ? 0 : c > 255.0f ? 255 : (s32) c);
        }
    }
}

/* --------------------------------------------------------------------- */
/* The draw.                                                             */

/* Before a sky is drawn: its list is read the first time it is seen, and
 * the fine drum takes its place; the drum is painted again when the game's
 * corner colors have changed since this set was painted. */
static void sky_before_draw(GObj* obj) {
    DObj* dobj = obj->data.dobj;
    Gfx* list;
    u32 sum;

    if (dobj == NULL || dobj->payload.dlist == NULL) {
        return;
    }
    list = dobj->payload.dlist;
    if (SKY_KEEP_DRUM) {
        if (list == sky_entry[0] || list == sky_entry[1]) {
            dobj->payload.dlist = sky_source;       /* the switch mid-run: back to the drum */
        }
        return;
    }
    if (list != sky_entry[0] && list != sky_entry[1]) {
        /* The game's own list. One this patch left alone is left alone
         * again, unless another course's list has taken its address. */
        u32 sig = list[0].words.w0 ^ (list[0].words.w1 * 31u) ^ list[1].words.w0 ^ (list[1].words.w1 * 17u);

        if (list == sky_source && sky_failed && sig == sky_source_sig) {
            return;
        }
        sky_source = list;
        sky_source_sig = sig;
        sky_ready = (u8) sky_build(list);
        sky_failed = !sky_ready;
        if (!sky_ready) {
            return;
        }
    } else if (!sky_ready) {
        return;
    }
    sky_turn ^= 1;
    sum = sky_color_sum();
    if (sky_sum[sky_turn] != sum) {
        sky_paint(sky_turn);
        sky_sum[sky_turn] = sum;
    }
    dobj->payload.dlist = sky_entry[sky_turn];
}

void drawSkyBox1Cycle(GObj* obj) {
    sky_before_draw(obj);
    gDPPipeSync(gMainGfxPos[0]++);
    gDPSetCycleType(gMainGfxPos[0]++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMainGfxPos[0]++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    gSPClearGeometryMode(gMainGfxPos[0]++, G_ZBUFFER | G_FOG);
    renRenderModelTypeA(obj);
    gDPPipeSync(gMainGfxPos[0]++);
    gSPSetGeometryMode(gMainGfxPos[0]++, G_ZBUFFER | G_FOG);
}

void drawSkyBox2Cycle(GObj* obj) {
    sky_before_draw(obj);
    gDPPipeSync(gMainGfxPos[0]++);
    gDPSetCycleType(gMainGfxPos[0]++, G_CYC_2CYCLE);
    gDPSetRenderMode(gMainGfxPos[0]++, G_RM_PASS, G_RM_AA_OPA_SURF2);
    gSPClearGeometryMode(gMainGfxPos[0]++, G_ZBUFFER | G_FOG);
    renRenderModelTypeA(obj);
    gDPPipeSync(gMainGfxPos[0]++);
    gSPSetGeometryMode(gMainGfxPos[0]++, G_ZBUFFER | G_FOG);
}
