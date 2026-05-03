#if !defined(X_C)
#define X_C

#include "st.h"
#include "math.h"
#include "config.def.h"
#include "boxdraw.c"
#include "selection.c"
#include "utf8.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_x 1
#elif !defined(TESTING_x)
#define TESTING_x 0
#endif

static uint16
sixd_to_16bit(int32 x) {
    int32 y;
    if (x == 0) {
        y = 0;
    } else {
        y = 0x3737 + 0x2828*x;
    }
    return (uint16)y;
}

static void
x_resize(int32 col, int32 row) {
    term_window.tty_width = col*term_window.cw;
    term_window.tty_height = row*term_window.ch;

    XFreePixmap(x_window.display, x_window.drawable);
    x_window.drawable = XCreatePixmap(x_window.display, x_window.win,
                                      (uint32)term_window.w,
                                      (uint32)term_window.h,
                                      (uint32)x_window.depth);
    XftDrawChange(x_window.xft_draw, x_window.drawable);
    x_clear(0, 0, term_window.w, term_window.h);

    x_window.specbuf = xrealloc(x_window.specbuf, col*SIZEOF(XftGlyphFontSpec));
    return;
}

static int32
x_get_color(int32 x, uint *r, uint *g, uint *b) {
    if (!BETWEEN(x, 0, draw_context.colors_len - 1)) {
        return 1;
    }

    *r = draw_context.colors[x].color.red >> 8;
    *g = draw_context.colors[x].color.green >> 8;
    *b = draw_context.colors[x].color.blue >> 8;

    return 0;
}

static int32
x_load_color(int32 i, char *name, XftColor *xft_color) {
    XRenderColor color = {.alpha = 0xffff};

    if (!name) {
        if (BETWEEN(i, 16 + CONF_NTRANSPARENT_COLORS, 255)) { /* 256 color */
            if (i < 6*6 * 6 + 16) { /* same colors as xterm */
                color.red = sixd_to_16bit(((i - 16) / 36) % 6);
                color.green = sixd_to_16bit(((i - 16) / 6) % 6);
                color.blue = sixd_to_16bit(((i - 16) / 1) % 6);
            } else { /* greyscale */
                color.red = (uint16) (0x0808 + 0x0a0a*(i - (6*6 * 6 + 16)));
                color.green = color.red;
                color.blue = color.red;
            }
            return XftColorAllocValue(x_window.display, x_window.visual,
                                      x_window.color_map, &color, xft_color);
        } else {
            name = CONF_COLORS[i];
        }
    }

    return XftColorAllocName(x_window.display, x_window.visual,
                             x_window.color_map, name, xft_color);
}

static void
x_load_cols(void) {
    static int32 loaded = 0;

    if (loaded) {
        for (XftColor *cp = draw_context.colors;
             cp < &draw_context.colors[draw_context.colors_len];
             cp += 1) {
            XftColorFree(x_window.display,
                         x_window.visual, x_window.color_map, cp);
        }
    } else {
        draw_context.colors_len = (int32)MAX(LENGTH(CONF_COLORS), 256);
        draw_context.colors = xmalloc(draw_context.colors_len*SIZEOF(XftColor));
    }

    for (int32 i = 0; i < draw_context.colors_len; i += 1) {
        if (!x_load_color(i, NULL, &draw_context.colors[i])) {
            if (CONF_COLORS[i]) {
                error("could not allocate color '%s'\n", CONF_COLORS[i]);
                exit(EXIT_FAILURE);
            } else {
                error("could not allocate color %d\n", i);
                exit(EXIT_FAILURE);
            }
        }
    }

    draw_context.colors[CONF_COLOR_BG].color.alpha
        = (uint16)(0xffff*CONF_ALPHA);
    draw_context.colors[CONF_COLOR_BG].pixel &= 0x00FFFFFF;
    draw_context.colors[CONF_COLOR_BG].pixel
        |= ((uint32)(0xFF*CONF_ALPHA) & 0xFF) << 24;

    for (int32 i = 16; i < 16 + CONF_NTRANSPARENT_COLORS; i += 1) {
        draw_context.colors[i].color.alpha = (uint16)(0xffff*CONF_ALPHA);
        draw_context.colors[i].pixel &= 0x00FFFFFF;
        draw_context.colors[i].pixel |= ((uint32)(0xff*CONF_ALPHA) & 0xff)
                                        << 24;
    }
    loaded = 1;
    return;
}

static int32
x_set_color_name(int32 x, char *name) {
    XftColor color;

    if (!BETWEEN(x, 0, draw_context.colors_len - 1)) {
        return 1;
    }

    if (!x_load_color(x, name, &color)) {
        return 1;
    }

    XftColorFree(x_window.display, x_window.visual, x_window.color_map,
                 &draw_context.colors[x]);
    draw_context.colors[x] = color;

    if (x == CONF_COLOR_BG) {
        draw_context.colors[CONF_COLOR_BG].color.alpha
            = (uint16)(0xffff*CONF_ALPHA);
        draw_context.colors[CONF_COLOR_BG].pixel &= 0x00FFFFFF;
        draw_context.colors[CONF_COLOR_BG].pixel
            |= ((uint32)(0xff*CONF_ALPHA) & 0xff) << 24;
    }

    return 0;
}

static void
x_clear(int32 x1, int32 y1, int32 x2, int32 y2) {
    int32 color_index;
    if (TERM_WINDOW_IS_SET(WIN_MODE_REVERSE)) {
        color_index = CONF_COLOR_INDEX_FONT;
    } else {
        color_index = CONF_COLOR_BG;
    }

    XftDrawRect(x_window.xft_draw, &draw_context.colors[color_index], x1, y1,
                (uint32)(x2 - x1), (uint32)(y2 - y1));
    return;
}

static void
x_hints(void) {
    XClassHint class;
    XWMHints wm = {.flags = InputHint, .input = 1};
    XSizeHints *sizeh;

    if (opt_name) {
        class.res_name = opt_name;
    } else {
        class.res_name = CONF_TERM_NAME;
    }

    if (opt_class) {
        class.res_class = opt_class;
    } else {
        class.res_class = CONF_TERM_NAME;
    }

    sizeh = XAllocSizeHints();

    sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
    sizeh->height = term_window.h;
    sizeh->width = term_window.w;
    sizeh->height_inc = 1;
    sizeh->width_inc = 1;
    sizeh->base_height = 2*CONF_BORDER_PIXELS;
    sizeh->base_width = 2*CONF_BORDER_PIXELS;
    sizeh->min_height = term_window.ch + 2*CONF_BORDER_PIXELS;
    sizeh->min_width = term_window.cw + 2*CONF_BORDER_PIXELS;
    if (x_window.is_fixed) {
        sizeh->flags |= PMaxSize;
        sizeh->min_width = term_window.w;
        sizeh->max_width = term_window.w;
        sizeh->min_height = term_window.h;
        sizeh->max_height = term_window.h;
    }
    if (x_window.geo_mask & (XValue | YValue)) {
        sizeh->flags |= USPosition | PWinGravity;
        sizeh->x = x_window.left_offset;
        sizeh->y = x_window.top_offset;
        sizeh->win_gravity = x_geom_mask_to_gravity(x_window.geo_mask);
    }

    XSetWMProperties(x_window.display, x_window.win, NULL, NULL, NULL, 0, sizeh,
                     &wm, &class);
    XFree(sizeh);
    return;
}

static int32
x_geom_mask_to_gravity(int32 mask) {
    switch (mask & (XNegative | YNegative)) {
    case 0:
        return NorthWestGravity;
    case XNegative:
        return NorthEastGravity;
    case YNegative:
        return SouthWestGravity;
    default:
        error("x_geom_mask_to_gravity: Unhandled switch case.\n");
        break;
    }

    return SouthEastGravity;
}

static int32
x_load_font(StFont *f, FcPattern *pattern) {
    FcPattern *configured;
    FcPattern *match;
    FcResult result;
    XGlyphInfo extents;
    int32 wantattr;
    int32 haveattr;

    configured = FcPatternDuplicate(pattern);
    if (!configured) {
        return 1;
    }

    FcConfigSubstitute(NULL, configured, FcMatchPattern);
    XftDefaultSubstitute(x_window.display, x_window.screen, configured);

    match = FcFontMatch(NULL, configured, &result);
    if (!match) {
        FcPatternDestroy(configured);
        return 1;
    }

    if (!(f->match = XftFontOpenPattern(x_window.display, match))) {
        FcPatternDestroy(configured);
        FcPatternDestroy(match);
        return 1;
    }

    if ((XftPatternGetInteger(pattern, "slant", 0, &wantattr)
         == XftResultMatch)) {
        if ((XftPatternGetInteger(f->match->pattern, "slant", 0, &haveattr)
             != XftResultMatch)
            || haveattr < wantattr) {
            f->badslant = 1;
            fputs("font slant does not match\n", stderr);
        }
    }

    if ((XftPatternGetInteger(pattern, "weight", 0, &wantattr)
         == XftResultMatch)) {
        if ((XftPatternGetInteger(f->match->pattern, "weight", 0, &haveattr)
             != XftResultMatch)
            || haveattr != wantattr) {
            f->badweight = 1;
            fputs("font weight does not match\n", stderr);
        }
    }

    XftTextExtentsUtf8(x_window.display, f->match,
                       (FcChar8 *)CONF_ASCII_PRINTABLE,
                       strlen32(CONF_ASCII_PRINTABLE), &extents);

    f->set = NULL;
    f->pattern = configured;

    f->ascent = f->match->ascent;
    f->descent = f->match->descent;
    f->lbearing = 0;
    f->rbearing = (int16)f->match->max_advance_width;

    f->height = f->ascent + f->descent;
    f->width
        = DIVCEIL(extents.xOff, strlen32(CONF_ASCII_PRINTABLE));

    return 0;
}

static void
x_load_fonts(char *fontstr, float fontsize) {
    FcPattern *pattern;
    double fontval;

    if (fontstr[0] == '-') {
        pattern = XftXlfdParse(fontstr, False, False);
    } else {
        pattern = FcNameParse((FcChar8 *)fontstr);
    }

    if (!pattern) {
        error("can't open font %s\n", fontstr);
        exit(EXIT_FAILURE);
    }

    if (fontsize > 1) {
        FcPatternDel(pattern, FC_PIXEL_SIZE);
        FcPatternDel(pattern, FC_SIZE);
        FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)fontsize);
        usedfontsize = fontsize;
    } else {
        if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval)
            == FcResultMatch) {
            usedfontsize = (float)fontval;
        } else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval)
                   == FcResultMatch) {
            usedfontsize = -1;
        } else {
            FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
            usedfontsize = 12;
        }
        defaultfontsize = usedfontsize;
    }

    if (x_load_font(&draw_context.font, pattern)) {
        error("can't open font %s\n", fontstr);
        exit(EXIT_FAILURE);
    }

    if (usedfontsize < 0) {
        FcPatternGetDouble(draw_context.font.match->pattern, FC_PIXEL_SIZE, 0,
                           &fontval);
        usedfontsize = (float)fontval;
        if (fabsf(fontsize) <= 0) {
            defaultfontsize = (float)fontval;
        }
    }

    /* Setting character width and height. */
    {
        float cw = ceilf((float)(draw_context.font.width)*CONF_CHAR_WIDTH_SCALE);
        float ch = ceilf((float)(draw_context.font.height)*CONF_CHAR_HEIGHT_SCALE);
        term_window.cw = (int32)cw;
        term_window.ch = (int32)ch;
    }

    FcPatternDel(pattern, FC_SLANT);
    FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
    if (x_load_font(&draw_context.ifont, pattern)) {
        error("can't open font %s\n", fontstr);
        exit(EXIT_FAILURE);
    }

    FcPatternDel(pattern, FC_WEIGHT);
    FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
    if (x_load_font(&draw_context.ibfont, pattern)) {
        error("can't open font %s\n", fontstr);
        exit(EXIT_FAILURE);
    }

    FcPatternDel(pattern, FC_SLANT);
    FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
    if (x_load_font(&draw_context.bfont, pattern)) {
        error("can't open font %s\n", fontstr);
        exit(EXIT_FAILURE);
    }

    FcPatternDestroy(pattern);
    return;
}

static int32
xloadsparefont(FcPattern *pattern, int32 flags) {
    FcPattern *match;
    FcResult result;

    match = FcFontMatch(NULL, pattern, &result);
    if (!match) {
        return 1;
    }

    if (!(frc[frclen].font = XftFontOpenPattern(x_window.display, match))) {
        FcPatternDestroy(match);
        return 1;
    }

    frc[frclen].flags = flags;
    frc[frclen].unicodep = 0;
    frclen += 1;

    return 0;
}

static void
x_load_spare_fonts(void) {
    FcPattern *pattern;
    double sizeshift;
    double fontval;
    int32 fc;
    char **fp;

    if (frclen != 0) {
        error("can't embed spare fonts. cache isn't empty");
        exit(EXIT_FAILURE);
    }

    /* Calculate count of spare fonts */
    fc = SIZEOF(CONF_FONT2) / SIZEOF(*CONF_FONT2);
    if (fc == 0) {
        return;
    }

    /* Allocate memory for cache entries. */
    if (frccap < 4*fc) {
        frccap += 4*fc - frccap;
        frc = xrealloc(frc, (int64)frccap*SIZEOF(FontCache));
    }

    for (fp = CONF_FONT2; fp - CONF_FONT2 < fc; fp += 1) {

        if (**fp == '-') {
            pattern = XftXlfdParse(*fp, False, False);
        } else {
            pattern = FcNameParse((FcChar8 *)*fp);
        }

        if (!pattern) {
            error("can't open spare font %s\n", *fp);
            exit(EXIT_FAILURE);
        }

        if (defaultfontsize > 0) {
            sizeshift = (double)(usedfontsize - defaultfontsize);
            if (fabs(sizeshift) >= 0.001) {
                if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval)
                    == FcResultMatch) {
                    fontval += sizeshift;
                    FcPatternDel(pattern, FC_PIXEL_SIZE);
                    FcPatternDel(pattern, FC_SIZE);
                    FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontval);
                }
            }
        }

        FcPatternAddBool(pattern, FC_SCALABLE, 1);

        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        XftDefaultSubstitute(x_window.display, x_window.screen, pattern);

        if (xloadsparefont(pattern, FRC_NORMAL)) {
            error("can't open spare font %s\n", *fp);
            exit(EXIT_FAILURE);
        }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
        if (xloadsparefont(pattern, FRC_ITALIC)) {
            error("can't open spare font %s\n", *fp);
            exit(EXIT_FAILURE);
        }

        FcPatternDel(pattern, FC_WEIGHT);
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
        if (xloadsparefont(pattern, FRC_ITALICBOLD)) {
            error("can't open spare font %s\n", *fp);
            exit(EXIT_FAILURE);
        }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
        if (xloadsparefont(pattern, FRC_BOLD)) {
            error("can't open spare font %s\n", *fp);
            exit(EXIT_FAILURE);
        }

        FcPatternDestroy(pattern);
    }
    return;
}

static void
x_unload_font(StFont *f) {
    XftFontClose(x_window.display, f->match);
    FcPatternDestroy(f->pattern);
    if (f->set) {
        FcFontSetDestroy(f->set);
    }
    return;
}

static void
x_unload_fonts(void) {
    /* Free the loaded fonts in the font cache.  */
    while (frclen > 0) {
        frclen -= 1;
        XftFontClose(x_window.display, frc[frclen].font);
    }

    x_unload_font(&draw_context.font);
    x_unload_font(&draw_context.bfont);
    x_unload_font(&draw_context.ifont);
    x_unload_font(&draw_context.ibfont);
    return;
}

static void
x_im_instantiate(Display *display, XPointer client, XPointer call) {
    (void)client;
    (void)call;
    if (x_im_open(display)) {
        XUnregisterIMInstantiateCallback(x_window.display, NULL, NULL, NULL,
                                         x_im_instantiate, NULL);
    }
    return;
}

static void
x_im_destroy(XIM xim, XPointer client, XPointer call) {
    (void)xim;
    (void)client;
    (void)call;
    x_window.ime.xim = NULL;
    XRegisterIMInstantiateCallback(x_window.display, NULL, NULL, NULL,
                                   x_im_instantiate, NULL);
    XFree(x_window.ime.spotlist);
    return;
}

static int32
x_ic_destroy(XIC xim, XPointer client, XPointer call) {
    (void)xim;
    (void)client;
    (void)call;
    x_window.ime.xic = NULL;
    return 1;
}

static int32
x_im_open(Display *display) {
    XIMCallback imdestroy = {.client_data = NULL, .callback = x_im_destroy};
    XICCallback icdestroy = {.client_data = NULL, .callback = x_ic_destroy};
    (void)display;

    x_window.ime.xim = XOpenIM(x_window.display, NULL, NULL, NULL);
    if (x_window.ime.xim == NULL) {
        return 0;
    }

    if (XSetIMValues(x_window.ime.xim, XNDestroyCallback, &imdestroy, NULL)) {
        error("XSetIMValues: Could not set XNDestroyCallback.\n");
    }

    x_window.ime.spotlist
        = XVaCreateNestedList(0, XNSpotLocation, &x_window.ime.point, NULL);

    if (x_window.ime.xic == NULL) {
        x_window.ime.xic
            = XCreateIC(x_window.ime.xim, XNInputStyle,
                        XIMPreeditNothing | XIMStatusNothing, XNClientWindow,
                        x_window.win, XNDestroyCallback, &icdestroy, NULL);
    }
    if (x_window.ime.xic == NULL) {
        error("XCreateIC: Could not create input context.\n");
    }

    return 1;
}

static int32
x_make_glyph_font_specs(XftGlyphFontSpec *specs, StGlyph *glyphs,
                        int32 len, int32 x, int32 y) {
    int32 winx = term_window.hborderpx + x*term_window.cw;
    int32 winy = term_window.vborderpx + y*term_window.ch;
    enum GlyphAttribute prevmode = ATTR_LAST;
    StFont *font_local = &draw_context.font;
    int32 frcflags = FRC_NORMAL;
    int32 runewidth = term_window.cw;
    int32 numspecs = 0;
    int32 xp = winx;
    int32 yp = winy + font_local->ascent;

    for (int32 i = 0; i < len; i += 1) {
        uint32 rune = glyphs[i].rune;
        enum GlyphAttribute mode = glyphs[i].mode;
        FT_UInt glyphidx;
        int32 f;

        if (mode == ATTR_WDUMMY) {
            continue;
        }

        if (prevmode != mode) {
            prevmode = mode;
            font_local = &draw_context.font;
            frcflags = FRC_NORMAL;
            if (mode & ATTR_WIDE) {
                runewidth = term_window.cw * 2;
            } else {
                runewidth = term_window.cw;
            }
            if (mode & ATTR_ITALIC) {
                if (mode & ATTR_BOLD) {
                    font_local = &draw_context.ibfont;
                    frcflags = FRC_ITALICBOLD;
                } else {
                    font_local = &draw_context.ifont;
                    frcflags = FRC_ITALIC;
                }
            } else if (mode & ATTR_BOLD) {
                font_local = &draw_context.bfont;
                frcflags = FRC_BOLD;
            }
            yp = winy + font_local->ascent;
        }

        if (mode & ATTR_BOXDRAW) {
            glyphidx = boxdrawindex(&glyphs[i]);
        } else {
            glyphidx = XftCharIndex(x_window.display, font_local->match, rune);
        }

        if (glyphidx) {
            specs[numspecs].font = font_local->match;
            specs[numspecs].glyph = glyphidx;
            specs[numspecs].x = (int16)xp;
            specs[numspecs].y = (int16)yp;
            xp += runewidth;
            numspecs += 1;
            continue;
        }

        for (f = 0; f < frclen; f += 1) {
            glyphidx = XftCharIndex(x_window.display, frc[f].font, rune);
            if (glyphidx) {
                if (frc[f].flags == frcflags) {
                    break;
                }
            }
            if (!glyphidx) {
                if (frc[f].flags == frcflags) {
                    if (frc[f].unicodep == rune) {
                        break;
                    }
                }
            }
        }

        if (f >= frclen) {
            FcResult fcres;
            FcPattern *fcpattern;
            FcPattern *fontpattern;
            FcFontSet *fcsets[] = {NULL};
            FcCharSet *fccharset;

            if (!font_local->set) {
                font_local->set = FcFontSort(0, font_local->pattern, 1, 0, &fcres);
            }
            fcsets[0] = font_local->set;

            fcpattern = FcPatternDuplicate(font_local->pattern);
            fccharset = FcCharSetCreate();

            FcCharSetAddChar(fccharset, rune);
            FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
            FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

            FcConfigSubstitute(0, fcpattern, FcMatchPattern);
            FcDefaultSubstitute(fcpattern);

            fontpattern = FcFontSetMatch(0, fcsets, 1, fcpattern, &fcres);

            if (frclen >= frccap) {
                frccap += 16;
                frc = xrealloc(frc, (int64)frccap*SIZEOF(FontCache));
            }

            frc[frclen].font = XftFontOpenPattern(x_window.display, fontpattern);
            if (!frc[frclen].font) {
                error("XftFontOpenPattern failed seeking fallback font: %s\n",
                      strerror(errno));
                exit(EXIT_FAILURE);
            }
            frc[frclen].flags = frcflags;
            frc[frclen].unicodep = rune;

            glyphidx = XftCharIndex(x_window.display, frc[frclen].font, rune);

            f = frclen;
            frclen += 1;

            FcPatternDestroy(fcpattern);
            FcCharSetDestroy(fccharset);
        }

        specs[numspecs].font = frc[f].font;
        specs[numspecs].glyph = glyphidx;
        specs[numspecs].x = (int16)xp;
        specs[numspecs].y = (int16)yp;
        xp += runewidth;
        numspecs += 1;
    }

    return numspecs;
}

static void
x_draw_glyph_font_specs(XftGlyphFontSpec *specs,
                        StGlyph base, int32 len, int32 x, int32 y) {
    int32 charlen;
    int32 winx = term_window.hborderpx + x*term_window.cw;
    int32 winy = term_window.vborderpx + y*term_window.ch;
    int32 width;
    XftColor *fg;
    XftColor *bg;
    XftColor truefg;
    XftColor truebg;
    XRenderColor colfg;
    XRenderColor colbg;
    XRectangle r;

    if (base.mode & ATTR_WIDE) {
        charlen = len * 2;
    } else {
        charlen = len;
    }
    width = charlen*term_window.cw;

    if (base.mode & ATTR_ITALIC) {
        if (base.mode & ATTR_BOLD) {
            if (draw_context.ibfont.badslant || draw_context.ibfont.badweight) {
                base.fg = (int32)CONF_DEFAULT_ATTR;
            }
        } else if (draw_context.ifont.badslant) {
            base.fg = (int32)CONF_DEFAULT_ATTR;
        }
    } else if (base.mode & ATTR_BOLD) {
        if (draw_context.bfont.badweight) {
            base.fg = (int32)CONF_DEFAULT_ATTR;
        }
    }

    if (IS_TRUECOL(base.fg)) {
        colfg.alpha = 0xffff;
        colfg.red = TRUE_RED(base.fg);
        colfg.green = TRUE_GREEN(base.fg);
        colfg.blue = TRUE_BLUE(base.fg);
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &colfg, &truefg);
        fg = &truefg;
    } else {
        fg = &draw_context.colors[base.fg];
    }

    if (IS_TRUECOL(base.bg)) {
        colbg.alpha = 0xffff;
        colbg.red = TRUE_RED(base.bg);
        colbg.green = TRUE_GREEN(base.bg);
        colbg.blue = TRUE_BLUE(base.bg);
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &colbg, &truebg);
        bg = &truebg;
    } else {
        bg = &draw_context.colors[base.bg];
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_REVERSE)) {
        XftColor revfg;
        XftColor revbg;
        if (fg == &draw_context.colors[CONF_COLOR_INDEX_FONT]) {
            fg = &draw_context.colors[CONF_COLOR_BG];
        } else {
            colfg.red = (ushort)~fg->color.red;
            colfg.green = (ushort)~fg->color.green;
            colfg.blue = (ushort)~fg->color.blue;
            colfg.alpha = (ushort)fg->color.alpha;
            XftColorAllocValue(x_window.display, x_window.visual,
                               x_window.color_map, &colfg, &revfg);
            fg = &revfg;
        }

        if (bg == &draw_context.colors[CONF_COLOR_BG]) {
            bg = &draw_context.colors[CONF_COLOR_INDEX_FONT];
        } else {
            colbg.red = (ushort)~bg->color.red;
            colbg.green = (ushort)~bg->color.green;
            colbg.blue = (ushort)~bg->color.blue;
            colbg.alpha = (ushort)bg->color.alpha;
            XftColorAllocValue(x_window.display, x_window.visual,
                               x_window.color_map, &colbg, &revbg);
            bg = &revbg;
        }
    }

    if ((base.mode & ATTR_BOLD_FAINT) == ATTR_FAINT) {
        XftColor revfg;
        colfg.red = fg->color.red / 2;
        colfg.green = fg->color.green / 2;
        colfg.blue = fg->color.blue / 2;
        colfg.alpha = fg->color.alpha;
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &colfg, &revfg);
        fg = &revfg;
    }

    if (base.mode & ATTR_REVERSE) {
        XftColor *temp = fg;
        fg = bg;
        bg = temp;
    }

    if (base.mode & ATTR_SELECTED) {
        bg = &draw_context.colors[CONF_COLOR_INDEX_SELECTION_BACK];
        if (!CONF_COLOR_IGNORE_SELECTION_FONT_COLOR) {
            fg = &draw_context.colors[CONF_COLOR_INDEX_SELECTION_FONT];
        }
    }

    if (base.mode & ATTR_BLINK && term_window.mode & WIN_MODE_BLINK) {
        fg = bg;
    }

    if (base.mode & ATTR_INVISIBLE) {
        fg = bg;
    }

    /* Intelligent cleaning up of the borders. */
    if (x == 0) {
        int32 limit_y;
        if (winy + term_window.ch >= term_window.vborderpx + term_window.tty_height) {
            limit_y = term_window.h;
        } else {
            limit_y = 0;
        }
        x_clear(0, (y == 0) ? 0 : winy, term_window.hborderpx, winy + term_window.ch + limit_y);
    }
    if (winx + width >= term_window.hborderpx + term_window.tty_width) {
        int32 limit_y;
        if (winy + term_window.ch >= term_window.vborderpx + term_window.tty_height) {
            limit_y = term_window.h;
        } else {
            limit_y = winy + term_window.ch;
        }
        x_clear(winx + width, (y == 0) ? 0 : winy, term_window.w, limit_y);
    }
    if (y == 0) {
        x_clear(winx, 0, winx + width, term_window.vborderpx);
    }
    if (winy + term_window.ch >= term_window.vborderpx + term_window.tty_height) {
        x_clear(winx, winy + term_window.ch, winx + width, term_window.h);
    }

    /* Clean up the region we want to draw to. */
    XftDrawRect(x_window.xft_draw, bg, winx, winy, (uint32)width,
                (uint32)term_window.ch);

    /* Set the clip region because Xft is sometimes dirty. */
    r.x = 0;
    r.y = 0;
    r.height = (uint16)term_window.ch;
    r.width = (uint16)width;
    XftDrawSetClipRectangles(x_window.xft_draw, winx, winy, &r, 1);

    if (base.mode & ATTR_BOXDRAW) {
        drawboxes(winx, winy, width / len, term_window.ch, fg, bg, specs, len);
    } else {
        XftDrawGlyphFontSpec(x_window.xft_draw, fg, specs, len);
    }

    if (base.mode & ATTR_UNDERLINE) {
        XftDrawRect(x_window.xft_draw, fg, winx,
                    winy + (int32)((float)draw_context.font.ascent * CONF_CHAR_HEIGHT_SCALE) + 1,
                    (uint32)width, 1);
    }

    if (base.mode & ATTR_STRUCK) {
        XftDrawRect(x_window.xft_draw, fg, winx,
                    winy + 2 * (int32)((float)draw_context.font.ascent * CONF_CHAR_HEIGHT_SCALE / 3),
                    (uint32)width, 1);
    }

    XftDrawSetClip(x_window.xft_draw, 0);
    return;
}

static void
x_draw_glyph(StGlyph g, int32 x, int32 y) {
    int32 numspecs;
    XftGlyphFontSpec spec;

    numspecs = x_make_glyph_font_specs(&spec, &g, 1, x, y);
    x_draw_glyph_font_specs(&spec, g, numspecs, x, y);
    return;
}

static void
x_draw_cursor(int32 cx, int32 cy, StGlyph g, int32 ox, int32 oy, StGlyph og) {
    XftColor drawcol;

    if (selection_is_selected(ox, oy)) {
        og.mode |= ATTR_SELECTED;
    }
    x_draw_glyph(og, ox, oy);

    if (TERM_WINDOW_IS_SET(WIN_MODE_HIDE)) {
        return;
    }

    g.mode &= ATTR_BOLD | ATTR_ITALIC | ATTR_UNDERLINE | ATTR_STRUCK | ATTR_WIDE
              | ATTR_BOXDRAW;

    if (TERM_WINDOW_IS_SET(WIN_MODE_REVERSE)) {
        g.mode |= ATTR_REVERSE;
        g.fg = CONF_COLOR_INDEX_CURSOR;
        g.bg = CONF_COLOR_INDEX_FONT;
        drawcol = draw_context.colors[CONF_COLOR_INDEX_REVCURSOR];
    } else {
        g.fg = CONF_COLOR_BG;
        g.bg = CONF_COLOR_INDEX_CURSOR;
        drawcol = draw_context.colors[CONF_COLOR_INDEX_CURSOR];
    }

    if (TERM_WINDOW_IS_SET(WIN_MODE_FOCUSED)) {
        switch (term_window.cursor) {
        case 7:
            g.rune = 0x2603;
            _X_FALLTHROUGH;
        case 0:
        case 1:
        case 2:
            x_draw_glyph(g, cx, cy);
            break;
        case 3:
        case 4:
            XftDrawRect(x_window.xft_draw, &drawcol,
                        term_window.hborderpx + cx*term_window.cw,
                        term_window.vborderpx + (cy + 1)*term_window.ch - (int32)CONF_CURSOR_THICKNESS,
                        (uint32)term_window.cw, (uint32)CONF_CURSOR_THICKNESS);
            break;
        case 5:
        case 6:
            XftDrawRect(x_window.xft_draw, &drawcol,
                        term_window.hborderpx + cx*term_window.cw,
                        term_window.vborderpx + cy*term_window.ch,
                        CONF_CURSOR_THICKNESS, (uint32)term_window.ch);
            break;
        default:
            error("x_draw_cursor: Unhandled switch case.\n");
            break;
        }
    } else {
        XftDrawRect(x_window.xft_draw, &drawcol,
                    term_window.hborderpx + cx*term_window.cw,
                    term_window.vborderpx + cy*term_window.ch,
                    (uint32)(term_window.cw - 1), 1);
        XftDrawRect(x_window.xft_draw, &drawcol,
                    term_window.hborderpx + cx*term_window.cw,
                    term_window.vborderpx + cy*term_window.ch, 1,
                    (uint32)(term_window.ch - 1));
        XftDrawRect(x_window.xft_draw, &drawcol,
                    term_window.hborderpx + (cx + 1)*term_window.cw - 1,
                    term_window.vborderpx + cy*term_window.ch, 1,
                    (uint32)(term_window.ch - 1));
        XftDrawRect(x_window.xft_draw, &drawcol,
                    term_window.hborderpx + cx*term_window.cw,
                    term_window.vborderpx + (cy + 1)*term_window.ch - 1,
                    (uint32)term_window.cw, 1);
    }
    return;
}

static void
x_set_icon_title(char *p) {
    XTextProperty prop;
    if (!p) {
        p = opt_title;
    }
    if (p[0] == '\0') {
        p = opt_title;
    }

    if (Xutf8TextListToTextProperty(x_window.display,
                                    &p, 1,
                                    XUTF8StringStyle, &prop) != Success) {
        return;
    }
    XSetWMIconName(x_window.display, x_window.win, &prop);
    XSetTextProperty(x_window.display, x_window.win, &prop, x_window.net_wm_iconname);
    XFree(prop.value);
    return;
}

static void
x_set_title(char *p) {
    XTextProperty prop;
    if (!p) {
        p = opt_title;
    }
    if (p[0] == '\0') {
        p = opt_title;
    }

    if (Xutf8TextListToTextProperty(x_window.display, &p, 1, XUTF8StringStyle, &prop) != Success) {
        return;
    }
    XSetWMName(x_window.display, x_window.win, &prop);
    XSetTextProperty(x_window.display, x_window.win, &prop, x_window.net_wm_name);
    XFree(prop.value);
    return;
}

static int32
x_start_draw(void) {
    return TERM_WINDOW_IS_SET(WIN_MODE_VISIBLE);
}

static void
x_draw_line(StGlyph *line, int32 x1, int32 y1, int32 x2) {
    int32 i;
    int32 ox;
    int32 numspecs;
    StGlyph base = {0};
    StGlyph new = {0};
    XftGlyphFontSpec *specs = x_window.specbuf;

    numspecs = x_make_glyph_font_specs(specs, &line[x1], x2 - x1, x1, y1);
    i = 0;
    ox = 0;
    for (int32 x = x1; x < x2 && i < numspecs; x += 1) {
        new = line[x];
        if (new.mode == ATTR_WDUMMY) {
            continue;
        }
        if (selection_is_selected(x, y1)) {
            new.mode |= ATTR_SELECTED;
        }
        if (i > 0 && ATTRCMP(base, new)) {
            x_draw_glyph_font_specs(specs, base, i, ox, y1);
            specs += i;
            numspecs -= i;
            i = 0;
        }
        if (i == 0) {
            ox = x;
            base = new;
        }
        i += 1;
    }
    if (i > 0) {
        x_draw_glyph_font_specs(specs, base, i, ox, y1);
    }
    return;
}

static void
x_xim_spot(int32 x, int32 y) {
    if (x_window.ime.xic == NULL) {
        return;
    }

    x_window.ime.point.x = (int16)(CONF_BORDER_PIXELS + x*term_window.cw);
    x_window.ime.point.y = (int16)(CONF_BORDER_PIXELS + (y + 1)*term_window.ch);

    XSetICValues(x_window.ime.xic,
                 XNPreeditAttributes, x_window.ime.spotlist, NULL);
    return;
}

static void
x_set_pointer_motion(int32 set) {
    MODBIT(x_window.attrs.event_mask, set, PointerMotionMask);
    XChangeWindowAttributes(x_window.display,
                            x_window.win, CWEventMask, &x_window.attrs);
    return;
}

static void
x_set_mode(int32 set, uint32 flags) {
    enum WinMode mode = term_window.mode;
    MODBIT(term_window.mode, set, flags);
    if ((term_window.mode & WIN_MODE_REVERSE) != (mode & WIN_MODE_REVERSE)) {
        redraw();
    }
    return;
}

static int32
x_set_cursor(int32 cursor) {
    if (!BETWEEN(cursor, 0, 7)) {
        return 1;
    }
    term_window.cursor = cursor;
    return 0;
}

static void
x_set_urgency(int32 add) {
    XWMHints *h = XGetWMHints(x_window.display, x_window.win);
    MODBIT(h->flags, add, XUrgencyHint);
    XSetWMHints(x_window.display, x_window.win, h);
    XFree(h);
    return;
}

static void
x_bell(void) {
    if (!(TERM_WINDOW_IS_SET(WIN_MODE_FOCUSED))) {
        x_set_urgency(1);
    }
    if (CONF_BELL_VOLUME) {
        XkbBell(x_window.display, x_window.win, CONF_BELL_VOLUME, (Atom)NULL);
    }
    return;
}

#if TESTING_x

#include <stdbool.h>
#include <stdlib.h>

#include "assert.c"
#include "user.c"
#include "st.c"
#include "boxdraw.c"

int
main(void) {
    opt_title = "st";
    opt_class = "st";
    opt_name = "st";

    {
        Window parent;
        Window root;
        XWindowAttributes attr;
        XVisualInfo visual;
        ulong cw_flags;
        XGCValues xgc_values;

        x_window.display = XOpenDisplay(NULL);
        if (!x_window.display) {
            error("can't open display\n");
            exit(EXIT_FAILURE);
        }
        x_window.screen = XDefaultScreen(x_window.display);
        root = XRootWindow(x_window.display, x_window.screen);
        parent = root;

        if (XMatchVisualInfo(x_window.display, x_window.screen, 32, TrueColor, &visual) != 0) {
            x_window.visual = visual.visual;
            x_window.depth = visual.depth;
        } else {
            XGetWindowAttributes(x_window.display, parent, &attr);
            x_window.visual = attr.visual;
            x_window.depth = attr.depth;
        }

        if (!FcInit()) {
            error("could not init fontconfig.\n");
            exit(EXIT_FAILURE);
        }

        x_window.color_map = XCreateColormap(x_window.display, parent, x_window.visual, None);

        term_window.w = 800;
        term_window.h = 600;
        term_window.cw = 10;
        term_window.ch = 20;

        x_window.attrs.colormap = x_window.color_map;
        x_window.attrs.background_pixel = 0;
        x_window.attrs.border_pixel = 0;
        x_window.attrs.bit_gravity = NorthWestGravity;
        x_window.attrs.event_mask = FocusChangeMask | KeyPressMask | ExposureMask | StructureNotifyMask | PointerMotionMask;

        cw_flags = CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap;
        x_window.win = XCreateWindow(x_window.display, parent,
                                     0, 0,
                                     (uint32)term_window.w, (uint32)term_window.h,
                                     0, x_window.depth,
                                     InputOutput, x_window.visual,
                                     cw_flags,
                                     &x_window.attrs);

        memset64(&xgc_values, 0, SIZEOF(xgc_values));
        xgc_values.graphics_exposures = False;
        draw_context.graphics = XCreateGC(x_window.display, x_window.win, GCGraphicsExposures, &xgc_values);

        x_window.drawable = XCreatePixmap(x_window.display, x_window.win,
                                          (uint32)term_window.w,
                                          (uint32)term_window.h,
                                          (uint32)x_window.depth);

        x_window.xft_draw = XftDrawCreate(x_window.display, x_window.drawable,
                                          x_window.visual, x_window.color_map);

        x_window.xembed = XInternAtom(x_window.display, "_XEMBED", False);
        x_window.wm_delete_win = XInternAtom(x_window.display, "WM_DELETE_WINDOW", False);
        x_window.net_wm_name = XInternAtom(x_window.display, "_NET_WM_NAME", False);
        x_window.net_wm_iconname = XInternAtom(x_window.display, "_NET_WM_ICON_NAME", False);
        x_window.net_wm_pid = XInternAtom(x_window.display, "_NET_WM_PID", False);

        xsel.xtarget = XInternAtom(x_window.display, "UTF8_STRING", 0);
        if (xsel.xtarget == None) {
            xsel.xtarget = XA_STRING;
        }

        CONF_NUMBER_COLS = 80;
        CONF_NUMBER_ROWS = 24;
        term.dirty = xmalloc(CONF_NUMBER_ROWS*SIZEOF(*(term.dirty)));
        for (int32 i = 0; i < 2; i += 1) {
            term.lines = xmalloc(CONF_NUMBER_ROWS*SIZEOF(*(term.lines)));
            for (int32 j = 0; j < CONF_NUMBER_ROWS; j += 1) {
                term.lines[j] = xmalloc(CONF_NUMBER_COLS*SIZEOF(*(term.lines[j])));
            }
            term.ncols = CONF_NUMBER_COLS;
            term.nrows = CONF_NUMBER_ROWS;
            term_swap_screen();
        }
        term_window.tty_width = term_window.cw * term.ncols;
        term_window.tty_height = term_window.ch * term.nrows;
    }

    {
        uint16 result;

        result = sixd_to_16bit(0);
        ASSERT_EQUAL(result, 0);

        result = sixd_to_16bit(4);
        ASSERT_EQUAL(result, 0x3737 + 0x2828*4);
    }

    {
        int32 gravity;

        gravity = x_geom_mask_to_gravity(0);
        ASSERT_EQUAL(gravity, NorthWestGravity);

        gravity = x_geom_mask_to_gravity(XNegative);
        ASSERT_EQUAL(gravity, NorthEastGravity);

        gravity = x_geom_mask_to_gravity(YNegative);
        ASSERT_EQUAL(gravity, SouthWestGravity);

        gravity = x_geom_mask_to_gravity(XNegative | YNegative);
        ASSERT_EQUAL(gravity, SouthEastGravity);
    }

    {
        uint r;
        uint g;
        uint b;
        int32 ret;

        x_load_cols();

        ret = x_get_color(0, &r, &g, &b);
        ASSERT_EQUAL(ret, 0);

        ret = x_get_color(9999, &r, &g, &b);
        ASSERT_EQUAL(ret, 1);
    }

    {
        int32 result;

        result = x_set_color_name(CONF_COLOR_BG, "black");
        ASSERT_EQUAL(result, 0);

        result = x_set_color_name(9999, "black");
        ASSERT_EQUAL(result, 1);
    }

    {
        XftColor xft_color;

        xft_color.pixel = 0;
        x_load_color(0, "black", &xft_color);
    }

    {
        x_load_fonts("monospace", 12.0);
    }

    {
        x_load_spare_fonts();
    }

    {
        StFont font;
        FcPattern *pattern = FcNameParse((FcChar8 *)"monospace");

        x_load_font(&font, pattern);
        x_unload_font(&font);
        FcPatternDestroy(pattern);
    }

    {
        FcPattern *pattern = FcNameParse((FcChar8 *)"monospace");

        xloadsparefont(pattern, 0);
        FcPatternDestroy(pattern);
    }

    {
        x_clear(0, 0, 10, 10);
    }

    {
        x_resize(100, 30);
    }

    {
        x_hints();
    }

    {
        int32 result;

        term_window.cursor = 0;
        result = x_set_cursor(-1);
        ASSERT_EQUAL(result, 1);

        result = x_set_cursor(5);
        ASSERT_EQUAL(result, 0);
        ASSERT_EQUAL(term_window.cursor, 5);
    }

    {
        int32 result;

        term_window.mode = WIN_MODE_VISIBLE;
        result = x_start_draw();
        ASSERT_EQUAL(result, WIN_MODE_VISIBLE);

        term_window.mode = 0;
        result = x_start_draw();
        ASSERT_EQUAL(result, 0);
    }

    {
        term_window.mode = 0;
        x_set_mode(WIN_MODE_REVERSE, WIN_MODE_REVERSE);
        ASSERT(term_window.mode == WIN_MODE_REVERSE);
    }

    {
        x_set_icon_title(NULL);
        x_set_title(NULL);
        x_set_pointer_motion(0);
        x_set_urgency(0);
        x_bell();
    }

    {
        XftGlyphFontSpec spec;
        StGlyph glyph;
        StGlyph line[2];
        StGlyph og;

        glyph.rune = 'a';
        glyph.mode = 0;
        glyph.fg = 0;
        glyph.bg = 0;

        x_make_glyph_font_specs(&spec, &glyph, 1, 0, 0);
        x_draw_glyph_font_specs(&spec, glyph, 1, 0, 0);
        x_draw_glyph(glyph, 0, 0);

        og.rune = 'b';
        og.mode = 0;
        og.fg = 0;
        og.bg = 0;
        x_draw_cursor(0, 0, glyph, 0, 0, og);

        line[0] = glyph;
        line[1] = og;
        x_draw_line(line, 0, 0, 2);
    }

    {
        int32 result;

        result = x_im_open(x_window.display);
        ASSERT_EQUAL(result, 1);
        x_xim_spot(0, 0);
        x_im_instantiate(x_window.display, NULL, NULL);
        x_ic_destroy(NULL, NULL, NULL);
        x_im_destroy(NULL, NULL, NULL);
    }

    {
        x_unload_fonts();
    }

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_x */

#endif /* X_C */
