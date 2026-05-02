#ifndef SIXEL_H
#define SIXEL_H

#include <X11/X.h>
#include "cbase/util.c"

#define DECSIXEL_PARAMS_MAX 16
#define DECSIXEL_PALETTE_MAX 1024
#define DECSIXEL_PARAMVALUE_MAX 65535
#define DECSIXEL_WIDTH_MAX 4096
#define DECSIXEL_HEIGHT_MAX 4096

typedef struct sixel_image_buffer {
	ushort *data;
	int32 width;
	int32 height;
	uint32 palette[DECSIXEL_PALETTE_MAX + 1];
	int32 ncolors;
	int32 palette_modified;
	int32 use_private_register;
} SixelImage;

typedef enum parse_state {
	PS_ESC        = 1,  /* ESC */
	PS_DECSIXEL   = 2,  /* DECSIXEL body part ", $, -, ? ... ~ */
	PS_DECGRA     = 3,  /* DECGRA Set Raster Attributes " Pan; Pad; Ph; Pv */
	PS_DECGRI     = 4,  /* DECGRI Graphics Repeat Introducer ! Pn Ch */
	PS_DECGCI     = 5,  /* DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz */
	PS_ERROR      = 6,
} parse_state_t;

typedef struct parser_context {
	parse_state_t state;
	int32 pos_x;
	int32 pos_y;
	int32 max_x;
	int32 max_y;
	int32 attributed_pan;
	int32 attributed_pad;
	int32 attributed_ph;
	int32 attributed_pv;
	int32 transparent;
	int32 repeat_count;
	int32 color_index;
	int32 bgindex;
	int32 grid_width;
	int32 grid_height;
	int32 param;
	int32 nparams;
	uint32 params[DECSIXEL_PARAMS_MAX];
	SixelImage image;
} SixelState;

struct ImageList;
void scroll_images(int32 n);
void delete_image(struct ImageList *im);
int32 sixel_parser_init(SixelState *st, int32 transparent, uint32 fgcolor, uint32 bgcolor, unsigned char use_private_register, int32 cell_width, int32 cell_height);
int32 sixel_parser_parse(SixelState *st, uchar *p, int32 len);
int32 sixel_parser_set_default_color(SixelState *st);
int32 sixel_parser_finalize(SixelState *st, struct ImageList **newimages, int32 cx, int32 cy, int32 cw, int32 ch);
void sixel_parser_deinit(SixelState *st);
Pixmap sixel_create_clipmask(char *pixels, int32 width, int32 height);

#endif /* SIXEL_H */
