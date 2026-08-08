#ifndef SIXEL_H
#define SIXEL_H

#include <X11/X.h>
#include "util.c"

#define DECSIXEL_PARAMS_MAX 16
#define DECSIXEL_PALETTE_MAX 1024
#define DECSIXEL_PARAMVALUE_MAX 65535
#define DECSIXEL_WIDTH_MAX 4096
#define DECSIXEL_HEIGHT_MAX 4096

typedef struct SixelImage {
    ushort *data;
    int32 width;
    int32 height;
    uint32 palette[DECSIXEL_PALETTE_MAX + 1];
    int32 ncolors;
    bool palette_modified;
    bool use_private_register;
} SixelImage;

enum ParseState {
    PARSE_STATE_ESC      = 1,  /* ESC */
    PARSE_STATE_DECSIXEL = 2,  /* DECSIXEL body part ", $, -, ? ... ~ */
    PARSE_STATE_DECGRA   = 3,  /* DECGRA Set Raster Attributes " Pan; Pad; Ph; Pv */
    PARSE_STATE_DECGRI   = 4,  /* DECGRI Graphics Repeat Introducer ! Pn Ch */
    PARSE_STATE_DECGCI   = 5,  /* DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz */
    PARSE_STATE_ERROR    = 6,
};

typedef struct SixelState {
    enum ParseState state;
    int32 pos_x;
    int32 pos_y;
    int32 max_x;
    int32 max_y;
    uint32 attributed_pan;
    uint32 attributed_pad;
    int32 attributed_ph;
    int32 attributed_pv;
    bool transparent;
    int32 repeat_count;
    int32 color_index;
    int32 bgindex;
    int32 grid_width;
    int32 grid_height;
    int32 nparams;
    uint32 param;
    uint32 params[DECSIXEL_PARAMS_MAX];
    SixelImage image;
} SixelState;

struct ImageList;
static void sixel_image_delete(struct ImageList *im);
static int32 sixel_parser_init(SixelState *st,
                               bool transparent,
                               uint32 fgcolor, uint32 bgcolor,
                               bool use_private_register,
                               int32 cell_width, int32 cell_height);
static int32 sixel_parser_parse(SixelState *st, uchar *p, int32 len);
static int32 sixel_parser_finalize(SixelState *st, struct ImageList **newimages,
                                   int32 cx, int32 cy, int32 cw, int32 ch);
static void sixel_parser_deinit(SixelState *st);
static Pixmap sixel_create_clipmask(char *pixels, int32 width, int32 height);

#endif /* SIXEL_H */
