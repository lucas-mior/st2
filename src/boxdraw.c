/*
 * Copyright 2018 Avi Halachmi (:avih) avihpit@yahoo.com https://github.com/avih
 * MIT/X Consortium License
 */

#ifndef BOXDRAW_C
#define BOXDRAW_C

#include <X11/Xft/Xft.h>
#include "st.h"
#include "boxdraw_data.h"
#include "cbase/minmax.c"
#include "config.def.h"

/* Rounded non-negative integers division of n / d  */
#define DIV(n, d) (((n) + (d) / 2) / (d))

static Display *xdpy;
static Colormap xcmap;
static XftDraw *xd;
static Visual *xvis;

static void drawbox(int32, int32, int32, int32, XftColor *, XftColor *, uint16);
static void drawboxlines(int32, int32, int32, int32, XftColor *, uint16);

/* public API */

void
boxdraw_xinit(Display *display, Colormap color_map, XftDraw *draw,
              Visual *visual) {
    xdpy = display;
    xcmap = color_map;
    xd = draw;
    xvis = visual;
}

int32
isboxdraw(uint32 u) {
    uint32 block = u & ~(uint32)0xff;
    return (CONF_BOXDRAW && block == 0x2500 && boxdata[(uint8_t)u])
           || (CONF_BOXDRAW_BRAILLE && block == 0x2800);
}

/* the "index" is actually the entire shape data encoded as uint16 */
uint16
boxdrawindex(Glyph *g) {
    if (CONF_BOXDRAW_BRAILLE && (g->rune & ~(uint32)0xff) == 0x2800) {
        return BRL | (uint8_t)g->rune;
    }
    if (CONF_BOXDRAW_BOLD && (g->mode & ATTR_BOLD)) {
        return BDB | boxdata[(uint8_t)g->rune];
    }
    return boxdata[(uint8_t)g->rune];
}

void
drawboxes(int32 x, int32 y, int32 cw, int32 ch, XftColor *fg, XftColor *bg,
          XftGlyphFontSpec *specs, int32 len) {
    for (; len-- > 0; x += cw, specs++) {
        drawbox(x, y, cw, ch, fg, bg, (uint16)specs->glyph);
    }
}

/* implementation */

void
drawbox(int32 x, int32 y, int32 w, int32 h, XftColor *fg, XftColor *bg,
        uint16 bd) {
    uint16 cat = bd & ~(BDB | 0xff); /* mask out bold and data */
    if (bd & (BDL | BDA)) {
        /* lines (light/double/heavy/arcs) */
        drawboxlines(x, y, w, h, fg, bd);

    } else if (cat == BBD) {
        /* lower (8-X)/8 block */
        int32 d = DIV((uint8_t)bd*h, 8);
        XftDrawRect(xd, fg, x, y + d, (uint32)w, (uint32)(h - d));

    } else if (cat == BBU) {
        /* upper X/8 block */
        XftDrawRect(xd, fg, x, y, (uint32)w, (uint32)DIV((uint8_t)bd*h, 8));

    } else if (cat == BBL) {
        /* left X/8 block */
        XftDrawRect(xd, fg, x, y, (uint32)DIV((uint8_t)bd*w, 8), (uint32)h);

    } else if (cat == BBR) {
        /* right (8-X)/8 block */
        int32 d = DIV((uint8_t)bd*w, 8);
        XftDrawRect(xd, fg, x + d, y, (uint32)(w - d), (uint32)h);

    } else if (cat == BBQ) {
        /* Quadrants */
        int32 w2 = DIV(w, 2), h2 = DIV(h, 2);
        if (bd & TL) {
            XftDrawRect(xd, fg, x, y, (uint32)w2, (uint32)h2);
        }
        if (bd & TR) {
            XftDrawRect(xd, fg, x + w2, y, (uint32)(w - w2), (uint32)h2);
        }
        if (bd & BL) {
            XftDrawRect(xd, fg, x, y + h2, (uint32)w2, (uint32)(h - h2));
        }
        if (bd & BR) {
            XftDrawRect(xd, fg, x + w2, y + h2, (uint32)(w - w2),
                        (uint32)(h - h2));
        }

    } else if (bd & BBS) {
        /* Shades - data is 1/2/3 for 25%/50%/75% alpha, respectively */
        int32 d = (uint8_t)bd;
        XftColor xfc;
        XRenderColor xrc = {.alpha = 0xffff};

        xrc.red = (uint16)DIV(fg->color.red*d + bg->color.red*(4 - d), 4);
        xrc.green
            = (uint16)DIV(fg->color.green*d + bg->color.green*(4 - d), 4);
        xrc.blue
            = (uint16)DIV(fg->color.blue*d + bg->color.blue*(4 - d), 4);

        XftColorAllocValue(xdpy, xvis, xcmap, &xrc, &xfc);
        XftDrawRect(xd, &xfc, x, y, (uint32)w, (uint32)h);
        XftColorFree(xdpy, xvis, xcmap, &xfc);

    } else if (cat == BRL) {
        /* braille, each data bit corresponds to one dot at 2x4 grid */
        int32 w1 = DIV(w, 2);
        int32 h1 = DIV(h, 4), h2 = DIV(h, 2), h3 = DIV(3*h, 4);

        if (bd & 1) {
            XftDrawRect(xd, fg, x, y, (uint32)w1, (uint32)h1);
        }
        if (bd & 2) {
            XftDrawRect(xd, fg, x, y + h1, (uint32)w1, (uint32)(h2 - h1));
        }
        if (bd & 4) {
            XftDrawRect(xd, fg, x, y + h2, (uint32)w1, (uint32)(h3 - h2));
        }
        if (bd & 8) {
            XftDrawRect(xd, fg, x + w1, y, (uint32)(w - w1), (uint32)h1);
        }
        if (bd & 16) {
            XftDrawRect(xd, fg, x + w1, y + h1, (uint32)(w - w1),
                        (uint32)(h2 - h1));
        }
        if (bd & 32) {
            XftDrawRect(xd, fg, x + w1, y + h2, (uint32)(w - w1),
                        (uint32)(h3 - h2));
        }
        if (bd & 64) {
            XftDrawRect(xd, fg, x, y + h3, (uint32)w1, (uint32)(h - h3));
        }
        if (bd & 128) {
            XftDrawRect(xd, fg, x + w1, y + h3, (uint32)(w - w1),
                        (uint32)(h - h3));
        }
    }
}

void
drawboxlines(int32 x, int32 y, int32 w, int32 h, XftColor *fg, uint16 bd) {
    /* s: stem thickness. width/8 roughly matches underscore thickness. */
    /* We draw bold as 1.5*normal-stem and at least 1px thicker.      */
    /* doubles draw at least 3px, even when w or h < 3. bold needs 6px. */
    int32 mwh = (int32)MIN(w, h);
    int32 base_s = (int32)MAX(1, DIV(mwh, 8));
    int32 bold = (bd & BDB) && mwh >= 6; /* possibly ignore boldness */
    int32 s = bold ? (int32)MAX(base_s + 1, DIV(3*base_s, 2)) : base_s;
    int32 w2 = DIV(w - s, 2), h2 = DIV(h - s, 2);
    /* the s-by-s square (x + w2, y + h2, s, s) is the center texel.    */
    /* The base length (per direction till edge) includes this square.  */

    int32 light = bd & (LL | LU | LR | LD);
    int32 double_ = bd & (DL | DU | DR | DD);

    if (light) {
        /* d: additional (negative) length to not-draw the center   */
        /* texel - at arcs and avoid drawing inside (some) doubles  */
        int32 arc = bd & BDA;
        int32 multi_light = light & (light - 1);
        int32 multi_double = double_ & (double_ - 1);
        /* light crosses double only at DH+LV, DV+LH (ref. shapes)  */
        int32 d = arc || (multi_double && !multi_light) ? -s : 0;

        if (bd & LL) {
            XftDrawRect(xd, fg, x, y + h2, (uint32)(w2 + s + d), (uint32)s);
        }
        if (bd & LU) {
            XftDrawRect(xd, fg, x + w2, y, (uint32)s, (uint32)(h2 + s + d));
        }
        if (bd & LR) {
            XftDrawRect(xd, fg, x + w2 - d, y + h2, (uint32)(w - w2 + d),
                        (uint32)s);
        }
        if (bd & LD) {
            XftDrawRect(xd, fg, x + w2, y + h2 - d, (uint32)s,
                        (uint32)(h - h2 + d));
        }
    }

    /* double lines - also align with light to form heavy when combined */
    if (double_) {
        /*
         * going clockwise, for each double-ray: p is additional length
         * to the single-ray nearer to the previous direction, and n to
         * the next. p and n adjust from the base length to lengths
         * which consider other doubles - shorter to avoid intersections
         * (p, n), or longer to draw the far-corner texel (n).
         */

        int32 dl = bd & DL;
        int32 du = bd & DU;
        int32 dr = bd & DR;
        int32 dd = bd & DD;

        if (dl) {
            int32 p = dd ? -s : 0, n = du ? -s : dd ? s : 0;
            XftDrawRect(xd, fg, x, y + h2 + s, (uint32)(w2 + s + p), (uint32)s);
            XftDrawRect(xd, fg, x, y + h2 - s, (uint32)(w2 + s + n), (uint32)s);
        }
        if (du) {
            int32 p = dl ? -s : 0, n = dr ? -s : dl ? s : 0;
            XftDrawRect(xd, fg, x + w2 - s, y, (uint32)s, (uint32)(h2 + s + p));
            XftDrawRect(xd, fg, x + w2 + s, y, (uint32)s, (uint32)(h2 + s + n));
        }
        if (dr) {
            int32 p = du ? -s : 0, n = dd ? -s : du ? s : 0;
            XftDrawRect(xd, fg, x + w2 - p, y + h2 - s, (uint32)(w - w2 + p),
                        (uint32)s);
            XftDrawRect(xd, fg, x + w2 - n, y + h2 + s, (uint32)(w - w2 + n),
                        (uint32)s);
        }
        if (dd) {
            int32 p = dr ? -s : 0, n = dl ? -s : dr ? s : 0;
            XftDrawRect(xd, fg, x + w2 + s, y + h2 - p, (uint32)s,
                        (uint32)(h - h2 + p));
            XftDrawRect(xd, fg, x + w2 - s, y + h2 - n, (uint32)s,
                        (uint32)(h - h2 + n));
        }
    }
}

#endif /* BOXDRAW_C */

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_boxdraw 1
#elif !defined(TESTING_boxdraw)
#define TESTING_boxdraw 0
#endif
