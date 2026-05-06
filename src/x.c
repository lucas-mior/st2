#if !defined(X_C)
#define X_C

#include "st.h"
#include "math.h"
#include "config.h"
#include "boxdraw.c"
#include "selection.c"
#include "utf8.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_x 1
#elif !defined(TESTING_x)
#define TESTING_x 0
#endif

/* StFont Ring Cache */
enum {
    FRC_NORMAL,
    FRC_ITALIC,
    FRC_BOLD,
    FRC_ITALICBOLD
};

typedef struct FontCache {
    XftFont *font;
    int32 flags;
    uint32 unicodep;
} FontCache;

static FontCache *frc = NULL;
static int32 frc_len = 0;
static int32 frc_cap = 0;

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
x_resize(int32 new_ncols, int32 new_nrows) {
    term_window.tty_width = new_ncols*term_window.cw;
    term_window.tty_height = new_nrows*term_window.ch;

    XFreePixmap(x_window.display, x_window.drawable);
    x_window.drawable = XCreatePixmap(x_window.display, x_window.win,
                                      (uint32)term_window.w,
                                      (uint32)term_window.h,
                                      (uint32)x_window.depth);
    XftDrawChange(x_window.xft_draw, x_window.drawable);
    x_clear(0, 0, term_window.w, term_window.h);

    x_window.font_spec_buf = xrealloc(x_window.font_spec_buf,
                                      new_ncols*SIZEOF(XftGlyphFontSpec));
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
            if (i < 6*6*6 + 16) { /* same colors as xterm */
                color.red = sixd_to_16bit(((i - 16) / 36) % 6);
                color.green = sixd_to_16bit(((i - 16) / 6) % 6);
                color.blue = sixd_to_16bit(((i - 16) / 1) % 6);
            } else { /* greyscale */
                color.red = (uint16) (0x0808 + 0x0a0a*(i - (6*6*6 + 16)));
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
x_load_colors(void) {
    static bool loaded = false;

    if (loaded) {
        for (int32 i = 0; i < draw_context.colors_len; i += 1) {
            XftColor *xft_color = &draw_context.colors[i];
            XftColorFree(x_window.display,
                         x_window.visual, x_window.color_map, xft_color);
        }
    } else {
        int64 size;

        draw_context.colors_len = (int32)MAX(LENGTH(CONF_COLORS), 256);
        size = draw_context.colors_len*SIZEOF(*draw_context.colors);
        draw_context.colors = malloc2(size);
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

    for (int32 i = 16; i < (16 + CONF_NTRANSPARENT_COLORS); i += 1) {
        draw_context.colors[i].color.alpha = (uint16)(0xffff*CONF_ALPHA);
        draw_context.colors[i].pixel &= 0x00FFFFFF;
        draw_context.colors[i].pixel |= ((uint32)(0xff*CONF_ALPHA) & 0xff)
                                        << 24;
    }
    loaded = true;
    return;
}

static int32
x_set_color_name(int32 x, char *name) {
    XftColor xft_color;

    if (!BETWEEN(x, 0, draw_context.colors_len - 1)) {
        return 1;
    }

    if (!x_load_color(x, name, &xft_color)) {
        return 1;
    }

    XftColorFree(x_window.display,
                 x_window.visual, x_window.color_map, &draw_context.colors[x]);
    draw_context.colors[x] = xft_color;

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

    if (win_mode_is_set(WIN_MODE_REVERSE)) {
        color_index = CONF_COLOR_INDEX_FONT;
    } else {
        color_index = CONF_COLOR_BG;
    }

    XftDrawRect(x_window.xft_draw, &draw_context.colors[color_index],
                x1, y1, (uint32)(x2 - x1), (uint32)(y2 - y1));
    return;
}

static void
x_hints(void) {
    XClassHint class_hints;
    XWMHints wm_hints = {.flags = InputHint, .input = 1};
    XSizeHints *size_hints;

    if (opt_name) {
        class_hints.res_name = opt_name;
    } else {
        class_hints.res_name = CONF_TERM_NAME;
    }

    if (opt_class) {
        class_hints.res_class = opt_class;
    } else {
        class_hints.res_class = CONF_TERM_NAME;
    }

    size_hints = XAllocSizeHints();

    size_hints->flags = PSize | PResizeInc | PBaseSize | PMinSize;
    size_hints->height = term_window.h;
    size_hints->width = term_window.w;
    size_hints->height_inc = 1;
    size_hints->width_inc = 1;
    size_hints->base_height = 2*CONF_BORDER_PIXELS;
    size_hints->base_width = 2*CONF_BORDER_PIXELS;
    size_hints->min_height = term_window.ch + 2*CONF_BORDER_PIXELS;
    size_hints->min_width = term_window.cw + 2*CONF_BORDER_PIXELS;

    if (x_window.is_fixed) {
        size_hints->flags |= PMaxSize;
        size_hints->min_width = term_window.w;
        size_hints->max_width = term_window.w;
        size_hints->min_height = term_window.h;
        size_hints->max_height = term_window.h;
    }
    if (x_window.geo_mask & (XValue | YValue)) {
        size_hints->flags |= USPosition | PWinGravity;
        size_hints->x = x_window.left_offset;
        size_hints->y = x_window.top_offset;
        size_hints->win_gravity = x_geom_mask_to_gravity(x_window.geo_mask);
    }

    XSetWMProperties(x_window.display, x_window.win,
                     NULL, NULL, NULL, 0,
                     size_hints, &wm_hints, &class_hints);
    XFree(size_hints);
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
        return SouthEastGravity;
    }
}

static int32
x_load_font(StFont *st_font, FcPattern *pattern) {
    FcPattern *configured;
    FcPattern *match;
    FcResult fc_result;
    XGlyphInfo extents;
    int32 want_attr;
    int32 have_attr;

    configured = FcPatternDuplicate(pattern);
    if (!configured) {
        return 1;
    }

    FcConfigSubstitute(NULL, configured, FcMatchPattern);
    XftDefaultSubstitute(x_window.display, x_window.screen, configured);

    match = FcFontMatch(NULL, configured, &fc_result);
    if (!match) {
        FcPatternDestroy(configured);
        return 1;
    }

    if (!(st_font->match = XftFontOpenPattern(x_window.display, match))) {
        FcPatternDestroy(configured);
        FcPatternDestroy(match);
        return 1;
    }

    if ((XftPatternGetInteger(pattern, "slant", 0, &want_attr)
         == XftResultMatch)) {
        if ((XftPatternGetInteger(st_font->match->pattern, "slant", 0, &have_attr)
             != XftResultMatch)
            || have_attr < want_attr) {
            st_font->bad_slant = true;
            fputs("font slant does not match\n", stderr);
        }
    }

    if ((XftPatternGetInteger(pattern, "weight", 0, &want_attr)
         == XftResultMatch)) {
        if ((XftPatternGetInteger(st_font->match->pattern, "weight", 0, &have_attr)
             != XftResultMatch)
            || have_attr != want_attr) {
            st_font->bad_weight = true;
            fputs("font weight does not match\n", stderr);
        }
    }

    XftTextExtentsUtf8(x_window.display, st_font->match,
                       (FcChar8 *)CONF_ASCII_PRINTABLE,
                       strlen32(CONF_ASCII_PRINTABLE), &extents);

    st_font->set = NULL;
    st_font->pattern = configured;

    st_font->ascent = st_font->match->ascent;
    st_font->descent = st_font->match->descent;

    st_font->height = st_font->ascent + st_font->descent;
    st_font->width = DIVCEIL(extents.xOff, strlen32(CONF_ASCII_PRINTABLE));

    return 0;
}

static void
x_load_fonts(char *font_str, double font_size) {
    FcPattern *fc_pattern;
    double font_val;

    if (font_str[0] == '-') {
        fc_pattern = XftXlfdParse(font_str, False, False);
    } else {
        fc_pattern = FcNameParse((FcChar8 *)font_str);
    }

    if (!fc_pattern) {
        error("can't open font %s\n", font_str);
        exit(EXIT_FAILURE);
    }

    if (font_size > 1) {
        FcPatternDel(fc_pattern, FC_PIXEL_SIZE);
        FcPatternDel(fc_pattern, FC_SIZE);
        FcPatternAddDouble(fc_pattern, FC_PIXEL_SIZE, (double)font_size);
        used_font_size = font_size;
    } else {
        if (FcPatternGetDouble(fc_pattern, FC_PIXEL_SIZE, 0, &font_val)
            == FcResultMatch) {
            used_font_size = (double)font_val;
        } else if (FcPatternGetDouble(fc_pattern, FC_SIZE, 0, &font_val)
                   == FcResultMatch) {
            used_font_size = -1;
        } else {
            FcPatternAddDouble(fc_pattern, FC_PIXEL_SIZE, 12);
            used_font_size = 12;
        }
        default_font_size = used_font_size;
    }

    if (x_load_font(&draw_context.font, fc_pattern)) {
        error("can't open font %s\n", font_str);
        exit(EXIT_FAILURE);
    }

    if (used_font_size < 0) {
        FcPatternGetDouble(draw_context.font.match->pattern,
                           FC_PIXEL_SIZE, 0, &font_val);
        used_font_size = (double)font_val;
        if (fabs(font_size) <= 0) {
            default_font_size = (double)font_val;
        }
    }

    {
        double cw = ceil((double)(draw_context.font.width)*CONF_CHAR_WIDTH_SCALE);
        double ch = ceil((double)(draw_context.font.height)*CONF_CHAR_HEIGHT_SCALE);
        term_window.cw = (int32)cw;
        term_window.ch = (int32)ch;
    }

    FcPatternDel(fc_pattern, FC_SLANT);
    FcPatternAddInteger(fc_pattern, FC_SLANT, FC_SLANT_ITALIC);
    if (x_load_font(&draw_context.ifont, fc_pattern)) {
        error("can't open font %s\n", font_str);
        exit(EXIT_FAILURE);
    }

    FcPatternDel(fc_pattern, FC_WEIGHT);
    FcPatternAddInteger(fc_pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
    if (x_load_font(&draw_context.ibfont, fc_pattern)) {
        error("can't open font %s\n", font_str);
        exit(EXIT_FAILURE);
    }

    FcPatternDel(fc_pattern, FC_SLANT);
    FcPatternAddInteger(fc_pattern, FC_SLANT, FC_SLANT_ROMAN);
    if (x_load_font(&draw_context.bfont, fc_pattern)) {
        error("can't open font %s\n", font_str);
        exit(EXIT_FAILURE);
    }

    FcPatternDestroy(fc_pattern);
    return;
}

static int32
x_load_spare_font(FcPattern *pattern, int32 flags) {
    FcPattern *fc_pattern;
    FcResult fc_result;

    fc_pattern = FcFontMatch(NULL, pattern, &fc_result);
    if (!fc_pattern) {
        return 1;
    }

    if (!(frc[frc_len].font = XftFontOpenPattern(x_window.display, fc_pattern))) {
        FcPatternDestroy(fc_pattern);
        return 1;
    }

    frc[frc_len].flags = flags;
    frc[frc_len].unicodep = 0;
    frc_len += 1;

    return 0;
}

static void
x_load_spare_fonts(void) {
    int32 nspare_fonts;

    if ((nspare_fonts = LENGTH(CONF_FONT2)) <= 0) {
        return;
    }

    if (frc_len > 0) {
        error("can't embed spare fonts. cache isn't empty");
        exit(EXIT_FAILURE);
    }

    if (frc_cap < 4*nspare_fonts) {
        frc_cap += 4*nspare_fonts - frc_cap;
        frc = xrealloc(frc, frc_cap*SIZEOF(FontCache));
    }

    for (int32 i = 0; i < nspare_fonts; i += 1) {
        char *font_name = CONF_FONT2[i];
        FcPattern *fc_pattern;

        if (font_name[0] == '-') {
            fc_pattern = XftXlfdParse(font_name, False, False);
        } else {
            fc_pattern = FcNameParse((FcChar8 *)font_name);
        }

        if (!fc_pattern) {
            error("can't open spare font %s\n", font_name);
            exit(EXIT_FAILURE);
        }

        if (default_font_size > 0) {
            double sizeshift = (double)(used_font_size - default_font_size);
            if (fabs(sizeshift) >= 0.001) {
                double font_val;
                if (FcPatternGetDouble(fc_pattern, FC_PIXEL_SIZE, 0, &font_val)
                    == FcResultMatch) {
                    font_val += sizeshift;
                    FcPatternDel(fc_pattern, FC_PIXEL_SIZE);
                    FcPatternDel(fc_pattern, FC_SIZE);
                    FcPatternAddDouble(fc_pattern, FC_PIXEL_SIZE, font_val);
                }
            }
        }

        FcPatternAddBool(fc_pattern, FC_SCALABLE, 1);

        FcConfigSubstitute(NULL, fc_pattern, FcMatchPattern);
        XftDefaultSubstitute(x_window.display, x_window.screen, fc_pattern);

        if (x_load_spare_font(fc_pattern, FRC_NORMAL)) {
            error("can't open spare font %s\n", font_name);
            exit(EXIT_FAILURE);
        }

        FcPatternDel(fc_pattern, FC_SLANT);
        FcPatternAddInteger(fc_pattern, FC_SLANT, FC_SLANT_ITALIC);
        if (x_load_spare_font(fc_pattern, FRC_ITALIC)) {
            error("can't open spare font %s\n", font_name);
            exit(EXIT_FAILURE);
        }

        FcPatternDel(fc_pattern, FC_WEIGHT);
        FcPatternAddInteger(fc_pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
        if (x_load_spare_font(fc_pattern, FRC_ITALICBOLD)) {
            error("can't open spare font %s\n", font_name);
            exit(EXIT_FAILURE);
        }

        FcPatternDel(fc_pattern, FC_SLANT);
        FcPatternAddInteger(fc_pattern, FC_SLANT, FC_SLANT_ROMAN);
        if (x_load_spare_font(fc_pattern, FRC_BOLD)) {
            error("can't open spare font %s\n", font_name);
            exit(EXIT_FAILURE);
        }

        FcPatternDestroy(fc_pattern);
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
    while (frc_len > 0) {
        frc_len -= 1;
        XftFontClose(x_window.display, frc[frc_len].font);
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
    int32 win_x = term_window.hborderpx + x*term_window.cw;
    int32 win_y = term_window.vborderpx + y*term_window.ch;
    enum GlyphAttribute prevmode = ATTR_LAST;
    StFont *font_local = &draw_context.font;
    int32 frc_flags = FRC_NORMAL;
    int32 rune_width = term_window.cw;
    int32 nfont_specs = 0;
    int32 xp = win_x;
    int32 yp = win_y + font_local->ascent;

    for (int32 i = 0; i < len; i += 1) {
        uint32 rune = glyphs[i].rune;
        enum GlyphAttribute mode = glyphs[i].mode;
        FT_UInt glyph_idx;
        int32 nfonts = 0;

        if (mode == ATTR_WDUMMY) {
            continue;
        }

        if (prevmode != mode) {
            prevmode = mode;
            font_local = &draw_context.font;
            frc_flags = FRC_NORMAL;
            if (mode & ATTR_WIDE) {
                rune_width = term_window.cw*2;
            } else {
                rune_width = term_window.cw;
            }
            if (mode & ATTR_ITALIC) {
                if (mode & ATTR_BOLD) {
                    font_local = &draw_context.ibfont;
                    frc_flags = FRC_ITALICBOLD;
                } else {
                    font_local = &draw_context.ifont;
                    frc_flags = FRC_ITALIC;
                }
            } else if (mode & ATTR_BOLD) {
                font_local = &draw_context.bfont;
                frc_flags = FRC_BOLD;
            }
            yp = win_y + font_local->ascent;
        }

        if (mode & ATTR_BOXDRAW) {
            glyph_idx = boxdrawindex(&glyphs[i]);
        } else {
            glyph_idx = XftCharIndex(x_window.display, font_local->match, rune);
        }

        if (glyph_idx) {
            specs[nfont_specs].font = font_local->match;
            specs[nfont_specs].glyph = glyph_idx;
            specs[nfont_specs].x = (int16)xp;
            specs[nfont_specs].y = (int16)yp;
            xp += rune_width;
            nfont_specs += 1;
            continue;
        }

        while (nfonts < frc_len) {
            glyph_idx = XftCharIndex(x_window.display, frc[nfonts].font, rune);
            if (glyph_idx) {
                if (frc[nfonts].flags == frc_flags) {
                    break;
                }
            }
            if (!glyph_idx) {
                if (frc[nfonts].flags == frc_flags) {
                    if (frc[nfonts].unicodep == rune) {
                        break;
                    }
                }
            }
            nfonts += 1;
        }

        if (nfonts >= frc_len) {
            FcResult fc_result;
            FcPattern *fc_pattern;
            FcPattern *fontpattern;
            FcFontSet *fcsets[] = {NULL};
            FcCharSet *fc_charset;

            if (!font_local->set) {
                font_local->set = FcFontSort(0, font_local->pattern, 1, 0, &fc_result);
            }
            fcsets[0] = font_local->set;

            fc_pattern = FcPatternDuplicate(font_local->pattern);
            fc_charset = FcCharSetCreate();

            FcCharSetAddChar(fc_charset, rune);
            FcPatternAddCharSet(fc_pattern, FC_CHARSET, fc_charset);
            FcPatternAddBool(fc_pattern, FC_SCALABLE, 1);

            FcConfigSubstitute(0, fc_pattern, FcMatchPattern);
            FcDefaultSubstitute(fc_pattern);

            fontpattern = FcFontSetMatch(0, fcsets, 1, fc_pattern, &fc_result);

            if (frc_len >= frc_cap) {
                frc_cap += 16;
                frc = xrealloc(frc, frc_cap*SIZEOF(FontCache));
            }

            frc[frc_len].font = XftFontOpenPattern(x_window.display, fontpattern);
            if (!frc[frc_len].font) {
                error("XftFontOpenPattern failed seeking fallback font: %s\n",
                      strerror(errno));
                exit(EXIT_FAILURE);
            }
            frc[frc_len].flags = frc_flags;
            frc[frc_len].unicodep = rune;

            glyph_idx = XftCharIndex(x_window.display, frc[frc_len].font, rune);

            nfonts = frc_len;
            frc_len += 1;

            FcPatternDestroy(fc_pattern);
            FcCharSetDestroy(fc_charset);
        }

        specs[nfont_specs].font = frc[nfonts].font;
        specs[nfont_specs].glyph = glyph_idx;
        specs[nfont_specs].x = (int16)xp;
        specs[nfont_specs].y = (int16)yp;
        xp += rune_width;
        nfont_specs += 1;
    }

    return nfont_specs;
}

static void
x_draw_glyph_font_specs(XftGlyphFontSpec *specs,
                        StGlyph base, int32 len, int32 x, int32 y) {
    int32 char_len;
    int32 win_x = term_window.hborderpx + x*term_window.cw;
    int32 win_y = term_window.vborderpx + y*term_window.ch;
    int32 width;
    XftColor *fg;
    XftColor *bg;
    XftColor true_fg;
    XftColor true_bg;
    XRenderColor color_fg;
    XRenderColor color_bg;

    if (base.mode & ATTR_WIDE) {
        char_len = len*2;
    } else {
        char_len = len;
    }
    width = char_len*term_window.cw;

    if (base.mode & ATTR_ITALIC) {
        if (base.mode & ATTR_BOLD) {
            if (draw_context.ibfont.bad_slant || draw_context.ibfont.bad_weight) {
                base.fg = (int32)CONF_DEFAULT_ATTR;
            }
        } else if (draw_context.ifont.bad_slant) {
            base.fg = (int32)CONF_DEFAULT_ATTR;
        }
    } else if (base.mode & ATTR_BOLD) {
        if (draw_context.bfont.bad_weight) {
            base.fg = (int32)CONF_DEFAULT_ATTR;
        }
    }

    if (IS_TRUECOL(base.fg)) {
        color_fg.alpha = 0xffff;
        color_fg.red = TRUE_RED(base.fg);
        color_fg.green = TRUE_GREEN(base.fg);
        color_fg.blue = TRUE_BLUE(base.fg);
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &color_fg, &true_fg);
        fg = &true_fg;
    } else {
        fg = &draw_context.colors[base.fg];
    }

    if (IS_TRUECOL(base.bg)) {
        color_bg.alpha = 0xffff;
        color_bg.red = TRUE_RED(base.bg);
        color_bg.green = TRUE_GREEN(base.bg);
        color_bg.blue = TRUE_BLUE(base.bg);
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &color_bg, &true_bg);
        bg = &true_bg;
    } else {
        bg = &draw_context.colors[base.bg];
    }

    if (win_mode_is_set(WIN_MODE_REVERSE)) {
        XftColor rev_fg;
        XftColor rev_bg;
        if (fg == &draw_context.colors[CONF_COLOR_INDEX_FONT]) {
            fg = &draw_context.colors[CONF_COLOR_BG];
        } else {
            color_fg.red = (ushort)~fg->color.red;
            color_fg.green = (ushort)~fg->color.green;
            color_fg.blue = (ushort)~fg->color.blue;
            color_fg.alpha = (ushort)fg->color.alpha;
            XftColorAllocValue(x_window.display, x_window.visual,
                               x_window.color_map, &color_fg, &rev_fg);
            fg = &rev_fg;
        }

        if (bg == &draw_context.colors[CONF_COLOR_BG]) {
            bg = &draw_context.colors[CONF_COLOR_INDEX_FONT];
        } else {
            color_bg.red = (ushort)~bg->color.red;
            color_bg.green = (ushort)~bg->color.green;
            color_bg.blue = (ushort)~bg->color.blue;
            color_bg.alpha = (ushort)bg->color.alpha;
            XftColorAllocValue(x_window.display, x_window.visual,
                               x_window.color_map, &color_bg, &rev_bg);
            bg = &rev_bg;
        }
    }

    if ((base.mode & ATTR_BOLD_FAINT) == ATTR_FAINT) {
        XftColor rev_fg;
        color_fg.red = fg->color.red / 2;
        color_fg.green = fg->color.green / 2;
        color_fg.blue = fg->color.blue / 2;
        color_fg.alpha = fg->color.alpha;
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &color_fg, &rev_fg);
        fg = &rev_fg;
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
        if (win_y + term_window.ch >= term_window.vborderpx + term_window.tty_height) {
            limit_y = term_window.h;
        } else {
            limit_y = 0;
        }
        x_clear(0, (y == 0) ? 0 : win_y, term_window.hborderpx, win_y + term_window.ch + limit_y);
    }
    if (win_x + width >= term_window.hborderpx + term_window.tty_width) {
        int32 limit_y;
        if (win_y + term_window.ch >= term_window.vborderpx + term_window.tty_height) {
            limit_y = term_window.h;
        } else {
            limit_y = win_y + term_window.ch;
        }
        x_clear((int32)MIN(win_x + width, term_window.w), (y == 0) ? 0 : win_y, term_window.w, limit_y);
    }
    if (y == 0) {
        x_clear(win_x, 0, win_x + width, term_window.vborderpx);
    }
    if (win_y + term_window.ch >= term_window.vborderpx + term_window.tty_height) {
        x_clear(win_x, win_y + term_window.ch, win_x + width, term_window.h);
    }

    /* Clean up the region we want to draw to. */
    XftDrawRect(x_window.xft_draw, bg, win_x, win_y, (uint32)width,
                (uint32)term_window.ch);

    {
        /* Set the clip region because Xft is sometimes dirty. */
        XRectangle rect;
        rect.x = 0;
        rect.y = 0;
        rect.height = (uint16)term_window.ch;
        rect.width = (uint16)width;
        XftDrawSetClipRectangles(x_window.xft_draw, win_x, win_y, &rect, 1);
    }

    if (base.mode & ATTR_BOXDRAW) {
        drawboxes(win_x, win_y, width / len, term_window.ch, fg, bg, specs, len);
    } else {
        XftDrawGlyphFontSpec(x_window.xft_draw, fg, specs, len);
    }

    if (base.mode & ATTR_UNDERLINE) {
        XftDrawRect(x_window.xft_draw, fg, win_x,
                    win_y + (int32)((double)draw_context.font.ascent*CONF_CHAR_HEIGHT_SCALE) + 1,
                    (uint32)width, 1);
    }

    if (base.mode & ATTR_STRUCK) {
        XftDrawRect(x_window.xft_draw, fg, win_x,
                    win_y + 2*(int32)((double)draw_context.font.ascent*CONF_CHAR_HEIGHT_SCALE / 3),
                    (uint32)width, 1);
    }

    XftDrawSetClip(x_window.xft_draw, 0);
    return;
}

static void
x_draw_glyph(StGlyph glyph, int32 x, int32 y) {
    int32 nfont_specs;
    XftGlyphFontSpec xft_glyph_font_spec;

    nfont_specs = x_make_glyph_font_specs(&xft_glyph_font_spec, &glyph, 1, x, y);
    x_draw_glyph_font_specs(&xft_glyph_font_spec, glyph, nfont_specs, x, y);
    return;
}

static void
x_draw_cursor(int32 cx, int32 cy, StGlyph glyph,
              int32 ox, int32 oy, StGlyph og) {
    XftColor draw_color;

    if (selection_is_selected(ox, oy)) {
        og.mode |= ATTR_SELECTED;
    }
    x_draw_glyph(og, ox, oy);

    if (win_mode_is_set(WIN_MODE_HIDE)) {
        return;
    }

    glyph.mode &= ATTR_BOLD | ATTR_ITALIC | ATTR_UNDERLINE | ATTR_STRUCK | ATTR_WIDE
              | ATTR_BOXDRAW;

    if (win_mode_is_set(WIN_MODE_REVERSE)) {
        glyph.mode |= ATTR_REVERSE;
        glyph.fg = CONF_COLOR_INDEX_CURSOR;
        glyph.bg = CONF_COLOR_INDEX_FONT;
        draw_color = draw_context.colors[CONF_COLOR_INDEX_REVCURSOR];
    } else {
        glyph.fg = CONF_COLOR_BG;
        glyph.bg = CONF_COLOR_INDEX_CURSOR;
        draw_color = draw_context.colors[CONF_COLOR_INDEX_CURSOR];
    }

    if (win_mode_is_set(WIN_MODE_FOCUSED)) {
        switch (term_window.cursor) {
        case 7:
            glyph.rune = 0x2603;
            _X_FALLTHROUGH;
        case 0:
        case 1:
        case 2:
            x_draw_glyph(glyph, cx, cy);
            break;
        case 3:
        case 4:
            XftDrawRect(x_window.xft_draw, &draw_color,
                        term_window.hborderpx + cx*term_window.cw,
                        term_window.vborderpx + (cy + 1)*term_window.ch - (int32)CONF_CURSOR_THICKNESS,
                        (uint32)term_window.cw, (uint32)CONF_CURSOR_THICKNESS);
            break;
        case 5:
        case 6:
            XftDrawRect(x_window.xft_draw, &draw_color,
                        term_window.hborderpx + cx*term_window.cw,
                        term_window.vborderpx + cy*term_window.ch,
                        CONF_CURSOR_THICKNESS, (uint32)term_window.ch);
            break;
        default:
            error("x_draw_cursor: Unhandled switch case.\n");
            break;
        }
    } else {
        XftDrawRect(x_window.xft_draw, &draw_color,
                    term_window.hborderpx + cx*term_window.cw,
                    term_window.vborderpx + cy*term_window.ch,
                    (uint32)(term_window.cw - 1), 1);
        XftDrawRect(x_window.xft_draw, &draw_color,
                    term_window.hborderpx + cx*term_window.cw,
                    term_window.vborderpx + cy*term_window.ch, 1,
                    (uint32)(term_window.ch - 1));
        XftDrawRect(x_window.xft_draw, &draw_color,
                    term_window.hborderpx + (cx + 1)*term_window.cw - 1,
                    term_window.vborderpx + cy*term_window.ch, 1,
                    (uint32)(term_window.ch - 1));
        XftDrawRect(x_window.xft_draw, &draw_color,
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
x_set_title(char *title) {
    XTextProperty text_property;
    if (!title) {
        title = opt_title;
    }
    if (title[0] == '\0') {
        title = opt_title;
    }

    if (Xutf8TextListToTextProperty(x_window.display,
                                    &title, 1,
                                    XUTF8StringStyle, &text_property) != Success) {
        return;
    }
    XSetWMName(x_window.display, x_window.win, &text_property);
    XSetTextProperty(x_window.display, x_window.win,
                     &text_property, x_window.net_wm_name);
    XFree(text_property.value);
    return;
}

static int32
x_start_draw(void) {
    return win_mode_is_set(WIN_MODE_VISIBLE);
}

static void
x_draw_line(StGlyph *line, int32 x1, int32 y1, int32 x2) {
    int32 i;
    int32 ox;
    int32 nfont_specs;
    StGlyph base = {0};
    XftGlyphFontSpec *font_specs = x_window.font_spec_buf;

    nfont_specs = x_make_glyph_font_specs(font_specs, &line[x1], x2 - x1, x1, y1);
    i = 0;
    ox = 0;
    for (int32 x = x1; x < x2 && i < nfont_specs; x += 1) {
        StGlyph new = line[x];
        if (new.mode == ATTR_WDUMMY) {
            continue;
        }
        if (selection_is_selected(x, y1)) {
            new.mode |= ATTR_SELECTED;
        }
        if ((i > 0) && ATTRCMP(base, new)) {
            x_draw_glyph_font_specs(font_specs, base, i, ox, y1);
            font_specs += i;
            nfont_specs -= i;
            i = 0;
        }
        if (i == 0) {
            ox = x;
            base = new;
        }
        i += 1;
    }
    if (i > 0) {
        x_draw_glyph_font_specs(font_specs, base, i, ox, y1);
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
    XWMHints *wm_hints = XGetWMHints(x_window.display, x_window.win);
    MODBIT(wm_hints->flags, add, XUrgencyHint);
    XSetWMHints(x_window.display, x_window.win, wm_hints);
    XFree(wm_hints);
    return;
}

static void
x_bell(void) {
    if (!(win_mode_is_set(WIN_MODE_FOCUSED))) {
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

        if (XMatchVisualInfo(x_window.display, x_window.screen,
                             32, TrueColor, &visual) != 0) {
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

        x_window.color_map = XCreateColormap(x_window.display,
                                             parent, x_window.visual, None);

        term_window.w = 800;
        term_window.h = 600;
        term_window.cw = 10;
        term_window.ch = 20;

        x_window.attrs.colormap = x_window.color_map;
        x_window.attrs.background_pixel = 0;
        x_window.attrs.border_pixel = 0;
        x_window.attrs.bit_gravity = NorthWestGravity;
        x_window.attrs.event_mask = FocusChangeMask
                                    | KeyPressMask
                                    | ExposureMask
                                    | StructureNotifyMask
                                    | PointerMotionMask;

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
        draw_context.graphics = XCreateGC(x_window.display, x_window.win,
                                          GCGraphicsExposures, &xgc_values);

        x_window.drawable = XCreatePixmap(x_window.display, x_window.win,
                                          (uint32)term_window.w,
                                          (uint32)term_window.h,
                                          (uint32)x_window.depth);

        x_window.xft_draw = XftDrawCreate(x_window.display, x_window.drawable,
                                          x_window.visual, x_window.color_map);

        x_window.xembed
            = XInternAtom(x_window.display, "_XEMBED", False);
        x_window.wm_delete_win
            = XInternAtom(x_window.display, "WM_DELETE_WINDOW", False);
        x_window.net_wm_name
            = XInternAtom(x_window.display, "_NET_WM_NAME", False);
        x_window.net_wm_iconname
            = XInternAtom(x_window.display, "_NET_WM_ICON_NAME", False);
        x_window.net_wm_pid
            = XInternAtom(x_window.display, "_NET_WM_PID", False);

        xsel.xtarget = XInternAtom(x_window.display, "UTF8_STRING", 0);
        if (xsel.xtarget == None) {
            xsel.xtarget = XA_STRING;
        }

        CONF_NCOLS = 80;
        CONF_NROWS = 24;
        term_allocate();

        term_window.tty_width = term_window.cw*term.ncols;
        term_window.tty_height = term_window.ch*term.nrows;
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

        x_load_colors();

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
        FcPattern *fc_pattern = FcNameParse((FcChar8 *)"monospace");

        x_load_spare_font(fc_pattern, 0);
        FcPatternDestroy(fc_pattern);
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

    XCloseDisplay(x_window.display);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_x */

#endif /* X_C */
