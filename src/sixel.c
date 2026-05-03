// sixel.c (part of mintty)
// originally written by kmiya@cluti
// (https://github.com/saitoha/sixel/blob/master/fromsixel.c) Licensed under the
// terms of the GNU General Public License v3 or later.

#if !defined(SIXEL_C)
#define SIXEL_C

#include <stdlib.h>
#include <string.h>

#include "st.h"
#include "sixel.h"
#include "cbase/minmax.c"
#include "util.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_sixel 1
#elif !defined(TESTING_sixel)
#define TESTING_sixel 0
#endif

static uint32 hls_to_rgb(uint32 hue, uint32 lum, uint32 sat);

#define SIXEL_RGB(r, g, b) \
    ((255u << 24) + (((uint32)r) << 16) + (((uint32)g) << 8) +  ((uint32)b))
#define SIXEL_PALVAL(n, a, m)  \
    (((n)*(a) + ((m) / 2)) / (m))
#define SIXEL_XRGB(r, g, b) \
    SIXEL_RGB(SIXEL_PALVAL(r, 255, 100), \
              SIXEL_PALVAL(g, 255, 100), \
              SIXEL_PALVAL(b, 255, 100))

static uint32 sixel_default_color_table[] = {
    SIXEL_XRGB(0, 0, 0),    /*  0 Black    */
    SIXEL_XRGB(20, 20, 80), /*  1 Blue     */
    SIXEL_XRGB(80, 13, 13), /*  2 Red      */
    SIXEL_XRGB(20, 80, 20), /*  3 Green    */
    SIXEL_XRGB(80, 20, 80), /*  4 Magenta  */
    SIXEL_XRGB(20, 80, 80), /*  5 Cyan     */
    SIXEL_XRGB(80, 80, 20), /*  6 Yellow   */
    SIXEL_XRGB(53, 53, 53), /*  7 Gray 50% */
    SIXEL_XRGB(26, 26, 26), /*  8 Gray 25% */
    SIXEL_XRGB(33, 33, 60), /*  9 Blue*    */
    SIXEL_XRGB(60, 26, 26), /* 10 Red*     */
    SIXEL_XRGB(33, 60, 33), /* 11 Green*   */
    SIXEL_XRGB(60, 33, 60), /* 12 Magenta* */
    SIXEL_XRGB(33, 60, 60), /* 13 Cyan*    */
    SIXEL_XRGB(60, 60, 33), /* 14 Yellow*  */
    SIXEL_XRGB(80, 80, 80), /* 15 Gray 75% */
};

static void
scroll_images(int32 n) {
    ImageList *next;
    int32 top;
    if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
        top = 0;
    } else {
        top = term.lines_scrolled_up - HISTORY_SIZE;
    }

    for (ImageList *im = term.images; im; im = next) {
        next = im->next;
        im->y += n;

        /* check if the current sixel has exceeded the maximum
         * draw distance, and should therefore be deleted */
        if (im->y < top) {
            // error("im@0x%08x exceeded maximum distance\n");
            delete_image(im);
        }
    }
    return;
}

static void
delete_image(ImageList *im) {
    if (im->prev) {
        im->prev->next = im->next;
    } else {
        term.images = im->next;
    }
    if (im->next) {
        im->next->prev = im->prev;
    }
    if (im->pixmap) {
        XFreePixmap(x_window.display, (Drawable)im->pixmap);
    }
    if (im->clipmask) {
        XFreePixmap(x_window.display, (Drawable)im->clipmask);
    }
    free(im->pixels);
    free(im);
    return;
}

static int32
set_default_color(SixelImage *image) {
    int32 i;
    int32 n;
    int32 r;
    int32 g;
    int32 b;

    /* palette initialization */
    for (n = 1; n < 17; n++) {
        image->palette[n] = sixel_default_color_table[n - 1];
    }

    /* colors 17-232 are a 6x6x6 color cube */
    for (r = 0; r < 6; r++) {
        for (g = 0; g < 6; g++) {
            for (b = 0; b < 6; b++) {
                image->palette[n++] = SIXEL_RGB(r*51, g*51, b*51);
            }
        }
    }

    /* colors 233-256 are a grayscale ramp, intentionally leaving out */
    for (i = 0; i < 24; i++) {
        image->palette[n++] = SIXEL_RGB(i*11, i*11, i*11);
    }

    /* sixels rarely use more than 256 colors and if they do, they use a custom
     * palette, so we don't need to initialize these colors */
    /*
    for (; n < DECSIXEL_PALETTE_MAX; n++) {
            image->palette[n] = SIXEL_RGB(255, 255, 255);
    }
    */

    return (0);
}

static int32
sixel_image_init(SixelImage *image, int32 width, int32 height, int32 fgcolor,
                 int32 bgcolor, int32 use_private_register) {
    int64 size;

    size = (width*height) * SIZEOF(uint32);
    image->width = width;
    image->height = height;
    image->data = xmalloc(size);
    image->ncolors = 2;
    image->use_private_register = use_private_register;

    if (image->data == NULL) {
        return -1;
    }
    memset64(image->data, 0, size);

    image->palette[0] = (uint32)bgcolor;

    if (image->use_private_register) {
        image->palette[1] = (uint32)fgcolor;
    }

    image->palette_modified = 0;

    return 0;
}

static int32
image_buffer_resize(SixelImage *image, int32 width, int32 height) {
    int32 status = (-1);
    int64 size;
    uint16 *alt_buffer;
    int32 min_height;

    size = (width*height)*SIZEOF(uint16);
    alt_buffer = (uint16 *)xmalloc(size);
    if (alt_buffer == NULL) {
        /* free source image */
        free(image->data);
        image->data = NULL;
        status = (-1);
        goto end;
    }

    min_height = height > image->height ? image->height : height;
    if (width > image->width) { /* if width is extended */
        for (int32 n = 0; n < min_height; ++n) {
            /* copy from source image */
            memcpy64(alt_buffer + width*n, image->data + image->width*n,
                     image->width*SIZEOF(uint16));
            /* fill extended area with background color */
            memset64(alt_buffer + width*n + image->width, 0,
                     (width - image->width)*SIZEOF(uint16));
        }
    } else {
        for (int32 n = 0; n < min_height; ++n) {
            /* copy from source image */
            memcpy64(alt_buffer + width*n, image->data + image->width*n,
                     width*SIZEOF(uint16));
        }
    }

    if (height > image->height) { /* if height is extended */
        /* fill extended area with background color */
        memset64(alt_buffer + width*image->height, 0,
                 (width*(height - image->height))*SIZEOF(uint16));
    }

    free(image->data);

    image->data = alt_buffer;
    image->width = width;
    image->height = height;

    status = (0);

end:
    return status;
}

static void
sixel_image_deinit(SixelImage *image) {
    if (image->data) {
        free(image->data);
    }
    image->data = NULL;
    return;
}

static int32
sixel_parser_init(SixelState *sixel_state, int32 transparent, uint32 fgcolor,
                  uint32 bgcolor, uchar use_private_register, int32 cell_width,
                  int32 cell_height) {
    int32 status = (-1);

    sixel_state->state = PS_DECSIXEL;
    sixel_state->pos_x = 0;
    sixel_state->pos_y = 0;
    sixel_state->max_x = 0;
    sixel_state->max_y = 0;
    sixel_state->attributed_pan = 2;
    sixel_state->attributed_pad = 1;
    sixel_state->attributed_ph = 0;
    sixel_state->attributed_pv = 0;
    sixel_state->transparent = transparent;
    sixel_state->repeat_count = 1;
    sixel_state->color_index = 16;
    sixel_state->grid_width = cell_width;
    sixel_state->grid_height = cell_height;
    sixel_state->nparams = 0;
    sixel_state->param = 0;

    /* buffer initialization */
    if (transparent) {
        bgcolor = 0;
    }
    status = sixel_image_init(&sixel_state->image, 1, 1, (int32)fgcolor,
                              (int32)bgcolor, use_private_register);

    return status;
}

static int32
sixel_parser_set_default_color(SixelState *sixel_state) {
    return set_default_color(&sixel_state->image);
}

static int32
sixel_parser_finalize(SixelState *sixel_state, ImageList **newimages, int32 cx,
                      int32 cy, int32 cw, int32 ch) {
    SixelImage *sixel_image = &sixel_state->image;
    int32 x, y;
    uint16 *src;
    uint32 *dst;
    uint32 color;
    int32 w;
    int32 h;
    int32 i;
    int32 j;
    int32 cols;
    int32 numimages;
    char trans;
    ImageList *im;
    ImageList *tail;

    if (!sixel_image->data) {
        return -1;
    }

    if (++sixel_state->max_x < sixel_state->attributed_ph) {
        sixel_state->max_x = sixel_state->attributed_ph;
    }

    if (++sixel_state->max_y < sixel_state->attributed_pv) {
        sixel_state->max_y = sixel_state->attributed_pv;
    }

    if (sixel_image->use_private_register && sixel_image->ncolors > 2
        && !sixel_image->palette_modified) {
        if (set_default_color(sixel_image) < 0) {
            return -1;
        }
    }

    w = (int32)MIN(sixel_state->max_x, sixel_image->width);
    h = (int32)MIN(sixel_state->max_y, sixel_image->height);

    if ((numimages = (h + ch - 1) / ch) <= 0) {
        return -1;
    }

    cols = (w + cw - 1) / cw;

    *newimages = NULL;
    tail = NULL;
    for (y = 0, i = 0; i < numimages; i++) {
        im = xmalloc(sizeof(*im));
        if (!tail) {
            *newimages = tail = im;
            im->prev = im->next = NULL;
        } else {
            tail->next = im;
            im->prev = tail;
            im->next = NULL;
            tail = im;
        }
        im->x = cx;
        im->y = cy + i;
        im->cols = cols;
        im->width = w;
        im->height = (int32)MIN(h - ch*i, ch);
        im->pixels = xmalloc(im->width*im->height * 4);
        im->pixmap = NULL;
        im->clipmask = NULL;
        im->cw = cw;
        im->ch = ch;
        dst = (uint32 *)im->pixels;
        for (trans = 0, j = 0; j < im->height && y < h; j++, y++) {
            src = sixel_state->image.data + sixel_image->width*y;
            for (x = 0; x < w; x++) {
                color = sixel_state->image.palette[*src++];
                trans |= (color == 0);
                *dst++ = color;
            }
        }
        im->transparent = (sixel_state->transparent && trans);
    }

    return numimages;
}

/* convert sixel data into indexed pixel bytes and palette data */
static int32
sixel_parser_parse(SixelState *sixel_state, uchar *p, int32 len) {
    SixelImage *sixel_image = &sixel_state->image;
    int32 n = 0;
    int32 i;
    int32 x;
    int32 bits;
    int32 sx;
    int32 sy;
    int32 width;
    uchar *p0 = p;
    uchar *p2 = p + len;
    uint16 *data;
    int32 color_index;

    if (!sixel_image->data) {
        sixel_state->state = PS_ERROR;
    }

    while (p < p2) {
        switch (sixel_state->state) {
        case PS_ESC:
            goto end;

        case PS_DECSIXEL:
            switch (*p) {
            case '\x1b':
                sixel_state->state = PS_ESC;
                break;
            case '"':
                sixel_state->param = 0;
                sixel_state->nparams = 0;
                sixel_state->state = PS_DECGRA;
                p++;
                break;
            case '!':
                sixel_state->param = 0;
                sixel_state->nparams = 0;
                sixel_state->state = PS_DECGRI;
                p++;
                break;
            case '#':
                sixel_state->param = 0;
                sixel_state->nparams = 0;
                sixel_state->state = PS_DECGCI;
                p++;
                break;
            case '$':
                /* DECGCR Graphics Carriage Return */
                sixel_state->pos_x = 0;
                p++;
                break;
            case '-':
                /* DECGNL Graphics Next Line */
                sixel_state->pos_x = 0;
                if (sixel_state->pos_y < DECSIXEL_HEIGHT_MAX - 5 - 6) {
                    sixel_state->pos_y += 6;
                } else {
                    sixel_state->pos_y = DECSIXEL_HEIGHT_MAX + 1;
                }
                p++;
                break;
            default:
                if (*p >= '?' && *p <= '~') { /* sixel characters */
                    if ((sixel_image->width
                               < (sixel_state->pos_x + sixel_state->repeat_count)
                         || sixel_image->height < (sixel_state->pos_y + 6))
                        && sixel_image->width < DECSIXEL_WIDTH_MAX
                        && sixel_image->height < DECSIXEL_HEIGHT_MAX) {
                        sx = sixel_image->width*2;
                        sy = sixel_image->height*2;
                        while (sx < (sixel_state->pos_x
                                     + sixel_state->repeat_count)
                               || sy < (sixel_state->pos_y + 6)) {
                            sx *= 2;
                            sy *= 2;
                        }

                        sx = (int32)MIN(sx, DECSIXEL_WIDTH_MAX);
                        sy = (int32)MIN(sy, DECSIXEL_HEIGHT_MAX);

                        if (image_buffer_resize(sixel_image, sx, sy) < 0) {
                            perror("sixel_parser_parse() failed");
                            sixel_state->state = PS_ERROR;
                            p++;
                            break;
                        }
                    }

                    if (sixel_state->color_index > sixel_image->ncolors) {
                        sixel_image->ncolors = sixel_state->color_index;
                    }

                    if (sixel_state->pos_x + sixel_state->repeat_count
                        > sixel_image->width) {
                        sixel_state->repeat_count
                            = sixel_image->width - sixel_state->pos_x;
                    }

                    if (sixel_state->repeat_count > 0
                        && sixel_state->pos_y + 5 < sixel_image->height) {
                        bits = *p - '?';
                        if (bits != 0) {
                            data = sixel_image->data
                                   + sixel_image->width*sixel_state->pos_y
                                   + sixel_state->pos_x;
                            width = sixel_image->width;
                            color_index = sixel_state->color_index;
                            if (sixel_state->repeat_count <= 1) {
                                if (bits & 0x01) {
                                    *data = (uint16)color_index;
                                    n = 0;
                                }
                                data += width;
                                if (bits & 0x02) {
                                    *data = (uint16)color_index;
                                    n = 1;
                                }
                                data += width;
                                if (bits & 0x04) {
                                    *data = (uint16)color_index;
                                    n = 2;
                                }
                                data += width;
                                if (bits & 0x08) {
                                    *data = (uint16)color_index;
                                    n = 3;
                                }
                                data += width;
                                if (bits & 0x10) {
                                    *data = (uint16)color_index;
                                    n = 4;
                                }
                                if (bits & 0x20) {
                                    data[width] = (uint16)color_index;
                                    n = 5;
                                }
                                if (sixel_state->max_x < sixel_state->pos_x) {
                                    sixel_state->max_x = sixel_state->pos_x;
                                }
                            } else {
                                /* sixel_state->repeat_count > 1 */
                                for (i = 0; bits;
                                     bits >>= 1, i++, data += width) {
                                    if (bits & 1) {
                                        data[0] = (uint16)color_index;
                                        data[1] = (uint16)color_index;
                                        for (x = 2;
                                             x < sixel_state->repeat_count;
                                             x++) {
                                            data[x] = (uint16)color_index;
                                        }
                                        n = i;
                                    }
                                }
                                if (sixel_state->max_x
                                    < (sixel_state->pos_x
                                        + sixel_state->repeat_count - 1)) {
                                    sixel_state->max_x
                                         = sixel_state->pos_x
                                           + sixel_state->repeat_count - 1;
                                }
                            }
                            if (sixel_state->max_y < (sixel_state->pos_y + n)) {
                                sixel_state->max_y = sixel_state->pos_y + n;
                            }
                        }
                    }
                    if (sixel_state->repeat_count > 0) {
                        sixel_state->pos_x += sixel_state->repeat_count;
                    }
                    sixel_state->repeat_count = 1;
                }
                p++;
                break;
            }
            break;

        case PS_DECGRA:
            /* DECGRA Set Raster Attributes " Pan; Pad; Ph; Pv */
            switch (*p) {
            case '\x1b':
                sixel_state->state = PS_ESC;
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                sixel_state->param = sixel_state->param*10 + *p - '0';
                sixel_state->param = (uint32)MIN(sixel_state->param,
                                                 DECSIXEL_PARAMVALUE_MAX);
                p++;
                break;
            case ';':
                if (sixel_state->nparams < DECSIXEL_PARAMS_MAX) {
                    sixel_state->params[sixel_state->nparams++]
                        = sixel_state->param;
                }
                sixel_state->param = 0;
                p++;
                break;
            default:
                if (sixel_state->nparams < DECSIXEL_PARAMS_MAX) {
                    sixel_state->params[sixel_state->nparams++]
                        = sixel_state->param;
                }
                if (sixel_state->nparams > 0) {
                    sixel_state->attributed_pad = sixel_state->params[0];
                }
                if (sixel_state->nparams > 1) {
                    sixel_state->attributed_pan = sixel_state->params[1];
                }
                if (sixel_state->nparams > 2 && sixel_state->params[2] > 0) {
                    sixel_state->attributed_ph = (int32)sixel_state->params[2];
                }
                if (sixel_state->nparams > 3 && sixel_state->params[3] > 0) {
                    sixel_state->attributed_pv = (int32)sixel_state->params[3];
                }

                if (sixel_state->attributed_pan <= 0) {
                    sixel_state->attributed_pan = 1;
                }
                if (sixel_state->attributed_pad <= 0) {
                    sixel_state->attributed_pad = 1;
                }

                if (sixel_image->width < sixel_state->attributed_ph
                    || sixel_image->height < sixel_state->attributed_pv) {
                    sx = (int32)MAX(sixel_image->width, sixel_state->attributed_ph);
                    sy = (int32)MAX(sixel_image->height, sixel_state->attributed_pv);

                    /* the height of the sixel_image buffer must be divisible by 6
                     * to avoid unnecessary resizing of the sixel_image buffer when
                     * parsing the last sixel line */
                    sy = (sy + 5) / 6*6;

                    sx = (int32)MIN(sx, DECSIXEL_WIDTH_MAX);
                    sy = (int32)MIN(sy, DECSIXEL_HEIGHT_MAX);

                    if (image_buffer_resize(sixel_image, sx, sy) < 0) {
                        perror("sixel_parser_parse() failed");
                        sixel_state->state = PS_ERROR;
                        break;
                    }
                }
                sixel_state->state = PS_DECSIXEL;
                sixel_state->param = 0;
                sixel_state->nparams = 0;
            }
            break;

        case PS_DECGRI:
            /* DECGRI Graphics Repeat Introducer ! Pn Ch */
            switch (*p) {
            case '\x1b':
                sixel_state->state = PS_ESC;
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                sixel_state->param = sixel_state->param*10 + *p - '0';
                sixel_state->param = (uint32)MIN(sixel_state->param,
                                                 DECSIXEL_PARAMVALUE_MAX);
                p++;
                break;
            default:
                sixel_state->repeat_count = (int32)MAX(sixel_state->param, 1);
                sixel_state->state = PS_DECSIXEL;
                sixel_state->param = 0;
                sixel_state->nparams = 0;
                break;
            }
            break;

        case PS_DECGCI:
            /* DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz */
            switch (*p) {
            case '\x1b':
                sixel_state->state = PS_ESC;
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                sixel_state->param = sixel_state->param*10 + *p - '0';
                sixel_state->param
                    = (uint32)MIN(sixel_state->param, DECSIXEL_PARAMVALUE_MAX);
                p++;
                break;
            case ';':
                if (sixel_state->nparams < DECSIXEL_PARAMS_MAX) {
                    sixel_state->params[sixel_state->nparams++]
                        = sixel_state->param;
                }
                sixel_state->param = 0;
                p++;
                break;
            default:
                sixel_state->state = PS_DECSIXEL;
                if (sixel_state->nparams < DECSIXEL_PARAMS_MAX) {
                    sixel_state->params[sixel_state->nparams++]
                        = sixel_state->param;
                }
                sixel_state->param = 0;

                if (sixel_state->nparams > 0) {
                    sixel_state->color_index = (int32)sixel_state->params[0];
                    if (sixel_state->color_index < 0) {
                        sixel_state->color_index = 0;
                    } else if (sixel_state->color_index
                               >= DECSIXEL_PALETTE_MAX) {
                        sixel_state->color_index = DECSIXEL_PALETTE_MAX - 1;
                    }
                    sixel_state->color_index++; /* offset by 1 (background) */
                }

                if (sixel_state->nparams > 4) {
                    sixel_state->image.palette_modified = 1;
                    if (sixel_state->params[1] == 1) {
                        /* HLS */
                        sixel_state->params[2]
                            = (uint32)MIN(sixel_state->params[2], 360);
                        sixel_state->params[3]
                            = (uint32)MIN(sixel_state->params[3], 100);
                        sixel_state->params[4]
                            = (uint32)MIN(sixel_state->params[4], 100);
                        sixel_image->palette[sixel_state->color_index]
                            = hls_to_rgb(sixel_state->params[2],
                                         sixel_state->params[3],
                                         sixel_state->params[4]);
                    } else if (sixel_state->params[1] == 2) {
                        /* RGB */
                        sixel_state->params[2]
                            = (uint32)MIN(sixel_state->params[2], 100);
                        sixel_state->params[3]
                            = (uint32)MIN(sixel_state->params[3], 100);
                        sixel_state->params[4]
                            = (uint32)MIN(sixel_state->params[4], 100);
                        sixel_image->palette[sixel_state->color_index] = SIXEL_XRGB(
                            sixel_state->params[2], sixel_state->params[3],
                            sixel_state->params[4]);
                    }
                }
                break;
            }
            break;

        case PS_ERROR:
            if (*p == '\x1b') {
                sixel_state->state = PS_ESC;
                goto end;
            }
            p++;
            break;
        default:
            break;
        }
    }

end:
    return (int32)(p - p0);
}

static void
sixel_parser_deinit(SixelState *sixel_state) {
    if (sixel_state) {
        sixel_image_deinit(&sixel_state->image);
    }
    return;
}

static Pixmap
sixel_create_clipmask(char *pixels, int32 width, int32 height) {
    char c;
    char *clipdata;
    char *dst;
    int32 b, i, n, y, w;
    int32 msb = (XBitmapBitOrder(x_window.display) == MSBFirst);
    uint32 *src = (uint32 *)pixels;
    Pixmap clipmask;

    clipdata = dst = xmalloc((width + 7) / 8*height);

    for (y = 0; y < height; y++) {
        for (w = width; w > 0; w -= n) {
            n = (int32)MIN(w, 8);
            if (msb) {
                for (b = 0x80, c = 0, i = 0; i < n; i++, b >>= 1) {
                    c |= (*src++) ? b : 0;
                }
            } else {
                for (b = 0x01, c = 0, i = 0; i < n; i++, b <<= 1) {
                    c |= (*src++) ? b : 0;
                }
            }
            *dst++ = c;
        }
    }

    clipmask = XCreateBitmapFromData(x_window.display, x_window.win, clipdata,
                                     (uint32)width, (uint32)height);
    free(clipdata);
    return clipmask;
}

static uint32
hls_to_rgb(uint32 hue, uint32 lum, uint32 sat) {
    double lv = lum / 100.0;
    double sv = sat / 100.0;
    double c;
    double x;
    double m;
    double c2;
    double r1;
    double g1;
    double b1;
    uint32 r;
    uint32 g;
    uint32 b;
    uint32 hs;

    hue = (hue + 240) % 360;
    if (sat == 0) {
        r = g = b = lum*255 / 100;
        return SIXEL_RGB(r, g, b);
    }

    if ((c2 = ((2.0*lv) - 1.0)) < 0.0) {
        c2 = -c2;
    }
    if ((hs = (hue % 120) - 60) < 0) {
        hs = -hs;
    }
    c = (1.0 - c2)*sv;
    x = ((60 - hs) / 60.0)*c;
    m = lv - 0.5*c;

    switch (hue / 60) {
    case 0:
        r1 = c;
        g1 = x;
        b1 = 0.0;
        break;
    case 1:
        r1 = x;
        g1 = c;
        b1 = 0.0;
        break;
    case 2:
        r1 = 0.0;
        g1 = c;
        b1 = x;
        break;
    case 3:
        r1 = 0.0;
        g1 = x;
        b1 = c;
        break;
    case 4:
        r1 = x;
        g1 = 0.0;
        b1 = c;
        break;
    case 5:
        r1 = c;
        g1 = 0.0;
        b1 = x;
        break;
    default:
        return SIXEL_RGB(255, 255, 255);
    }

    r = (uint32)((r1 + m)*255.0 + 0.5);
    g = (uint32)((g1 + m)*255.0 + 0.5);
    b = (uint32)((b1 + m)*255.0 + 0.5);

    if (r < 0) {
        r = 0;
    } else if (r > 255) {
        r = 255;
    }
    if (g < 0) {
        g = 0;
    } else if (g > 255) {
        g = 255;
    }
    if (b < 0) {
        b = 0;
    } else if (b > 255) {
        b = 255;
    }
    return SIXEL_RGB(r, g, b);
}

#if TESTING_sixel

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "assert.c"

int
main(void) {
    {
        uint32 res;

        res = hls_to_rgb(0, 0, 0);
        ASSERT_EQUAL(res, SIXEL_RGB(0, 0, 0));

        res = hls_to_rgb(0, 100, 0);
        ASSERT_EQUAL(res, SIXEL_RGB(255, 255, 255));
    }

    {
        SixelImage img;
        int32 status;

        status = sixel_image_init(&img, 10, 10, 1, 0, 1);
        ASSERT_EQUAL(status, 0);
        ASSERT_EQUAL(img.width, 10);
        ASSERT_EQUAL(img.height, 10);
        ASSERT_EQUAL(img.use_private_register, 1);
        ASSERT_EQUAL(img.palette[0], 0);
        ASSERT_EQUAL(img.palette[1], 1);

        status = set_default_color(&img);
        ASSERT_EQUAL(status, 0);

        status = image_buffer_resize(&img, 20, 20);
        ASSERT_EQUAL(status, 0);
        ASSERT_EQUAL(img.width, 20);
        ASSERT_EQUAL(img.height, 20);

        sixel_image_deinit(&img);
        ASSERT_EQUAL((void *)img.data, NULL);
    }

    {
        SixelState state;
        int32 status;
        uchar buf[] = "\x1b";
        ImageList *newimages;
        int32 numimages;

        status = sixel_parser_init(&state, 0, 1, 0, 1, 10, 20);
        ASSERT_EQUAL(status, 0);
        ASSERT(state.state == PS_DECSIXEL);

        status = sixel_parser_set_default_color(&state);
        ASSERT_EQUAL(status, 0);

        sixel_parser_parse(&state, buf, 1);
        ASSERT(state.state == PS_ESC);

        newimages = NULL;
        numimages = sixel_parser_finalize(&state, &newimages, 0, 0, 10, 20);
        ASSERT_MORE(numimages, -2);

        if (newimages != NULL) {
            delete_image(newimages);
        }

        sixel_parser_deinit(&state);
    }

    {
        ImageList *dummy_img;

        dummy_img = xmalloc(sizeof(*dummy_img));
        dummy_img->next = NULL;
        dummy_img->prev = NULL;
        dummy_img->y = 100;
        dummy_img->pixmap = 0;
        dummy_img->clipmask = 0;
        dummy_img->pixels = NULL;

        term.images = dummy_img;
        term.mode = 0;
        term.lines_scrolled_up = 0;

        scroll_images(-10);

        if (term.images != NULL) {
            ASSERT_EQUAL(term.images->y, 90);
            delete_image(term.images);
        }
    }

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_sixel */

#endif /* SIXEL_C */
