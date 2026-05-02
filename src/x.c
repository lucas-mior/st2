#if !defined(X_C)
#define X_C

#include "st.h"
#include "math.h"

uint16
sixd_to_16bit(int32 x) {
    int32 y;
    if (x == 0) {
        y = 0;
    } else {
        y = 0x3737 + 0x2828*x;
    }
    return (uint16)y;
}

void
x_resize(int32 col, int32 row) {
    term_window.tty_width = col*term_window.cw;
    term_window.tty_height = row*term_window.ch;

    XFreePixmap(x_window.display, x_window.drawable);
    x_window.drawable
        = XCreatePixmap(x_window.display, x_window.win, (uint32)term_window.w,
                        (uint32)term_window.h, (uint32)x_window.depth);
    XftDrawChange(x_window.xft_draw, x_window.drawable);
    x_clear(0, 0, term_window.w, term_window.h);

    /* x_window.specbuf resize */
    x_window.specbuf
        = xrealloc(x_window.specbuf, (int64)col*SIZEOF(XftGlyphFontSpec));
    return;
}

int32
x_get_color(int32 x, uint *r, uint *g, uint *b) {
    if (!BETWEEN(x, 0, draw_context.colors_len - 1)) {
        return 1;
    }

    *r = draw_context.colors[x].color.red >> 8;
    *g = draw_context.colors[x].color.green >> 8;
    *b = draw_context.colors[x].color.blue >> 8;

    return 0;
}

int32
x_load_color(int32 i, char *name, XftColor *ncolor) {
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
                                      x_window.color_map, &color, ncolor);
        } else {
            name = CONF_COLORS[i];
        }
    }

    return XftColorAllocName(x_window.display, x_window.visual,
                             x_window.color_map, name, ncolor);
}

void
x_load_cols(void) {
    static int32 loaded = 0;
    XftColor *cp;

    if (loaded) {
        for (cp = draw_context.colors;
             cp < &draw_context.colors[draw_context.colors_len]; cp += 1) {
            XftColorFree(x_window.display, x_window.visual, x_window.color_map,
                         cp);
        }
    } else {
        draw_context.colors_len = (int32)MAX(LENGTH(CONF_COLORS), 256);
        draw_context.colors
            = xmalloc((uint16)draw_context.colors_len*SIZEOF(XftColor));
    }

    for (int32 i = 0; i < draw_context.colors_len; i += 1) {
        if (!x_load_color(i, NULL, &draw_context.colors[i])) {
            if (CONF_COLORS[i]) {
                die("could not allocate color '%s'\n", CONF_COLORS[i]);
            } else {
                die("could not allocate color %d\n", i);
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

int32
x_set_color_name(int32 x, char *name) {
    XftColor ncolor;

    if (!BETWEEN(x, 0, draw_context.colors_len - 1)) {
        return 1;
    }

    if (!x_load_color(x, name, &ncolor)) {
        return 1;
    }

    XftColorFree(x_window.display, x_window.visual, x_window.color_map,
                 &draw_context.colors[x]);
    draw_context.colors[x] = ncolor;

    if (x == CONF_COLOR_BG) {
        draw_context.colors[CONF_COLOR_BG].color.alpha
            = (uint16)(0xffff*CONF_ALPHA);
        draw_context.colors[CONF_COLOR_BG].pixel &= 0x00FFFFFF;
        draw_context.colors[CONF_COLOR_BG].pixel
            |= ((uint32)(0xff*CONF_ALPHA) & 0xff) << 24;
    }

    return 0;
}

void
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

void
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

int32
x_geom_mask_to_gravity(int32 mask) {
    switch (mask & (XNegative | YNegative)) {
    case 0:
        return NorthWestGravity;
    case XNegative:
        return NorthEastGravity;
    case YNegative:
        return SouthWestGravity;
    default:
        fprintf(stderr, "x_geom_mask_to_gravity: Unhandled switch case.\n");
        break;
    }

    return SouthEastGravity;
}

int32
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
                       (const FcChar8 *)CONF_ASCII_PRINTABLE,
                       (int32)(int64)strlen(CONF_ASCII_PRINTABLE), &extents);

    f->set = NULL;
    f->pattern = configured;

    f->ascent = f->match->ascent;
    f->descent = f->match->descent;
    f->lbearing = 0;
    f->rbearing = (int16)f->match->max_advance_width;

    f->height = f->ascent + f->descent;
    f->width
        = DIVCEIL(extents.xOff, (int32)(int64)strlen(CONF_ASCII_PRINTABLE));

    return 0;
}

void
x_load_fonts(char *fontstr, float fontsize) {
    FcPattern *pattern;
    double fontval;

    if (fontstr[0] == '-') {
        pattern = XftXlfdParse(fontstr, False, False);
    } else {
        pattern = FcNameParse((const FcChar8 *)fontstr);
    }

    if (!pattern) {
        die("can't open font %s\n", fontstr);
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
        die("can't open font %s\n", fontstr);
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
        die("can't open font %s\n", fontstr);
    }

    FcPatternDel(pattern, FC_WEIGHT);
    FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
    if (x_load_font(&draw_context.ibfont, pattern)) {
        die("can't open font %s\n", fontstr);
    }

    FcPatternDel(pattern, FC_SLANT);
    FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
    if (x_load_font(&draw_context.bfont, pattern)) {
        die("can't open font %s\n", fontstr);
    }

    FcPatternDestroy(pattern);
    return;
}

int32
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

void
x_load_spare_fonts(void) {
    FcPattern *pattern;
    double sizeshift;
    double fontval;
    int32 fc;
    char **fp;

    if (frclen != 0) {
        die("can't embed spare fonts. cache isn't empty");
    }

    /* Calculate count of spare fonts */
    fc = SIZEOF(CONF_FONT2) / SIZEOF(*CONF_FONT2);
    if (fc == 0) {
        return;
    }

    /* Allocate memory for cache entries. */
    if (frccap < 4*fc) {
        frccap += 4*fc - frccap;
        frc = xrealloc(frc, (int64)frccap*SIZEOF(Fontcache));
    }

    for (fp = CONF_FONT2; fp - CONF_FONT2 < fc; fp += 1) {

        if (**fp == '-') {
            pattern = XftXlfdParse(*fp, False, False);
        } else {
            pattern = FcNameParse((FcChar8 *)*fp);
        }

        if (!pattern) {
            die("can't open spare font %s\n", *fp);
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
            die("can't open spare font %s\n", *fp);
        }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
        if (xloadsparefont(pattern, FRC_ITALIC)) {
            die("can't open spare font %s\n", *fp);
        }

        FcPatternDel(pattern, FC_WEIGHT);
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
        if (xloadsparefont(pattern, FRC_ITALICBOLD)) {
            die("can't open spare font %s\n", *fp);
        }

        FcPatternDel(pattern, FC_SLANT);
        FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
        if (xloadsparefont(pattern, FRC_BOLD)) {
            die("can't open spare font %s\n", *fp);
        }

        FcPatternDestroy(pattern);
    }
    return;
}

void
x_unload_font(StFont *f) {
    XftFontClose(x_window.display, f->match);
    FcPatternDestroy(f->pattern);
    if (f->set) {
        FcFontSetDestroy(f->set);
    }
    return;
}

void
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

int32
x_im_open(Display *display) {
    XIMCallback imdestroy = {.client_data = NULL, .callback = x_im_destroy};
    XICCallback icdestroy = {.client_data = NULL, .callback = x_ic_destroy};
    (void)display;

    x_window.ime.xim = XOpenIM(x_window.display, NULL, NULL, NULL);
    if (x_window.ime.xim == NULL) {
        return 0;
    }

    if (XSetIMValues(x_window.ime.xim, XNDestroyCallback, &imdestroy, NULL)) {
        fprintf(stderr, "XSetIMValues: Could not set XNDestroyCallback.\n");
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
        fprintf(stderr, "XCreateIC: Could not create input context.\n");
    }

    return 1;
}

void
x_im_instantiate(Display *display, XPointer client, XPointer call) {
    (void)client;
    (void)call;
    if (x_im_open(display)) {
        XUnregisterIMInstantiateCallback(x_window.display, NULL, NULL, NULL,
                                         x_im_instantiate, NULL);
    }
    return;
}

void
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

int32
x_ic_destroy(XIC xim, XPointer client, XPointer call) {
    (void)xim;
    (void)client;
    (void)call;
    x_window.ime.xic = NULL;
    return 1;
}

int32
x_make_glyph_font_specs(XftGlyphFontSpec *specs, Glyph *glyphs, int32 len,
                        int32 x, int32 y) {
    int32 winx = term_window.hborderpx + x*term_window.cw;
    int32 winy = term_window.vborderpx + y*term_window.ch;
    uint16 mode;
    uint16 prevmode = USHRT_MAX;
    StFont *font_local = &draw_context.font;
    int32 frcflags = FRC_NORMAL;
    int32 runewidth = term_window.cw;
    uint32 rune;
    FT_UInt glyphidx;
    FcResult fcres;
    FcPattern *fcpattern;
    FcPattern *fontpattern;
    FcFontSet *fcsets[] = {NULL};
    FcCharSet *fccharset;
    int32 f;
    int32 numspecs = 0;
    int32 xp = winx;
    int32 yp = winy + font_local->ascent;

    for (int32 i = 0; i < len; i += 1) {
        rune = glyphs[i].rune;
        mode = glyphs[i].mode;

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
                frc = xrealloc(frc, (int64)frccap*SIZEOF(Fontcache));
            }

            frc[frclen].font = XftFontOpenPattern(x_window.display, fontpattern);
            if (!frc[frclen].font) {
                die("XftFontOpenPattern failed seeking fallback font: %s\n",
                    strerror(errno));
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

void
x_draw_glyph_font_specs(XftGlyphFontSpec *specs, Glyph base, int32 len, int32 x,
                        int32 y) {
    int32 charlen;
    int32 winx = term_window.hborderpx + x*term_window.cw;
    int32 winy = term_window.vborderpx + y*term_window.ch;
    int32 width;
    XftColor *fg;
    XftColor *bg;
    XftColor *temp;
    XftColor revfg;
    XftColor revbg;
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
        colfg.red = fg->color.red / 2;
        colfg.green = fg->color.green / 2;
        colfg.blue = fg->color.blue / 2;
        colfg.alpha = fg->color.alpha;
        XftColorAllocValue(x_window.display, x_window.visual,
                           x_window.color_map, &colfg, &revfg);
        fg = &revfg;
    }

    if (base.mode & ATTR_REVERSE) {
        temp = fg;
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

    /* Render underline and strikethrough. */
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

void
x_draw_glyph(Glyph g, int32 x, int32 y) {
    int32 numspecs;
    XftGlyphFontSpec spec;

    numspecs = x_make_glyph_font_specs(&spec, &g, 1, x, y);
    x_draw_glyph_font_specs(&spec, g, numspecs, x, y);
    return;
}

void
x_draw_cursor(int32 cx, int32 cy, Glyph g, int32 ox, int32 oy, Glyph og) {
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
            fprintf(stderr, "x_draw_cursor: Unhandled switch case.\n");
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

void
x_set_icon_title(char *p) {
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
    XSetWMIconName(x_window.display, x_window.win, &prop);
    XSetTextProperty(x_window.display, x_window.win, &prop, x_window.net_wm_iconname);
    XFree(prop.value);
    return;
}

void
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

int32
x_start_draw(void) {
    return TERM_WINDOW_IS_SET(WIN_MODE_VISIBLE);
}

void
x_draw_line(Glyph *line, int32 x1, int32 y1, int32 x2) {
    int32 i;
    int32 x;
    int32 ox;
    int32 numspecs;
    Glyph base = {0};
    Glyph new = {0};
    XftGlyphFontSpec *specs = x_window.specbuf;

    numspecs = x_make_glyph_font_specs(specs, &line[x1], x2 - x1, x1, y1);
    i = 0;
    ox = 0;
    for (x = x1; x < x2 && i < numspecs; x += 1) {
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

void
x_finish_draw(void) {
    ImageList *im;
    ImageList *next;
    XGCValues gcvalues;
    GC gc = NULL;
    int32 rel_y;
    int32 desty;
    int32 width;
    int32 height;
    int32 bw = term_window.hborderpx;
    int32 bh = term_window.vborderpx;
    XImage ximage;

    XSetClipMask(x_window.display, draw_context.graphics, None);

    for (im = term.images; im; im = next) {
        next = im->next;
        rel_y = im->y - term.lines_scrolled_up;

        if (im->x >= term.ncols || rel_y >= term.nrows || rel_y < 0) {
            continue;
        }

        width = im->width;
        height = im->height;

        if (im->pixmap == NULL) {
            im->pixmap = (void *)XCreatePixmap(x_window.display, x_window.win,
                                               (uint32)width, (uint32)height,
                                               (uint32)x_window.depth);

            if (im->transparent) {
                im->clipmask = (void *)sixel_create_clipmask((char *)im->pixels,
                                                             width, height);
            }

            ximage.format = ZPixmap;
            ximage.data = (char *)im->pixels;
            ximage.width = width;
            ximage.height = height;
            ximage.depth = x_window.depth;
            ximage.bits_per_pixel = 32;
            ximage.bytes_per_line = width*4;
            ximage.byte_order = ImageByteOrder(x_window.display);
            ximage.bitmap_unit = 32;
            ximage.bitmap_pad = 32;

            XPutImage(x_window.display, (Drawable)im->pixmap,
                      draw_context.graphics, &ximage, 0, 0, 0, 0, (uint32)width,
                      (uint32)height);
        }

        if (gc == NULL) {
            memset(&gcvalues, 0, SIZEOF(gcvalues));
            gcvalues.graphics_exposures = False;
            gc = XCreateGC(x_window.display, x_window.win, GCGraphicsExposures,
                            &gcvalues);
        }

        desty = bh + rel_y*term_window.ch;

        if (im->transparent && im->clipmask) {
            XSetClipOrigin(x_window.display, gc, bw + im->x*term_window.cw, desty);
            XSetClipMask(x_window.display, gc, (Pixmap)im->clipmask);
        } else {
            XSetClipMask(x_window.display, gc, None);
        }

        XCopyArea(x_window.display, (Drawable)im->pixmap, x_window.drawable, gc,
                  0, 0, (uint32)width, (uint32)height,
                  bw + im->x*term_window.cw, desty);
    }

    if (gc) {
        XFreeGC(x_window.display, gc);
    }

    XCopyArea(x_window.display, x_window.drawable, x_window.win,
              draw_context.graphics, 0, 0, (uint32)term_window.w,
              (uint32)term_window.h, 0, 0);
    return;
}

void
x_xim_spot(int32 x, int32 y) {
    if (x_window.ime.xic == NULL) {
        return;
    }

    x_window.ime.point.x = (int16)(CONF_BORDER_PIXELS + x*term_window.cw);
    x_window.ime.point.y = (int16)(CONF_BORDER_PIXELS + (y + 1)*term_window.ch);

    XSetICValues(x_window.ime.xic, XNPreeditAttributes, x_window.ime.spotlist,
                  NULL);
    return;
}

void
x_set_pointer_motion(int32 set) {
    MODBIT(x_window.attrs.event_mask, set, PointerMotionMask);
    XChangeWindowAttributes(x_window.display, x_window.win, CWEventMask,
                            &x_window.attrs);
    return;
}

void
x_set_mode(int32 set, uint32 flags) {
    int32 mode = term_window.mode;
    MODBIT(term_window.mode, set, flags);
    if ((term_window.mode & WIN_MODE_REVERSE) != (mode & WIN_MODE_REVERSE)) {
        redraw();
    }
    return;
}

int32
x_set_cursor(int32 cursor) {
    if (!BETWEEN(cursor, 0, 7)) {
        return 1;
    }
    term_window.cursor = cursor;
    return 0;
}

void
x_set_urgency(int32 add) {
    XWMHints *h = XGetWMHints(x_window.display, x_window.win);
    MODBIT(h->flags, add, XUrgencyHint);
    XSetWMHints(x_window.display, x_window.win, h);
    XFree(h);
    return;
}

void
x_bell(void) {
    if (!(TERM_WINDOW_IS_SET(WIN_MODE_FOCUSED))) {
        x_set_urgency(1);
    }
    if (CONF_BELL_VOLUME) {
        XkbBell(x_window.display, x_window.win, CONF_BELL_VOLUME, (Atom)NULL);
    }
    return;
}

#endif /* X_C */
