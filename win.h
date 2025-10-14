/* See LICENSE for license details. */

enum win_mode {
	WIN_MODE_VISIBLE     = 1 << 0,
	WIN_MODE_FOCUSED     = 1 << 1,
	WIN_MODE_APPKEYPAD   = 1 << 2,
	WIN_MODE_MOUSEBTN    = 1 << 3,
	WIN_MODE_MOUSEMOTION = 1 << 4,
	WIN_MODE_REVERSE     = 1 << 5,
	WIN_MODE_KBDLOCK     = 1 << 6,
	WIN_MODE_HIDE        = 1 << 7,
	WIN_MODE_APPCURSOR   = 1 << 8,
	WIN_MODE_MOUSESGR    = 1 << 9,
	WIN_MODE_8BIT        = 1 << 10,
	WIN_MODE_BLINK       = 1 << 11,
	WIN_MODE_FBLINK      = 1 << 12,
	WIN_MODE_FOCUS       = 1 << 13,
	WIN_MODE_MOUSEX10    = 1 << 14,
	WIN_MODE_MOUSEMANY   = 1 << 15,
	WIN_MODE_BRCKTPASTE  = 1 << 16,
	WIN_MODE_NUMLOCK     = 1 << 17,
	WIN_MODE_MOUSE       = WIN_MODE_MOUSEBTN|WIN_MODE_MOUSEMOTION|WIN_MODE_MOUSEX10\
	                  |WIN_MODE_MOUSEMANY,
};

void x_bell(void);
void x_clipboard_copy(void);
void x_draw_cursor(int, int, Glyph, int, int, Glyph);
void x_draw_line(Line, int, int, int);
void x_finish_draw(void);
void x_load_cols(void);
int x_set_color_name(int, const char *);
int x_get_color(int, unsigned char *, unsigned char *, unsigned char *);
void x_set_icon_title(char *);
void x_set_title(char *);
int x_set_cursor(int);
void x_set_mode(int, unsigned int);
void x_set_pointer_motion(int);
void xsetsel(char *);
int xstartdraw(void);
void xximspot(int, int);
