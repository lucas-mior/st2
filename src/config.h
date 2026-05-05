#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <limits.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/X.h>

#include "st.h"

/*
 * appearance
 *
 * font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
 */
static char *CONF_FONT = "Liberation Mono:pixelsize=18:antialias=true:autohint=true";
/* Spare fonts */
static char *CONF_FONT2[] = {
	"Noto Color Emoji:pixelsize=18:antialias=true:autohint=true",
	"LiterationMono Nerd Font Mono:pixelsize=18:style=Regular",
};

static int32 CONF_BORDER_PIXELS = 2;

/*
 * What program is execed by st depends of these precedence rules:
 * 1: program passed with -e
 * 2: scroll and/or CONF_UTMP
 * 3: SHELL environment variable
 * 4: value of CONF_SHELl in /etc/passwd
 * 5: value of CONF_SHELl in config.def.h
 */
static char *CONF_SHELl = "/bin/sh";
static char *CONF_UTMP = NULL;
static char *CONF_STTY_ARGS = "stty raw pass8 nl -echo -iexten -cstopb 38400";

/* identification sequence returned in DA and DECID */
static char *CONF_VTIDEN = "\033[?62;4c"; /* VT200 family (62) with sixel (4) */
static int const sixelbyteorder = LSBFirst;

/* Kerning / character bounding-box multipliers */
static float CONF_CHAR_WIDTH_SCALE = 1.0;
static float CONF_CHAR_HEIGHT_SCALE = 1.0;

/*
 * word delimiter string
 *
 * More advanced example: L" `'\"()[]{}"
 */
static wchar_t *CONF_WORD_DELIMITERS = L" ";

/* selection timeouts (in milliseconds) */
static const uint32 CONF_DOUBLE_CLICK_TIMEOUT = 300;
static const uint32 CONF_TRIPLE_CLICK_TIMEOUT = 600;

/* alt screens */
static int32 CONF_ALLOW_ALT_SCREEN = 1;

/* allow certain non-interactive (insecure) window operations such as:
   setting the clipboard text */
static int32 CONF_ALLOW_WINDOW_OPS = 0;

/*
 * draw latency range in ms - from new content/keypress/etc until drawing.
 * within this range, st draws when content stops arriving (idle). mostly it's
 * near CONF_LATENCY_MIN, but it waits longer for slow updates to avoid partial draw.
 * low CONF_LATENCY_MIN will tear/flicker more, as it can "detect" idle too early.
 */
static const float CONF_LATENCY_MIN = 2;
static const float CONF_LATENCY_MAX = 33;

/*
 * blinking timeout (set to 0 to disable blinking) for the terminal blinking
 * attribute.
 */
static uint32 CONF_BLINK_TIMEOUT = 800;

/*
 * thickness of underline and bar cursors
 */
static uint32 CONF_CURSOR_THICKNESS = 2;

/*
 * 1: render most of the lines/blocks characters without using the font for
 *    perfect alignment between cells (U2500 - U259F except dashes/diagonals).
 *    Bold affects lines thickness if CONF_BOXDRAW_BOLD is not 0. Italic is ignored.
 * 0: disable (render all U25XX glyphs normally from the font).
 */
static const int32 CONF_BOXDRAW = 1;
static const int32 CONF_BOXDRAW_BOLD = 1;

/* braille (U28XX):  1: render as adjacent "pixels",  0: use font */
static const int32 CONF_BOXDRAW_BRAILLE = 0;

/*
 * bell volume. It must be a value between -100 and 100. Use 0 for disabling
 * it
 */
static int32 CONF_BELL_VOLUME = 0;

/* default TERM value */
static char *CONF_TERM_NAME = "st-256color";

/*
 * spaces per tab
 *
 * When you are changing this value, don't forget to adapt the »it« value in
 * the st.info and appropriately install the st.info in the environment where
 * you use this st version.
 *
 *	it#$CONF_TAB_NSPACES,
 *
 * Secondly make sure your kernel is not expanding tabs. When running `stty
 * -a` »tab0« should appear. You can tell the terminal to not expand tabs by
 *  running following command:
 *
 *	stty tabs
 */
static int32 CONF_TAB_NSPACES = 4;

/* bg opacity */
static double CONF_ALPHA = 0.85;

/* Terminal colors (16 first used in escape sequence) */
static char *CONF_COLORS[] = {
	/* 8 normal colors */
	[0] = "#000000",
	[1] = "#ff0000",
	[2] = "#00ff00",
	[3] = "#ffff00",
	[4] = "#0088ff",
	[5] = "#ff00ff",
	[6] = "#00aaaa",
	[7] = "#ffffff",

	/* 8 bright colors */
	[8]  = "#333333",
	[9]  = "#ff6600",
	[10] = "#00cc00",
	[11] = "#ffbb00",
	[12] = "#0066ff",
	[13] = "#c600c6",
	[14] = "#0066aa",
	[15] = "#f1f1f1",

	/* 26 transparent colors */
	[16] = "#000000",
	[17] = "#3a0000",
	[18] = "#003a00",
	[19] = "#3a3a00",
	[20] = "#00003a",
	[21] = "#3a003a",
	[22] = "#003a3a",
	[23] = "#3a3a3a",

 	[24] = "#000000",
	[25] = "#2a0000",
	[26] = "#002a00",
	[27] = "#2a2a00",
	[28] = "#00002a",
	[29] = "#2a002a",
	[30] = "#002a2a",
	[31] = "#2a2a2a",

	[32] = "#881000",
	[33] = "#882000",
	[34] = "#883000",
	[35] = "#884000",
	[36] = "#885000",
	[37] = "#886000",
	[38] = "#887000",
	[39] = "#887800",
	[40] = "#888000",
	[41] = "#888800",
 
	[255] = 0,

	/* more colors can be added after 255 to use with DefaultXX */
 	[256] = "#000000",
	[257] = "#ffff00",
	[258] = "#0000ff",
	[259] = "#555555",
};

static const int32 CONF_NTRANSPARENT_COLORS = 26;

/*
 * Default colors (CONF_COLORS index)
 * foreground, background, cursor, reverse cursor, selection
 */
static int32 CONF_COLOR_INDEX_FONT = 7;
static int32 CONF_COLOR_BG = 0;
static int32 CONF_COLOR_INDEX_CURSOR = 257;
static int32 CONF_COLOR_INDEX_REVCURSOR = 258;
static int32 CONF_COLOR_INDEX_SELECTION_BACK = 259;
static int32 CONF_COLOR_INDEX_SELECTION_FONT = 7;
/* If 0 use CONF_COLOR_INDEX_SELECTION_FONT as foreground in order to have a uniform foreground-color */
/* Else if 1 keep original foreground-color of each cell => more colors :) */
static int32 CONF_COLOR_IGNORE_SELECTION_FONT_COLOR = 1;

/*
 * Default shape of cursor
 * 2: Block ("█")
 * 4: Underline ("_")
 * 6: Bar ("|")
 * 7: Snowman ("☃")
 */
static uint32 CONF_CURSOR_SHAPE = 2;

/*
 * Default columns and CONF_NROWS numbers
 */

static int32 CONF_NCOLS = 80;
static int32 CONF_NROWS = 24;

/*
 * Default colour and shape of the mouse cursor
 */
static int32 CONF_MOUSE_SHAPE = XC_xterm;
static int32 CONF_MOUSE_COLOR_FG = 7;
static int32 CONF_MOUSE_COLOR_BG = 0;

/*
 * Color used to display font attributes when fontconfig selection_is_selected a font which
 * doesn't match the ones requested.
 */
static uint32 CONF_DEFAULT_ATTR = 11;

/*
 * Force mouse select/CONF_KEYBOARD_SHORTCUTS while mask is active (when WIN_MODE_MOUSE is set).
 * Note that if you want to use ShiftMask with CONF_SELECTION_MASKS, set this to an other
 * modifier, set to 0 to not use it.
 */
static uint32 CONF_FORCE_MOUSE_MOD = ShiftMask;

/*
 * Internal mouse CONF_KEYBOARD_SHORTCUTS.
 * Beware that overloading Button1 will disable the selection.
 */
static MouseShortcut CONF_MOUSE_SHORTCUTS[] = {
	/* mask       button   function              argument            release */
	{ XK_ANY_MOD, Button2, user_selection_paste, {.i = 0},           1 },
	{ ShiftMask,  Button4, user_tty_send,             {.s = "\033[5;2~"}, 0 },
	{ XK_ANY_MOD, Button4, user_tty_send,             {.s = "\031"},      0 },
	{ ShiftMask,  Button5, user_tty_send,             {.s = "\033[6;2~"}, 0 },
	{ XK_ANY_MOD, Button5, user_tty_send,             {.s = "\005"},      0 },
};

/* Internal keyboard CONF_KEYBOARD_SHORTCUTS. */
#define MODKEY Mod1Mask
#define TERMMOD (ControlMask|ShiftMask)

static Shortcut CONF_KEYBOARD_SHORTCUTS[] = {
	/* mask                  keysym         function        argument */
	{ XK_ANY_MOD,            XK_Break,      user_send_break,      {.i =  0} },
	{ ControlMask,           XK_Print,      user_toggle_printer,  {.i =  0} },
	{ ShiftMask,             XK_Print,      user_print_screen,    {.i =  0} },
	{ XK_ANY_MOD,            XK_Print,      user_print_sel,       {.i =  0} },
	{ ControlMask|ShiftMask, XK_plus,       user_zoom,            {.f = +1} },
	{ ControlMask|ShiftMask, XK_underscore, user_zoom,            {.f = -1} },
	{ ControlMask|ShiftMask, XK_parenright, user_zoom_reset,      {.f =  0} },
	{ ControlMask|ShiftMask, XK_C,          user_clipboard_copy,  {.i =  0} },
	{ ControlMask|ShiftMask, XK_V,          user_clipboard_paste, {.i =  0} },
	{ ShiftMask,             XK_Insert,     user_selection_paste, {.i =  0} },
	{ Mod1Mask,              XK_Num_Lock,   user_toggle_numlock,  {.i =  0} },
	{ ControlMask|ShiftMask, XK_F,          user_scroll_up,       {.i = -1} },
	{ ControlMask|ShiftMask, XK_B,          user_scroll_down,     {.i = -1} },
	{ ControlMask|ShiftMask, XK_L,          user_scroll_up,       {.i = +1} },
	{ ControlMask|ShiftMask, XK_K,          user_scroll_down,     {.i = +1} },
	{ ControlMask|ShiftMask, XK_N,          user_change_alpha,    {.f = -0.02f} },
	{ ControlMask|ShiftMask, XK_M,          user_change_alpha,    {.f = +0.02f} },
	{ Mod1Mask,              XK_Escape,     user_vim_select,      {.i = 0} },
	{ Mod1Mask,              XK_c,          user_copy_output,     {.i = 0} },
	{ Mod1Mask,              XK_u,          user_url_select,      {.i = 0} },
};

/*
 * Special keys (change & recompile st.info accordingly)
 *
 * Mask value:
 * * Use XK_ANY_MOD to match the CONF_KEYS no matter modifiers state
 * * Use XK_NO_MOD to match the CONF_KEYS alone (no modifiers)
 * appkey value:
 * * 0: no value
 * * > 0: keypad application mode enabled
 * *   = 2: term.user_toggle_numlock = 1
 * * < 0: keypad application mode disabled
 * appcursor value:
 * * 0: no value
 * * > 0: cursor application mode enabled
 * * < 0: cursor application mode disabled
 *
 * Be careful with the order of the definitions because st searches in
 * this table sequentially, so any XK_ANY_MOD must be in the last
 * position for a CONF_KEYS.
 */

/*
 * If you want keys other than the X11 function keys (0xFD00 - 0xFFFF)
 * to be mapped below, add them to this array.
 */
static KeySym CONF_MAPPED_KEYS[] = { (KeySym)-1 };

/*
 * State bits to ignore when matching CONF_KEYS or button events.  By default,
 * user_toggle_numlock (Mod2Mask) and keyboard layout (XK_SWITCH_MOD) are ignored.
 */
static uint32 CONF_IGNORE_MOD = Mod2Mask|XK_SWITCH_MOD;

/*
 * This is the huge CONF_KEYS array which defines all compatibility to the Linux
 * world. Please decide about changes wisely.
 */
static Key CONF_KEYS[] = {
	/* keysym           mask            string      appkey appcursor */
	{ XK_KP_Home,       ShiftMask,      "\033[2J",       0,   -1},
	{ XK_KP_Home,       ShiftMask,      "\033[1;2H",     0,   +1},
	{ XK_KP_Home,       XK_ANY_MOD,     "\033[H",        0,   -1},
	{ XK_KP_Home,       XK_ANY_MOD,     "\033[1~",       0,   +1},
	{ XK_KP_Up,         XK_ANY_MOD,     "\033Ox",       +1,    0},
	{ XK_KP_Up,         XK_ANY_MOD,     "\033[A",        0,   -1},
	{ XK_KP_Up,         XK_ANY_MOD,     "\033OA",        0,   +1},
	{ XK_KP_Down,       XK_ANY_MOD,     "\033Or",       +1,    0},
	{ XK_KP_Down,       XK_ANY_MOD,     "\033[B",        0,   -1},
	{ XK_KP_Down,       XK_ANY_MOD,     "\033OB",        0,   +1},
	{ XK_KP_Left,       XK_ANY_MOD,     "\033Ot",       +1,    0},
	{ XK_KP_Left,       XK_ANY_MOD,     "\033[D",        0,   -1},
	{ XK_KP_Left,       XK_ANY_MOD,     "\033OD",        0,   +1},
	{ XK_KP_Right,      XK_ANY_MOD,     "\033Ov",       +1,    0},
	{ XK_KP_Right,      XK_ANY_MOD,     "\033[C",        0,   -1},
	{ XK_KP_Right,      XK_ANY_MOD,     "\033OC",        0,   +1},
	{ XK_KP_Prior,      ShiftMask,      "\033[5;2~",     0,    0},
	{ XK_KP_Prior,      XK_ANY_MOD,     "\033[5~",       0,    0},
	{ XK_KP_Begin,      XK_ANY_MOD,     "\033[E",        0,    0},
	{ XK_KP_End,        ControlMask,    "\033[J",       -1,    0},
	{ XK_KP_End,        ControlMask,    "\033[1;5F",    +1,    0},
	{ XK_KP_End,        ShiftMask,      "\033[K",       -1,    0},
	{ XK_KP_End,        ShiftMask,      "\033[1;2F",    +1,    0},
	{ XK_KP_End,        XK_ANY_MOD,     "\033[4~",       0,    0},
	{ XK_KP_Next,       ShiftMask,      "\033[6;2~",     0,    0},
	{ XK_KP_Next,       XK_ANY_MOD,     "\033[6~",       0,    0},
	{ XK_KP_Insert,     ShiftMask,      "\033[2;2~",    +1,    0},
	{ XK_KP_Insert,     ShiftMask,      "\033[4l",      -1,    0},
	{ XK_KP_Insert,     ControlMask,    "\033[L",       -1,    0},
	{ XK_KP_Insert,     ControlMask,    "\033[2;5~",    +1,    0},
	{ XK_KP_Insert,     XK_ANY_MOD,     "\033[4h",      -1,    0},
	{ XK_KP_Insert,     XK_ANY_MOD,     "\033[2~",      +1,    0},
	{ XK_KP_Delete,     ControlMask,    "\033[M",       -1,    0},
	{ XK_KP_Delete,     ControlMask,    "\033[3;5~",    +1,    0},
	{ XK_KP_Delete,     ShiftMask,      "\033[2K",      -1,    0},
	{ XK_KP_Delete,     ShiftMask,      "\033[3;2~",    +1,    0},
	{ XK_KP_Delete,     XK_ANY_MOD,     "\033[3~",       -1,    0},
	{ XK_KP_Delete,     XK_ANY_MOD,     "\033[3~",      +1,    0},
	{ XK_KP_Multiply,   XK_ANY_MOD,     "\033Oj",       +2,    0},
	{ XK_KP_Add,        XK_ANY_MOD,     "\033Ok",       +2,    0},
	{ XK_KP_Enter,      XK_ANY_MOD,     "\033OM",       +2,    0},
	{ XK_KP_Enter,      XK_ANY_MOD,     "\r",           -1,    0},
	{ XK_KP_Subtract,   XK_ANY_MOD,     "\033Om",       +2,    0},
	{ XK_KP_Decimal,    XK_ANY_MOD,     "\033On",       +2,    0},
	{ XK_KP_Divide,     XK_ANY_MOD,     "\033Oo",       +2,    0},
	{ XK_KP_0,          XK_ANY_MOD,     "\033Op",       +2,    0},
	{ XK_KP_1,          XK_ANY_MOD,     "\033Oq",       +2,    0},
	{ XK_KP_2,          XK_ANY_MOD,     "\033Or",       +2,    0},
	{ XK_KP_3,          XK_ANY_MOD,     "\033Os",       +2,    0},
	{ XK_KP_4,          XK_ANY_MOD,     "\033Ot",       +2,    0},
	{ XK_KP_5,          XK_ANY_MOD,     "\033Ou",       +2,    0},
	{ XK_KP_6,          XK_ANY_MOD,     "\033Ov",       +2,    0},
	{ XK_KP_7,          XK_ANY_MOD,     "\033Ow",       +2,    0},
	{ XK_KP_8,          XK_ANY_MOD,     "\033Ox",       +2,    0},
	{ XK_KP_9,          XK_ANY_MOD,     "\033Oy",       +2,    0},
	{ XK_Up,            ShiftMask,      "\033[1;2A",     0,    0},
	{ XK_Up,            Mod1Mask,       "\033[1;3A",     0,    0},
	{ XK_Up,         ShiftMask|Mod1Mask,"\033[1;4A",     0,    0},
	{ XK_Up,            ControlMask,    "\033[1;5A",     0,    0},
	{ XK_Up,      ShiftMask|ControlMask,"\033[1;6A",     0,    0},
	{ XK_Up,       ControlMask|Mod1Mask,"\033[1;7A",     0,    0},
	{ XK_Up,ShiftMask|ControlMask|Mod1Mask,"\033[1;8A",  0,    0},
	{ XK_Up,            XK_ANY_MOD,     "\033[A",        0,   -1},
	{ XK_Up,            XK_ANY_MOD,     "\033OA",        0,   +1},
	{ XK_Down,          ShiftMask,      "\033[1;2B",     0,    0},
	{ XK_Down,          Mod1Mask,       "\033[1;3B",     0,    0},
	{ XK_Down,       ShiftMask|Mod1Mask,"\033[1;4B",     0,    0},
	{ XK_Down,          ControlMask,    "\033[1;5B",     0,    0},
	{ XK_Down,    ShiftMask|ControlMask,"\033[1;6B",     0,    0},
	{ XK_Down,     ControlMask|Mod1Mask,"\033[1;7B",     0,    0},
	{ XK_Down,ShiftMask|ControlMask|Mod1Mask,"\033[1;8B",0,    0},
	{ XK_Down,          XK_ANY_MOD,     "\033[B",        0,   -1},
	{ XK_Down,          XK_ANY_MOD,     "\033OB",        0,   +1},
	{ XK_Left,          ShiftMask,      "\033[1;2D",     0,    0},
	{ XK_Left,          Mod1Mask,       "\033[1;3D",     0,    0},
	{ XK_Left,       ShiftMask|Mod1Mask,"\033[1;4D",     0,    0},
	{ XK_Left,          ControlMask,    "\033[1;5D",     0,    0},
	{ XK_Left,    ShiftMask|ControlMask,"\033[1;6D",     0,    0},
	{ XK_Left,     ControlMask|Mod1Mask,"\033[1;7D",     0,    0},
	{ XK_Left,ShiftMask|ControlMask|Mod1Mask,"\033[1;8D",0,    0},
	{ XK_Left,          XK_ANY_MOD,     "\033[D",        0,   -1},
	{ XK_Left,          XK_ANY_MOD,     "\033OD",        0,   +1},
	{ XK_Right,         ShiftMask,      "\033[1;2C",     0,    0},
	{ XK_Right,         Mod1Mask,       "\033[1;3C",     0,    0},
	{ XK_Right,      ShiftMask|Mod1Mask,"\033[1;4C",     0,    0},
	{ XK_Right,         ControlMask,    "\033[1;5C",     0,    0},
	{ XK_Right,   ShiftMask|ControlMask,"\033[1;6C",     0,    0},
	{ XK_Right,    ControlMask|Mod1Mask,"\033[1;7C",     0,    0},
	{ XK_Right,ShiftMask|ControlMask|Mod1Mask,"\033[1;8C",0,   0},
	{ XK_Right,         XK_ANY_MOD,     "\033[C",        0,   -1},
	{ XK_Right,         XK_ANY_MOD,     "\033OC",        0,   +1},
	{ XK_ISO_Left_Tab,  ShiftMask,      "\033[Z",        0,    0},
	{ XK_Return,        Mod1Mask,       "\033\r",        0,    0},
	{ XK_Return,        XK_ANY_MOD,     "\r",            0,    0},
	{ XK_Insert,        ShiftMask,      "\033[4l",      -1,    0},
	{ XK_Insert,        ShiftMask,      "\033[2;2~",    +1,    0},
	{ XK_Insert,        ControlMask,    "\033[L",       -1,    0},
	{ XK_Insert,        ControlMask,    "\033[2;5~",    +1,    0},
	{ XK_Insert,        XK_ANY_MOD,     "\033[4h",      -1,    0},
	{ XK_Insert,        XK_ANY_MOD,     "\033[2~",      +1,    0},
	{ XK_Delete,        ControlMask,    "\033[M",       -1,    0},
	{ XK_Delete,        ControlMask,    "\033[3;5~",    +1,    0},
	{ XK_Delete,        ShiftMask,      "\033[2K",      -1,    0},
	{ XK_Delete,        ShiftMask,      "\033[3;2~",    +1,    0},
	{ XK_Delete,        XK_ANY_MOD,     "\033[3~",       -1,    0},
	{ XK_Delete,        XK_ANY_MOD,     "\033[3~",      +1,    0},
	{ XK_BackSpace,     XK_NO_MOD,      "\177",          0,    0},
	{ XK_BackSpace,     Mod1Mask,       "\033\177",      0,    0},
	{ XK_Home,          ShiftMask,      "\033[2J",       0,   -1},
	{ XK_Home,          ShiftMask,      "\033[1;2H",     0,   +1},
	{ XK_Home,          XK_ANY_MOD,     "\033[H",        0,   -1},
	{ XK_Home,          XK_ANY_MOD,     "\033[1~",       0,   +1},
	{ XK_End,           ControlMask,    "\033[J",       -1,    0},
	{ XK_End,           ControlMask,    "\033[1;5F",    +1,    0},
	{ XK_End,           ShiftMask,      "\033[K",       -1,    0},
	{ XK_End,           ShiftMask,      "\033[1;2F",    +1,    0},
	{ XK_End,           XK_ANY_MOD,     "\033[4~",       0,    0},
	{ XK_Prior,         ControlMask,    "\033[5;5~",     0,    0},
	{ XK_Prior,         ShiftMask,      "\033[5;2~",     0,    0},
	{ XK_Prior,         XK_ANY_MOD,     "\033[5~",       0,    0},
	{ XK_Next,          ControlMask,    "\033[6;5~",     0,    0},
	{ XK_Next,          ShiftMask,      "\033[6;2~",     0,    0},
	{ XK_Next,          XK_ANY_MOD,     "\033[6~",       0,    0},
	{ XK_F1,            XK_NO_MOD,      "\033OP" ,       0,    0},
	{ XK_F1, /* F13 */  ShiftMask,      "\033[1;2P",     0,    0},
	{ XK_F1, /* F25 */  ControlMask,    "\033[1;5P",     0,    0},
	{ XK_F1, /* F37 */  Mod4Mask,       "\033[1;6P",     0,    0},
	{ XK_F1, /* F49 */  Mod1Mask,       "\033[1;3P",     0,    0},
	{ XK_F1, /* F61 */  Mod3Mask,       "\033[1;4P",     0,    0},
	{ XK_F2,            XK_NO_MOD,      "\033OQ" ,       0,    0},
	{ XK_F2, /* F14 */  ShiftMask,      "\033[1;2Q",     0,    0},
	{ XK_F2, /* F26 */  ControlMask,    "\033[1;5Q",     0,    0},
	{ XK_F2, /* F38 */  Mod4Mask,       "\033[1;6Q",     0,    0},
	{ XK_F2, /* F50 */  Mod1Mask,       "\033[1;3Q",     0,    0},
	{ XK_F2, /* F62 */  Mod3Mask,       "\033[1;4Q",     0,    0},
	{ XK_F3,            XK_NO_MOD,      "\033OR" ,       0,    0},
	{ XK_F3, /* F15 */  ShiftMask,      "\033[1;2R",     0,    0},
	{ XK_F3, /* F27 */  ControlMask,    "\033[1;5R",     0,    0},
	{ XK_F3, /* F39 */  Mod4Mask,       "\033[1;6R",     0,    0},
	{ XK_F3, /* F51 */  Mod1Mask,       "\033[1;3R",     0,    0},
	{ XK_F3, /* F63 */  Mod3Mask,       "\033[1;4R",     0,    0},
	{ XK_F4,            XK_NO_MOD,      "\033OS" ,       0,    0},
	{ XK_F4, /* F16 */  ShiftMask,      "\033[1;2S",     0,    0},
	{ XK_F4, /* F28 */  ControlMask,    "\033[1;5S",     0,    0},
	{ XK_F4, /* F40 */  Mod4Mask,       "\033[1;6S",     0,    0},
	{ XK_F4, /* F52 */  Mod1Mask,       "\033[1;3S",     0,    0},
	{ XK_F5,            XK_NO_MOD,      "\033[15~",      0,    0},
	{ XK_F5, /* F17 */  ShiftMask,      "\033[15;2~",    0,    0},
	{ XK_F5, /* F29 */  ControlMask,    "\033[15;5~",    0,    0},
	{ XK_F5, /* F41 */  Mod4Mask,       "\033[15;6~",    0,    0},
	{ XK_F5, /* F53 */  Mod1Mask,       "\033[15;3~",    0,    0},
	{ XK_F6,            XK_NO_MOD,      "\033[17~",      0,    0},
	{ XK_F6, /* F18 */  ShiftMask,      "\033[17;2~",    0,    0},
	{ XK_F6, /* F30 */  ControlMask,    "\033[17;5~",    0,    0},
	{ XK_F6, /* F42 */  Mod4Mask,       "\033[17;6~",    0,    0},
	{ XK_F6, /* F54 */  Mod1Mask,       "\033[17;3~",    0,    0},
	{ XK_F7,            XK_NO_MOD,      "\033[18~",      0,    0},
	{ XK_F7, /* F19 */  ShiftMask,      "\033[18;2~",    0,    0},
	{ XK_F7, /* F31 */  ControlMask,    "\033[18;5~",    0,    0},
	{ XK_F7, /* F43 */  Mod4Mask,       "\033[18;6~",    0,    0},
	{ XK_F7, /* F55 */  Mod1Mask,       "\033[18;3~",    0,    0},
	{ XK_F8,            XK_NO_MOD,      "\033[19~",      0,    0},
	{ XK_F8, /* F20 */  ShiftMask,      "\033[19;2~",    0,    0},
	{ XK_F8, /* F32 */  ControlMask,    "\033[19;5~",    0,    0},
	{ XK_F8, /* F44 */  Mod4Mask,       "\033[19;6~",    0,    0},
	{ XK_F8, /* F56 */  Mod1Mask,       "\033[19;3~",    0,    0},
	{ XK_F9,            XK_NO_MOD,      "\033[20~",      0,    0},
	{ XK_F9, /* F21 */  ShiftMask,      "\033[20;2~",    0,    0},
	{ XK_F9, /* F33 */  ControlMask,    "\033[20;5~",    0,    0},
	{ XK_F9, /* F45 */  Mod4Mask,       "\033[20;6~",    0,    0},
	{ XK_F9, /* F57 */  Mod1Mask,       "\033[20;3~",    0,    0},
	{ XK_F10,           XK_NO_MOD,      "\033[21~",      0,    0},
	{ XK_F10, /* F22 */ ShiftMask,      "\033[21;2~",    0,    0},
	{ XK_F10, /* F34 */ ControlMask,    "\033[21;5~",    0,    0},
	{ XK_F10, /* F46 */ Mod4Mask,       "\033[21;6~",    0,    0},
	{ XK_F10, /* F58 */ Mod1Mask,       "\033[21;3~",    0,    0},
	{ XK_F11,           XK_NO_MOD,      "\033[23~",      0,    0},
	{ XK_F11, /* F23 */ ShiftMask,      "\033[23;2~",    0,    0},
	{ XK_F11, /* F35 */ ControlMask,    "\033[23;5~",    0,    0},
	{ XK_F11, /* F47 */ Mod4Mask,       "\033[23;6~",    0,    0},
	{ XK_F11, /* F59 */ Mod1Mask,       "\033[23;3~",    0,    0},
	{ XK_F12,           XK_NO_MOD,      "\033[24~",      0,    0},
	{ XK_F12, /* F24 */ ShiftMask,      "\033[24;2~",    0,    0},
	{ XK_F12, /* F36 */ ControlMask,    "\033[24;5~",    0,    0},
	{ XK_F12, /* F48 */ Mod4Mask,       "\033[24;6~",    0,    0},
	{ XK_F12, /* F60 */ Mod1Mask,       "\033[24;3~",    0,    0},
	{ XK_F13,           XK_NO_MOD,      "\033[1;2P",     0,    0},
	{ XK_F14,           XK_NO_MOD,      "\033[1;2Q",     0,    0},
	{ XK_F15,           XK_NO_MOD,      "\033[1;2R",     0,    0},
	{ XK_F16,           XK_NO_MOD,      "\033[1;2S",     0,    0},
	{ XK_F17,           XK_NO_MOD,      "\033[15;2~",    0,    0},
	{ XK_F18,           XK_NO_MOD,      "\033[17;2~",    0,    0},
	{ XK_F19,           XK_NO_MOD,      "\033[18;2~",    0,    0},
	{ XK_F20,           XK_NO_MOD,      "\033[19;2~",    0,    0},
	{ XK_F21,           XK_NO_MOD,      "\033[20;2~",    0,    0},
	{ XK_F22,           XK_NO_MOD,      "\033[21;2~",    0,    0},
	{ XK_F23,           XK_NO_MOD,      "\033[23;2~",    0,    0},
	{ XK_F24,           XK_NO_MOD,      "\033[24;2~",    0,    0},
	{ XK_F25,           XK_NO_MOD,      "\033[1;5P",     0,    0},
	{ XK_F26,           XK_NO_MOD,      "\033[1;5Q",     0,    0},
	{ XK_F27,           XK_NO_MOD,      "\033[1;5R",     0,    0},
	{ XK_F28,           XK_NO_MOD,      "\033[1;5S",     0,    0},
	{ XK_F29,           XK_NO_MOD,      "\033[15;5~",    0,    0},
	{ XK_F30,           XK_NO_MOD,      "\033[17;5~",    0,    0},
	{ XK_F31,           XK_NO_MOD,      "\033[18;5~",    0,    0},
	{ XK_F32,           XK_NO_MOD,      "\033[19;5~",    0,    0},
	{ XK_F33,           XK_NO_MOD,      "\033[20;5~",    0,    0},
	{ XK_F34,           XK_NO_MOD,      "\033[21;5~",    0,    0},
	{ XK_F35,           XK_NO_MOD,      "\033[23;5~",    0,    0},
};

/*
 * Selection types' masks.
 * Use the same masks as usual.
 * Button1Mask is always unset, to make masks match between ButtonPress.
 * ButtonRelease and MotionNotify.
 * If no match is found, regular selection is used.
 */
static uint32 CONF_SELECTION_MASKS[] = {
	[SELECTION_RECTANGULAR] = Mod1Mask,
};

/*
 * Printable characters in ASCII, used to estimate the advance width
 * of single wide characters.
 */
static char CONF_ASCII_PRINTABLE[] =
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~";

#endif /* CONFIG_H */
