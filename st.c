/* See LICENSE for license details. */

#ifndef ST_C
#define ST_C

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#include "st.h"
#include "win.h"
#include "boxdraw.c"

#if   defined(__linux)
 #include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
 #include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
 #include <libutil.h>
#endif

/* Arbitrary sizes */
#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4
#define ESC_BUF_SIZ   (128*UTF_SIZ)
#define ESC_ARG_SIZ   16
#define STR_BUF_SIZ   ESC_BUF_SIZ
#define STR_ARG_SIZ   ESC_ARG_SIZ
#define HISTSIZE      2000
#define RESIZEBUFFER  1000

/* macros */
#define TERM_MODE_IS_SET(flag)		((term.mode & (flag)) != 0)
#define ISCONTROLC0(c)		(BETWEEN(c, 0, 0x1f) || (c) == 0x7f)
#define ISCONTROLC1(c)		(BETWEEN(c, 0x80, 0x9f))
#define ISCONTROL(c)		(ISCONTROLC0(c) || ISCONTROLC1(c))
#define ISDELIM(u)		(u && wcschr(worddelimiters, u))
#define TLINE(y) ( \
	(y) < term.scr ? term.hist[(term.histi + (y) - term.scr + 1 + HISTSIZE) % HISTSIZE] \
	               : term.line[(y) - term.scr] \
)

#define TLINEABS(y) ( \
	(y) < 0 ? term.hist[(term.histi + (y) + 1 + HISTSIZE) % HISTSIZE] : term.line[(y)] \
)
#define TLINE_HIST(y)           ((y) <= HISTSIZE-term.row+2 ? term.hist[(y)] : term.line[(y-HISTSIZE+term.row-3)])

#define UPDATEWRAPNEXT(alt, col) do { \
	if ((term.cursor.state & CURSOR_WRAPNEXT) && term.cursor.x + term.wrapcwidth[alt] < col) { \
		term.cursor.x += term.wrapcwidth[alt]; \
		term.cursor.state &= ~CURSOR_WRAPNEXT; \
	} \
} while (0);

enum term_mode {
	TERM_MODE_WRAP        = 1 << 0,
	TERM_MODE_INSERT      = 1 << 1,
	TERM_MODE_ALTSCREEN   = 1 << 2,
	TERM_MODE_CRLF        = 1 << 3,
	TERM_MODE_ECHO        = 1 << 4,
	TERM_MODE_PRINT       = 1 << 5,
	TERM_MODE_UTF8        = 1 << 6,
};

enum scroll_mode {
	SCROLL_RESIZE = -1,
	SCROLL_NOSAVEHIST = 0,
	SCROLL_SAVEHIST = 1
};

enum cursor_movement {
	CURSOR_SAVE,
	CURSOR_LOAD
};

enum cursor_state {
	CURSOR_DEFAULT  = 0,
	CURSOR_WRAPNEXT = 1,
	CURSOR_ORIGIN   = 2
};

enum charset {
	CS_GRAPHIC0,
	CS_GRAPHIC1,
	CS_UK,
	CS_USA,
	CS_MULTI,
	CS_GER,
	CS_FIN
};

enum escape_state {
	ESC_START      = 1,
	ESC_CSI        = 2,
	ESC_STR        = 4,  /* DCS, OSC, PM, APC */
	ESC_ALTCHARSET = 8,
	ESC_STR_END    = 16, /* a final string was encountered */
	ESC_TEST       = 32, /* Enter in test mode */
	ESC_UTF8       = 64,
};

typedef struct {
	Glyph attr; /* current char attributes */
	int x;
	int y;
	char state;
} TCursor;

typedef struct {
	int mode;
	int type;
	int snap;
	/*
	 * Selection variables:
	 * nb – normalized coordinates of the beginning of the selection
	 * ne – normalized coordinates of the end of the selection
	 * ob – original coordinates of the beginning of the selection
	 * oe – original coordinates of the end of the selection
	 */
	struct {
		int x, y;
	} nb, ne, ob, oe;

	int alt;
} Selection;

/* Internal representation of the screen */
typedef struct {
	int row;      /* nb row */
	int col;      /* nb col */
	Line *line;   /* screen */
	Line hist[HISTSIZE]; /* history buffer */
	int histi;           /* history index */
	int histf;           /* nb history available */
	int scr;             /* scroll back */
	int wrapcwidth[2];   /* used in updating WRAPNEXT when resizing */
	int *dirty;   /* dirtyness of lines */
	TCursor cursor;    /* cursor */
	int ocx;      /* old cursor col */
	int ocy;      /* old cursor row */
	int top;      /* top    scroll limit */
	int bot;      /* bottom scroll limit */
	int mode;     /* terminal mode flags */
	int esc;      /* escape state flags */
	char trantbl[4]; /* charset table translation */
	int charset;  /* current charset */
	int icharset; /* selected charset for sequence */
	int *tabs;
	Rune lastc;   /* last printed char outside of sequence, 0 if control */
} Term;

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
typedef struct {
	char buf[ESC_BUF_SIZ]; /* raw string */
	size_t len;            /* raw string length */
	char priv;
	int arg[ESC_ARG_SIZ];
	int narg;              /* nb of args */
	char mode[2];
} CSIEscape;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
typedef struct {
	char type;             /* ESC type ... */
	char *buf;             /* allocated raw string */
	size_t siz;            /* allocation size */
	size_t len;            /* raw string length */
	char *args[STR_ARG_SIZ];
	int narg;              /* nb of args */
} STREscape;

static void exec_shell(char *, char **);
static void stty(char **);
static void handler_sigchld(int);
static void tty_write_raw(const char *, size_t);

static void control_seq_intro_dump(void);
static void control_seq_intro_handle(void);
static void control_seq_intro_parse(void);
static void control_seq_intro_reset(void);
static void osc_color_response(int, int, int);
static int eschandle(uchar);
static void string_dump(void);
static void string_handle(void);
static void string_parse(void);
static void string_reset(void);

static void term_printer(char *, size_t);
static void term_dump_sel(void);
static void term_dump_line(int);
static void term_dump(void);
static void term_clear_region(int, int, int, int, int);
static void term_cursor(int);
static void tclearglyph(Glyph *, int);
static void tresetcursor(void);
static void term_delete_char(int);
static void term_delete_line(int);
static void term_insert_blank(int);
static void term_insert_blank_line(int);
static int term_line_len(Line len);
static int tiswrapped(Line line);
static char *tgetglyphs(char *, const Glyph *, const Glyph *);
static size_t tgetline(char *, const Glyph *);
static void term_move_to(int, int);
static void term_move_abs_to(int, int);
static void term_new_line(int);
static void term_put_tab(int);
static void term_putc(Rune);
static void term_reset(void);
static void term_scroll_up(int, int, int, int);
static void term_scroll_down(int, int);
static void treflow(int, int);
static void rscrolldown(int);
static void tresizedef(int, int);
static void tresizealt(int, int);
static void term_set_attr(const int *, int);
static void term_set_char(Rune, const Glyph *, int, int);
static void term_set_dirt(int, int);
static void term_set_scroll(int, int);
static void term_swap_screen(void);
static void tloaddefscreen(int, int);
static void tloadaltscreen(int, int);
static void term_set_mode(int, int, const int *, int);
static int term_write(const char *, int, int);
static void term_full_dirt(void);
static void term_control_code(uchar );
static void term_dec_test(char );
static void term_def_utf8(char);
static int32_t term_def_color(const int *, int *, int);
static void term_def_tran(char);
static void term_str_sequence(uchar);

static void draw_region(int, int, int, int);

static void selection_normalize(void);
static void selection_scroll(int, int, int);
static void selmove(int);
static void selremove(void);
static int regionselected(int, int, int, int);
static void selection_snap(int *, int *, int);

static size_t utf8_decode(const char *, Rune *, size_t);
static Rune utf8_decode_byte(char, size_t *);
static char utf8_encode_byte(Rune, size_t);
static size_t utf8_validate(Rune *, size_t);

static char *base64_decode(const char *);
static char base64_decode_getc(const char **);

static ssize_t xwrite(int, const char *, size_t);

/* Globals */
static Term term;
static Selection selection;
static CSIEscape csiescseq;
static STREscape strescseq;
static int iofd = 1;
static int cmdfd;
static pid_t pid;

static const uchar utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const uchar utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const Rune utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const Rune utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

ssize_t
xwrite(int fd, const char *s, size_t len)
{
	size_t aux = len;
	ssize_t r;

	while (len > 0) {
		r = write(fd, s, len);
		if (r < 0)
			return r;
		len -= r;
		s += r;
	}

	return aux;
}

void *
xmalloc(size_t len)
{
	void *p;

	if (!(p = malloc(len)))
		die("malloc: %s\n", strerror(errno));

	return p;
}

void *
xrealloc(void *p, size_t len)
{
	if ((p = realloc(p, len)) == NULL)
		die("realloc: %s\n", strerror(errno));

	return p;
}

char *
xstrdup(const char *s)
{
	char *p;

	if ((p = strdup(s)) == NULL)
		die("strdup: %s\n", strerror(errno));

	return p;
}

size_t
utf8_decode(const char *c, Rune *u, size_t clen)
{
	size_t len, type;
	Rune udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8_decode_byte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	{
		size_t j = 1;
		for (size_t i = 1; i < clen && j < len; ++i, ++j) {
			udecoded = (udecoded << 6) | utf8_decode_byte(c[i], &type);
			if (type != 0)
				return j;
		}
		if (j < len)
			return 0;
	}
	*u = udecoded;
	utf8_validate(u, len);

	return len;
}

Rune
utf8_decode_byte(char c, size_t *i)
{
	for (*i = 0; *i < LEN(utfmask); ++(*i))
		if (((uchar)c & utfmask[*i]) == utfbyte[*i])
			return (uchar)c & ~utfmask[*i];

	return 0;
}

size_t
utf8_encode(Rune u, char *c)
{
	size_t len;

	len = utf8_validate(&u, 0);
	if (len > UTF_SIZ)
		return 0;

	for (size_t i = len - 1; i != 0; --i) {
		c[i] = utf8_encode_byte(u, 0);
		u >>= 6;
	}
	c[0] = utf8_encode_byte(u, len);

	return len;
}

char
utf8_encode_byte(Rune u, size_t i)
{
	return utfbyte[i] | (u & ~utfmask[i]);
}

size_t
utf8_validate(Rune *u, size_t i)
{
	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i)
		;

	return i;
}

char
base64_decode_getc(const char **src)
{
	while (**src && !isprint((unsigned char)**src))
		(*src)++;
	return **src ? *((*src)++) : '=';  /* emulate padding if string ends */
}

char *
base64_decode(const char *src)
{
	size_t in_len = strlen(src);
	char *result, *dst;
	static const char base64_digits[256] = {
		[43] = 62, 0, 0, 0, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
		0, 0, 0, -1, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
		13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0, 0, 0, 0,
		0, 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
		40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
	};

	if (in_len % 4)
		in_len += 4 - (in_len % 4);
	result = dst = xmalloc(in_len / 4 * 3 + 1);
	while (*src) {
		int a = base64_digits[(unsigned char) base64_decode_getc(&src)];
		int b = base64_digits[(unsigned char) base64_decode_getc(&src)];
		int c = base64_digits[(unsigned char) base64_decode_getc(&src)];
		int d = base64_digits[(unsigned char) base64_decode_getc(&src)];

		/* invalid input. 'a' can be -1, e.g. if src is "\n" (c-str) */
		if (a == -1 || b == -1)
			break;

		*dst++ = (a << 2) | ((b & 0x30) >> 4);
		if (c == -1)
			break;
		*dst++ = ((b & 0x0f) << 4) | ((c & 0x3c) >> 2);
		if (d == -1)
			break;
		*dst++ = ((c & 0x03) << 6) | d;
	}
	*dst = '\0';
	return result;
}

void
selection_init(void)
{
	selection.mode = SELECTION_IDLE;
	selection.snap = 0;
	selection.ob.x = -1;
}

int
term_line_len(Line line)
{
	int i = term.col - 1;

	for (; i >= 0 && !(line[i].mode & (ATTR_SET | ATTR_WRAP)); i--);
	return i + 1;
}

int
tiswrapped(Line line)
{
	int len = term_line_len(line);

	return len > 0 && (line[len - 1].mode & ATTR_WRAP);
}

char *
tgetglyphs(char *buf, const Glyph *gp, const Glyph *lgp)
{
	while (gp <= lgp)
		if (gp->mode & ATTR_WDUMMY) {
			gp++;
		} else {
			buf += utf8_encode((gp++)->rune, buf);
		}
	return buf;
}

size_t
tgetline(char *buf, const Glyph *fgp)
{
	char *ptr;
	const Glyph *lgp = &fgp[term.col - 1];

	while (lgp > fgp && !(lgp->mode & (ATTR_SET | ATTR_WRAP)))
		lgp--;
	ptr = tgetglyphs(buf, fgp, lgp);
	if (!(lgp->mode & ATTR_WRAP))
		*(ptr++) = '\n';
	return ptr - buf;
}

int
tlinehistlen(int y)
{
	int i = term.col;

	if (TLINE_HIST(y)[i - 1].mode & ATTR_WRAP)
		return i;

	while (i > 0 && TLINE_HIST(y)[i - 1].rune == ' ')
		--i;

	return i;
}

void
selection_start(int col, int row, int snap)
{
	selection_clear();
	selection.mode = SELECTION_EMPTY;
	selection.type = SELECTION_REGULAR;
	selection.alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);
	selection.snap = snap;
	selection.oe.x = selection.ob.x = col;
	selection.oe.y = selection.ob.y = row;
	selection_normalize();

	if (selection.snap != 0)
		selection.mode = SELECTION_READY;
	term_set_dirt(selection.nb.y, selection.ne.y);
}

void
selection_extend(int col, int row, int type, int done)
{
	int oldey, oldex, oldsby, oldsey, oldtype;

	if (selection.mode == SELECTION_IDLE)
		return;
	if (done && selection.mode == SELECTION_EMPTY) {
		selection_clear();
		return;
	}

	oldey = selection.oe.y;
	oldex = selection.oe.x;
	oldsby = selection.nb.y;
	oldsey = selection.ne.y;
	oldtype = selection.type;

	selection.oe.x = col;
	selection.oe.y = row;
	selection.type = type;
	selection_normalize();

	if (oldey != selection.oe.y || oldex != selection.oe.x ||
	    oldtype != selection.type || selection.mode == SELECTION_EMPTY)
		term_set_dirt(MIN(selection.nb.y, oldsby), MAX(selection.ne.y, oldsey));

	selection.mode = done ? SELECTION_IDLE : SELECTION_READY;
}

void
selection_normalize(void)
{
    int i;

    if (selection.type == SELECTION_REGULAR && selection.ob.y != selection.oe.y) {
        selection.nb.x = selection.ob.y < selection.oe.y ? selection.ob.x : selection.oe.x;
        selection.ne.x = selection.ob.y < selection.oe.y ? selection.oe.x : selection.ob.x;
    } else {
        selection.nb.x = MIN(selection.ob.x, selection.oe.x);
        selection.ne.x = MAX(selection.ob.x, selection.oe.x);
    }
    selection.nb.y = MIN(selection.ob.y, selection.oe.y);
    selection.ne.y = MAX(selection.ob.y, selection.oe.y);

    selection_snap(&selection.nb.x, &selection.nb.y, -1);
    selection_snap(&selection.ne.x, &selection.ne.y, +1);

    /* expand selection over line breaks */
    if (selection.type == SELECTION_RECTANGULAR)
        return;

    i = term_line_len(TLINE(selection.nb.y));
    if (selection.nb.x > i)
        selection.nb.x = i;
    if (selection.ne.x >= term_line_len(TLINE(selection.ne.y)))
        selection.ne.x = term.col - 1;
}

    int
regionselected(int x1, int y1, int x2, int y2)
{
    if (selection.ob.x == -1 || selection.mode == SELECTION_EMPTY ||
            selection.alt != TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN) || selection.nb.y > y2 || selection.ne.y < y1)
        return 0;

    return (selection.type == SELECTION_RECTANGULAR) ? selection.nb.x <= x2 && selection.ne.x >= x1
        : (selection.nb.y != y2 || selection.nb.x <= x2) &&
        (selection.ne.y != y1 || selection.ne.x >= x1);
}

    int
selected(int x, int y)
{
    return regionselected(x, y, x, y);
}


void
selection_snap(int *x, int *y, int direction)
{
	int newx, newy, xt, yt;
	int rtop = 0, rbot = term.row - 1;
	int delim, prevdelim;
	const Glyph *gp, *prevgp;

	if (!TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN))
		rtop += -term.histf + term.scr, rbot += term.scr;

	switch (selection.snap) {
	case SELECTION_SNAP_WORD:
		/*
		 * Snap around if the word wraps around at the end or
		 * beginning of a line.
		 */
		prevgp = &TLINE(*y)[*x];
		prevdelim = ISDELIM(prevgp->rune);
		for (;;) {
			newx = *x + direction;
			newy = *y;
			if (!BETWEEN(newx, 0, term.col - 1)) {
				newy += direction;
				newx = (newx + term.col) % term.col;
				if (!BETWEEN(newy, rtop, rbot))
					break;

				if (direction > 0)
					yt = *y, xt = *x;
				else
					yt = newy, xt = newx;
				if (!(TLINE(yt)[xt].mode & ATTR_WRAP))
					break;
			}

            if (newx >= term_line_len(TLINE(newy)))
				break;

			gp = &TLINE(newy)[newx];
			delim = ISDELIM(gp->rune);
			if (!(gp->mode & ATTR_WDUMMY) && (delim != prevdelim ||
			    (delim && !(gp->rune == ' ' && prevgp->rune == ' '))))
				break;

			*x = newx;
			*y = newy;
			prevgp = gp;
			prevdelim = delim;
		}
		break;
	case SELECTION_SNAP_LINE:
		/*
		 * Snap around if the the previous line or the current one
		 * has set ATTR_WRAP at its end. Then the whole next or
		 * previous line will be selected.
		 */
        *x = (direction < 0) ? 0 : term.col - 1;
        if (direction < 0) {
            for (; *y > rtop; *y -= 1) {
                if (!tiswrapped(TLINE(*y-1)))
                        break;
			}
		} else if (direction > 0) {
			for (; *y < rbot; *y += 1) {
				if (!tiswrapped(TLINE(*y)))
					break;
			}
		}
		break;
	}
}

char *
get_sel(void)
{
	char *str, *ptr;
	int y, lastx, linelen;
	const Glyph *gp, *lgp;

	if (selection.ob.x == -1 || selection.alt != TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN))
		return NULL;

	str = xmalloc((term.col + 1) * (selection.ne.y - selection.nb.y + 1) * UTF_SIZ);
	ptr = str;

	/* append every set & selected glyph to the selection */
	for (y = selection.nb.y; y <= selection.ne.y; y++) {
		Line line = TLINE(y);

		if ((linelen = term_line_len(line)) == 0) {
			*ptr++ = '\n';
			continue;
		}

		if (selection.type == SELECTION_RECTANGULAR) {
			gp = &line[selection.nb.x];
			lastx = selection.ne.x;
		} else {
			gp = &line[selection.nb.y == y ? selection.nb.x : 0];
			lastx = (selection.ne.y == y) ? selection.ne.x : term.col-1;
		}
		lgp = &line[MIN(lastx, linelen-1)];

		ptr = tgetglyphs(ptr, gp, lgp);

		/*
		 * Copy and pasting of line endings is inconsistent
		 * in the inconsistent terminal and GUI world.
		 * The best solution seems like to produce '\n' when
		 * something is copied from st and convert '\n' to
		 * '\r', when something to be pasted is received by
		 * st.
		 * FIXME: Fix the computer world.
		 */
		if ((y < selection.ne.y || lastx >= linelen) &&
		    (!(lgp->mode & ATTR_WRAP) || selection.type == SELECTION_RECTANGULAR))
			*ptr++ = '\n';
	}
	*ptr = '\0';
	return str;
}

void
selection_clear(void)
{
	if (selection.ob.x == -1)
		return;
	selremove();
	term_set_dirt(selection.nb.y, selection.ne.y);
}

void
selremove(void)
{
	selection.mode = SELECTION_IDLE;
	selection.ob.x = -1;
}

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

void
exec_shell(char *cmd, char **args)
{
	char *sh, *prog, *arg;
	const struct passwd *pw;

	errno = 0;
	if ((pw = getpwuid(getuid())) == NULL) {
		if (errno)
			die("getpwuid: %s\n", strerror(errno));
		else
			die("who are you?\n");
	}

	if ((sh = getenv("SHELL")) == NULL)
		sh = (pw->pw_shell[0]) ? pw->pw_shell : cmd;

	if (args) {
		prog = args[0];
		arg = NULL;
	} else if (scroll) {
		prog = scroll;
		arg = utmp ? utmp : sh;
	} else if (utmp) {
		prog = utmp;
		arg = NULL;
	} else {
		prog = sh;
		arg = NULL;
	}
	DEFAULT(args, ((char *[]) {prog, arg, NULL}));

	unsetenv("COLUMNS");
	unsetenv("LINES");
	unsetenv("TERMCAP");
	setenv("LOGNAME", pw->pw_name, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("SHELL", sh, 1);
	setenv("HOME", pw->pw_dir, 1);
	setenv("TERM", termname, 1);

	signal(SIGCHLD, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGALRM, SIG_DFL);

	execvp(prog, args);
	_exit(1);
}

void
handler_sigchld(int a)
{
	int stat;
	pid_t p;

	if ((p = waitpid(pid, &stat, WNOHANG)) < 0)
		die("waiting for pid %hd failed: %s\n", pid, strerror(errno));

	if (pid != p) {
		if (p == 0 && wait(&stat) < 0)
			die("wait: %s\n", strerror(errno));

		/* reinstall handler_sigchld handler */
		signal(SIGCHLD, handler_sigchld);
		return;
	}

	if (WIFEXITED(stat) && WEXITSTATUS(stat))
		die("child exited with status %d\n", WEXITSTATUS(stat));
	else if (WIFSIGNALED(stat))
		die("child terminated due to signal %d\n", WTERMSIG(stat));
	_exit(0);
}

void
stty(char **args)
{
	char cmd[_POSIX_ARG_MAX], **p, *q, *s;
	size_t n, siz;

	if ((n = strlen(stty_args)) > sizeof(cmd)-1)
		die("incorrect stty parameters\n");
	memcpy(cmd, stty_args, n);
	q = cmd + n;
	siz = sizeof(cmd) - n;
	for (p = args; p && (s = *p); ++p) {
		if ((n = strlen(s)) > siz-1)
			die("stty parameter length too long\n");
		*q++ = ' ';
		memcpy(q, s, n);
		q += n;
		siz -= n + 1;
	}
	*q = '\0';
	if (system(cmd) != 0)
		perror("Couldn't call stty");
}

int
tty_new(const char *line, char *cmd, const char *out, char **args)
{
	int m, s;

	if (out) {
		term.mode |= TERM_MODE_PRINT;
		iofd = (!strcmp(out, "-")) ?
			  1 : open(out, O_WRONLY | O_CREAT, 0666);
		if (iofd < 0) {
			fprintf(stderr, "Error opening %s:%s\n",
				out, strerror(errno));
		}
	}

	if (line) {
		if ((cmdfd = open(line, O_RDWR)) < 0)
			die("open line '%s' failed: %s\n",
			    line, strerror(errno));
		dup2(cmdfd, 0);
		stty(args);
		return cmdfd;
	}

	/* seems to work fine on linux, openbsd and freebsd */
	if (openpty(&m, &s, NULL, NULL, NULL) < 0)
		die("openpty failed: %s\n", strerror(errno));

	switch (pid = fork()) {
	case -1:
		die("fork failed: %s\n", strerror(errno));
		break;
	case 0:
		close(iofd);
		close(m);
		setsid(); /* create a new process group */
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (ioctl(s, TIOCSCTTY, NULL) < 0)
			die("ioctl TIOCSCTTY failed: %s\n", strerror(errno));
		if (s > 2)
			close(s);
#ifdef __OpenBSD__
		if (pledge("stdio getpw proc exec", NULL) == -1)
			die("pledge\n");
#endif
		exec_shell(cmd, args);
		break;
	default:
#ifdef __OpenBSD__
		if (pledge("stdio rpath tty proc exec", NULL) == -1)
			die("pledge\n");
#endif
		close(s);
		cmdfd = m;
		signal(SIGCHLD, handler_sigchld);
		break;
	}
	return cmdfd;
}

size_t
tty_read(void)
{
	static char buf[BUFSIZ];
	static int buflen = 0;
	int ret, written;

	/* append read bytes to unprocessed bytes */
	ret = read(cmdfd, buf+buflen, LEN(buf)-buflen);

	switch (ret) {
	case 0:
		exit(0);
	case -1:
		die("couldn't read from shell: %s\n", strerror(errno));
	default:
		buflen += ret;
		written = term_write(buf, buflen, 0);
		buflen -= written;
		/* keep any incomplete UTF-8 byte sequence for the next call */
		if (buflen > 0)
			memmove(buf, buf + written, buflen);
		return ret;
	}
}

void
tty_write(const char *s, size_t n, int may_echo)
{
	const char *next;

    user_scroll_down(&((Arg){ .i = term.scr }));

	if (may_echo && TERM_MODE_IS_SET(TERM_MODE_ECHO))
		term_write(s, n, 1);

	if (!TERM_MODE_IS_SET(TERM_MODE_CRLF)) {
		tty_write_raw(s, n);
		return;
	}

	/* This is similar to how the kernel handles ONLCR for ttys */
	while (n > 0) {
		if (*s == '\r') {
			next = s + 1;
			tty_write_raw("\r\n", 2);
		} else {
			next = memchr(s, '\r', n);
			DEFAULT(next, s + n);
			tty_write_raw(s, next - s);
		}
		n -= next - s;
		s = next;
	}
}

void
tty_write_raw(const char *s, size_t n)
{
	fd_set wfd, rfd;
	ssize_t r;
	size_t lim = 256;

	/*
	 * Remember that we are using a pty, which might be a modem line.
	 * Writing too much will clog the line. That's why we are doing this
	 * dance.
	 * FIXME: Migrate the world to Plan 9.
	 */
	while (n > 0) {
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		FD_SET(cmdfd, &wfd);
		FD_SET(cmdfd, &rfd);

		/* Check if we can write. */
		if (pselect(cmdfd+1, &rfd, &wfd, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		if (FD_ISSET(cmdfd, &wfd)) {
			/*
			 * Only write the bytes written by tty_write() or the
			 * default of 256. This seems to be a reasonable value
			 * for a serial line. Bigger values might clog the I/O.
			 */
			if ((r = write(cmdfd, s, (n < lim)? n : lim)) < 0)
				goto write_error;
			if (r < n) {
				/*
				 * We weren't able to write out everything.
				 * This means the buffer is getting full
				 * again. Empty it.
				 */
				if (n < lim)
					lim = tty_read();
				n -= r;
				s += r;
			} else {
				/* All bytes have been written. */
				break;
			}
		}
		if (FD_ISSET(cmdfd, &rfd))
			lim = tty_read();
	}
	return;

write_error:
	die("write error on tty: %s\n", strerror(errno));
}

void
tty_resize(int tty_width, int tty_height)
{
	struct winsize winsize;

	winsize.ws_row = term.row;
	winsize.ws_col = term.col;
	winsize.ws_xpixel = tty_width;
	winsize.ws_ypixel = tty_height;
	if (ioctl(cmdfd, TIOCSWINSZ, &winsize) < 0)
		fprintf(stderr, "Couldn't set window size: %s\n", strerror(errno));
}

void
tty_hangup(void)
{
	/* Send SIGHUP to shell */
	kill(pid, SIGHUP);
}

int
term_attr_set(int attr)
{
	for (int i = 0; i < term.row-1; i++) {
		for (int j = 0; j < term.col-1; j++) {
			if (term.line[i][j].mode & attr)
				return 1;
		}
	}

	return 0;
}

void
term_set_dirt(int top, int bot)
{
	LIMIT(top, 0, term.row-1);
	LIMIT(bot, 0, term.row-1);

	for (int i = top; i <= bot; i++)
		term.dirty[i] = 1;
}

void
term_set_dirt_attr(int attr)
{
	for (int i = 0; i < term.row-1; i++) {
		for (int j = 0; j < term.col-1; j++) {
			if (term.line[i][j].mode & attr) {
				term.dirty[i] = 1;
				break;
			}
		}
	}
}

void
term_full_dirt(void)
{
    for (int i = 0; i < term.row; i++)
        term.dirty[i] = 1;
}

void
term_cursor(int mode)
{
	static TCursor c[2];
	int alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);

	if (mode == CURSOR_SAVE) {
		c[alt] = term.cursor;
	} else if (mode == CURSOR_LOAD) {
		term.cursor = c[alt];
		term_move_to(c[alt].x, c[alt].y);
	}
}

void
tresetcursor(void)
{
	term.cursor = (TCursor){ { .mode = ATTR_NULL, .fg = default_foreground, .bg = default_background },
	                    .x = 0, .y = 0, .state = CURSOR_DEFAULT };
}

void
term_reset(void)
{
	tresetcursor();

	memset(term.tabs, 0, term.col * sizeof(*term.tabs));
	for (uint i = tabspaces; i < term.col; i += tabspaces)
		term.tabs[i] = 1;
	term.top = 0;
	term.histf = 0;
	term.scr = 0;
	term.bot = term.row - 1;
	term.mode = TERM_MODE_WRAP|TERM_MODE_UTF8;
	memset(term.trantbl, CS_USA, sizeof(term.trantbl));
	term.charset = 0;

    selremove();
	for (uint i = 0; i < 2; i++) {
        term_cursor(CURSOR_SAVE); /* reset saved cursor */
        for (int y = 0; y < term.row; y++)
            for (int x = 0; x < term.col; x++)
                tclearglyph(&term.line[y][x], 0);
        term_swap_screen();
	}
    term_full_dirt();
}

void
term_new(int col, int row)
{
    for (int i = 0; i < 2; i++) {
        term.line = xmalloc(row * sizeof(Line));
        for (int j = 0; j < row; j++)
            term.line[j] = xmalloc(col * sizeof(Glyph));
        term.col = col, term.row = row;
        term_swap_screen();
    }
    term.dirty = xmalloc(row * sizeof(*term.dirty));
    term.tabs = xmalloc(col * sizeof(*term.tabs));
    for (int i = 0; i < HISTSIZE; i++)
        term.hist[i] = xmalloc(col * sizeof(Glyph));
    term_reset();
}

/* handle it with care */
void
term_swap_screen(void)
{
	static Line *altline;
	static int altcol, altrow;
	Line *tmpline = term.line;
	int tmpcol = term.col, tmprow = term.row;

	term.line = altline;
	term.col = altcol, term.row = altrow;
	altline = tmpline;
	altcol = tmpcol, altrow = tmprow;
	term.mode ^= TERM_MODE_ALTSCREEN;
}

void
tloaddefscreen(int clear, int loadcursor)
{
	int col, row, alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);

	if (alt) {
		if (clear)
			term_clear_region(0, 0, term.col-1, term.row-1, 1);
		col = term.col, row = term.row;
		term_swap_screen();
	}
	if (loadcursor)
		term_cursor(CURSOR_LOAD);
	if (alt)
		tresizedef(col, row);
}

void
tloadaltscreen(int clear, int savecursor)
{
	int col, row, def = !TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);

	if (savecursor)
		term_cursor(CURSOR_SAVE);
	if (def) {
		col = term.col, row = term.row;
		term_swap_screen();
		term.scr = 0;
		tresizealt(col, row);
	}
	if (clear)
		term_clear_region(0, 0, term.col-1, term.row-1, 1);
}

int
tisaltscreen(void)
{
	return TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);
}


void
user_scroll_down(const Arg* a)
{
    int n = a->i;

    if (!term.scr || TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN))
        return;

    if (n < 0)
        n = MAX(term.row / -n, 1);

    if (n <= term.scr) {
        term.scr -= n;
    } else {
        n = term.scr;
        term.scr = 0;
    }
	if (selection.ob.x != -1 && !selection.alt)
		selmove(-n); /* negate change in term.scr */
	term_full_dirt();
}



void
user_scroll_up(const Arg* a)
{
    int n = a->i;

    if (!term.histf || TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN))
        return;

    if (n < 0)
        n = MAX(term.row / -n, 1);

    if (term.scr + n <= term.histf) {
        term.scr += n;
    } else {
        n = term.histf - term.scr;
        term.scr = term.histf;
    }

    if (selection.ob.x != -1 && !selection.alt)
        selmove(n); /* negate change in term.scr */
    term_full_dirt();

}

void
term_scroll_down(int top, int n)
{
    int bot = term.bot;
    Line temp;

    if (n <= 0)
        return;
    n = MIN(n, bot-top+1);

    term_set_dirt(top, bot-n);
    term_clear_region(0, bot-n+1, term.col-1, bot, 1);

    for (int i = bot; i >= top+n; i--) {
        temp = term.line[i];
        term.line[i] = term.line[i-n];
        term.line[i-n] = temp;
    }

    if (selection.ob.x != -1 && selection.alt == TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN))
        selection_scroll(top, bot, n);
}

void
term_scroll_up(int top, int bot, int n, int mode)
{
    int s;
    int alt = TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN);
    int savehist = !alt && top == 0 && mode != SCROLL_NOSAVEHIST;
    Line temp;

    if (n <= 0)
        return;
    n = MIN(n, bot-top+1);

    if (savehist) {
        for (int i = 0; i < n; i++) {
            term.histi = (term.histi + 1) % HISTSIZE;
            temp = term.hist[term.histi];
            for (int j = 0; j < term.col; j++)
                tclearglyph(&temp[j], 1);
            term.hist[term.histi] = term.line[i];
            term.line[i] = temp;
        }
        term.histf = MIN(term.histf + n, HISTSIZE);
        s = n;
        if (term.scr) {
            int j = term.scr;
            term.scr = MIN(j + n, HISTSIZE);
            s = j + n - term.scr;
        }
        if (mode != SCROLL_RESIZE)
            term_full_dirt();
    } else {
        term_clear_region(0, top, term.col-1, top+n-1, 1);
        term_set_dirt(top+n, bot);
    }

    for (int i = top; i <= bot-n; i++) {
        temp = term.line[i];
        term.line[i] = term.line[i+n];
        term.line[i+n] = temp;
    }

    if (selection.ob.x != -1 && selection.alt == alt) {
        if (!savehist) {
            selection_scroll(top, bot, -n);
        } else if (s > 0) {
            selmove(-s);
            if (-term.scr + selection.nb.y < -term.histf)
                selremove();
        }
    }
}

void
selmove(int n)
 {
	selection.ob.y += n, selection.nb.y += n;
	selection.oe.y += n, selection.ne.y += n;
}

void
selection_scroll(int top, int bot, int n)
{
    /* turn absolute coordinates into relative */
    top += term.scr, bot += term.scr;

    if (BETWEEN(selection.nb.y, top, bot) != BETWEEN(selection.ne.y, top, bot)) {
        selection_clear();
    } else if (BETWEEN(selection.nb.y, top, bot)) {
        selmove(n);
        if (selection.nb.y < top || selection.ne.y > bot)
            selection_clear();
	}
}

void
term_new_line(int first_col)
{
	int y = term.cursor.y;

	if (y == term.bot) {
		term_scroll_up(term.top, term.bot, 1, SCROLL_SAVEHIST);
	} else {
		y++;
	}
	term_move_to(first_col ? 0 : term.cursor.x, y);
}

void
control_seq_intro_parse(void)
{
	char *p = csiescseq.buf, *np;
	long int v;
	int sep = ';'; /* colon or semi-colon, but not both */

	csiescseq.narg = 0;
	if (*p == '?') {
		csiescseq.priv = 1;
		p++;
	}

	csiescseq.buf[csiescseq.len] = '\0';
	while (p < csiescseq.buf+csiescseq.len) {
		np = NULL;
		v = strtol(p, &np, 10);
		if (np == p)
			v = 0;
		if (v == LONG_MAX || v == LONG_MIN)
			v = -1;
		csiescseq.arg[csiescseq.narg++] = v;
		p = np;
		if (sep == ';' && *p == ':')
			sep = ':'; /* allow override to colon once */
		if (*p != sep || csiescseq.narg == ESC_ARG_SIZ)
			break;
		p++;
	}
	csiescseq.mode[0] = *p++;
	csiescseq.mode[1] = (p < csiescseq.buf+csiescseq.len) ? *p : '\0';
}

/* for absolute user moves, when decom is set */
void
term_move_abs_to(int x, int y)
{
	term_move_to(x, y + ((term.cursor.state & CURSOR_ORIGIN) ? term.top: 0));
}

void
term_move_to(int x, int y)
{
	int miny, maxy;

	if (term.cursor.state & CURSOR_ORIGIN) {
		miny = term.top;
		maxy = term.bot;
	} else {
		miny = 0;
		maxy = term.row - 1;
	}
	term.cursor.state &= ~CURSOR_WRAPNEXT;
	term.cursor.x = LIMIT(x, 0, term.col-1);
	term.cursor.y = LIMIT(y, miny, maxy);
}

void
term_set_char(Rune u, const Glyph *attr, int x, int y)
{
    static const char *vt100_0[62] = { /* 0x41 - 0x7e */
        "↑", "↓", "→", "←", "█", "▚", "☃", /* A - G */
        0, 0, 0, 0, 0, 0, 0, 0, /* H - O */
        0, 0, 0, 0, 0, 0, 0, 0, /* P - W */
        0, 0, 0, 0, 0, 0, 0, " ", /* X - _ */
        "◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
        "␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
        "⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
        "│", "≤", "≥", "π", "≠", "£", "·", /* x - ~ */
    };

    /*
     * The table is proudly stolen from rxvt.
     */
    if (term.trantbl[term.charset] == CS_GRAPHIC0 &&
            BETWEEN(u, 0x41, 0x7e) && vt100_0[u - 0x41])
        utf8_decode(vt100_0[u - 0x41], &u, UTF_SIZ);

    if (term.line[y][x].mode & ATTR_WIDE) {
        if (x+1 < term.col) {
            term.line[y][x+1].rune = ' ';
            term.line[y][x+1].mode &= ~ATTR_WDUMMY;
        }
    } else if (term.line[y][x].mode & ATTR_WDUMMY) {
        term.line[y][x-1].rune = ' ';
        term.line[y][x-1].mode &= ~ATTR_WIDE;
    }

    term.dirty[y] = 1;
    term.line[y][x] = *attr;
    term.line[y][x].rune = u;
    term.line[y][x].mode |= ATTR_SET;

	if (isboxdraw(u))
		term.line[y][x].mode |= ATTR_BOXDRAW;
}



void
tclearglyph(Glyph *gp, int usecurattr)
{
	if (usecurattr) {
		gp->fg = term.cursor.attr.fg;
		gp->bg = term.cursor.attr.bg;
	} else {
		gp->fg = default_foreground;
		gp->bg = default_background;
	}
	gp->mode = ATTR_NULL;
	gp->rune = ' ';
}



void
term_clear_region(int x1, int y1, int x2, int y2, int usecurattr)
{
	int x, y;
	/* regionselected() takes relative coordinates */
	if (regionselected(x1+term.scr, y1+term.scr, x2+term.scr, y2+term.scr))
		selremove();

    for (y = y1; y <= y2; y++) {
		term.dirty[y] = 1;
		for (x = x1; x <= x2; x++)
			tclearglyph(&term.line[y][x], usecurattr);
	}
}

void
term_delete_char(int n)
{
    int src, dst, size;
    Line line;

    if (n <= 0)
        return;

    dst = term.cursor.x;
    src = MIN(term.cursor.x + n, term.col);
    size = term.col - src;
    if (size > 0) {
        /*
         * otherwise src would point beyond the array
         * https://stackoverflow.com/questions/29844298
         */
        line = term.line[term.cursor.y];
        memmove(&line[dst], &line[src], size * sizeof(Glyph));
    }
    term_clear_region(dst + size, term.cursor.y, term.col - 1, term.cursor.y, 1);
}

void
term_insert_blank(int n)
{
    int src, dst, size;
    Line line;

    if (n <= 0)
        return;
    dst = MIN(term.cursor.x + n, term.col);
    src = term.cursor.x;
    size = term.col - dst;
    if (size > 0) { /* otherwise dst would point beyond the array */
        line = term.line[term.cursor.y];
        memmove(&line[dst], &line[src], size * sizeof(Glyph));
    }
    term_clear_region(src, term.cursor.y, dst - 1, term.cursor.y, 1);
}

void
term_insert_blank_line(int n)
{
	if (BETWEEN(term.cursor.y, term.top, term.bot))
		term_scroll_down(term.cursor.y, n);
}

void
term_delete_line(int n)
{
	if (BETWEEN(term.cursor.y, term.top, term.bot))
		term_scroll_up(term.cursor.y, term.bot, n, SCROLL_NOSAVEHIST);
}

int32_t
term_def_color(const int *attr, int *npar, int l)
{
	int32_t idx = -1;
	uint r, g, b;

	switch (attr[*npar + 1]) {
	case 2: /* direct color in RGB space */
		if (*npar + 4 >= l) {
			fprintf(stderr,
				"erresc(38): Incorrect number of parameters (%d)\n",
				*npar);
			break;
		}
		r = attr[*npar + 2];
		g = attr[*npar + 3];
		b = attr[*npar + 4];
		*npar += 4;
		if (!BETWEEN(r, 0, 255) || !BETWEEN(g, 0, 255) || !BETWEEN(b, 0, 255))
			fprintf(stderr, "erresc: bad rgb color (%u,%u,%u)\n",
				r, g, b);
		else
			idx = TRUECOLOR(r, g, b);
		break;
	case 5: /* indexed color */
		if (*npar + 2 >= l) {
			fprintf(stderr,
				"erresc(38): Incorrect number of parameters (%d)\n",
				*npar);
			break;
		}
		*npar += 2;
		if (!BETWEEN(attr[*npar], 0, 255))
			fprintf(stderr, "erresc: bad fgcolor %d\n", attr[*npar]);
		else
			idx = attr[*npar];
		break;
	case 0: /* implemented defined (only foreground) */
	case 1: /* transparent */
	case 3: /* direct color in CMY space */
	case 4: /* direct color in CMYK space */
	default:
		fprintf(stderr,
		        "erresc(38): gfx attr %d unknown\n", attr[*npar]);
		break;
	}

	return idx;
}

void
term_set_attr(const int *attr, int l)
{
	int32_t idx;

	for (int i = 0; i < l; i++) {
		switch (attr[i]) {
		case 0:
			term.cursor.attr.mode &= ~(
				ATTR_BOLD       |
				ATTR_FAINT      |
				ATTR_ITALIC     |
				ATTR_UNDERLINE  |
				ATTR_BLINK      |
				ATTR_REVERSE    |
				ATTR_INVISIBLE  |
				ATTR_STRUCK     );
			term.cursor.attr.fg = default_foreground;
			term.cursor.attr.bg = default_background;
			break;
		case 1:
			term.cursor.attr.mode |= ATTR_BOLD;
			break;
		case 2:
			term.cursor.attr.mode |= ATTR_FAINT;
			break;
		case 3:
			term.cursor.attr.mode |= ATTR_ITALIC;
			break;
		case 4:
			term.cursor.attr.mode |= ATTR_UNDERLINE;
			break;
		case 5: /* slow blink */
			/* FALLTHROUGH */
		case 6: /* rapid blink */
			term.cursor.attr.mode |= ATTR_BLINK;
			break;
		case 7:
			term.cursor.attr.mode |= ATTR_REVERSE;
			break;
		case 8:
			term.cursor.attr.mode |= ATTR_INVISIBLE;
			break;
		case 9:
			term.cursor.attr.mode |= ATTR_STRUCK;
			break;
		case 22:
			term.cursor.attr.mode &= ~(ATTR_BOLD | ATTR_FAINT);
			break;
		case 23:
			term.cursor.attr.mode &= ~ATTR_ITALIC;
			break;
		case 24:
			term.cursor.attr.mode &= ~ATTR_UNDERLINE;
			break;
		case 25:
			term.cursor.attr.mode &= ~ATTR_BLINK;
			break;
		case 27:
			term.cursor.attr.mode &= ~ATTR_REVERSE;
			break;
		case 28:
			term.cursor.attr.mode &= ~ATTR_INVISIBLE;
			break;
		case 29:
			term.cursor.attr.mode &= ~ATTR_STRUCK;
			break;
		case 38:
			if ((idx = term_def_color(attr, &i, l)) >= 0)
				term.cursor.attr.fg = idx;
			break;
		case 39: /* set foreground color to default */
			term.cursor.attr.fg = default_foreground;
			break;
		case 48:
			if ((idx = term_def_color(attr, &i, l)) >= 0)
				term.cursor.attr.bg = idx;
			break;
		case 49: /* set background color to default */
			term.cursor.attr.bg = default_background;
			break;
		case 58:
			/* This starts a sequence to change the color of
			 * "underline" pixels. We don't support that and
			 * instead eat up a following "5;n" or "2;r;g;b". */
			term_def_color(attr, &i, l);
			break;
		default:
			if (BETWEEN(attr[i], 30, 37)) {
				term.cursor.attr.fg = attr[i] - 30;
			} else if (BETWEEN(attr[i], 40, 47)) {
				term.cursor.attr.bg = attr[i] - 40;
			} else if (BETWEEN(attr[i], 90, 97)) {
				term.cursor.attr.fg = attr[i] - 90 + 8;
			} else if (BETWEEN(attr[i], 100, 107)) {
				term.cursor.attr.bg = attr[i] - 100 + 8;
			} else {
				fprintf(stderr,
					"erresc(default): gfx attr %d unknown\n",
					attr[i]);
				control_seq_intro_dump();
			}
			break;
		}
	}
}

void
term_set_scroll(int t, int b)
{
	int temp;

	LIMIT(t, 0, term.row-1);
	LIMIT(b, 0, term.row-1);
	if (t > b) {
		temp = t;
		t = b;
		b = temp;
	}
	term.top = t;
	term.bot = b;
}

void
term_set_mode(int priv, int set, const int *args, int narg)
{
	const int *lim;

	for (lim = args + narg; args < lim; ++args) {
		if (priv) {
			switch (*args) {
			case 1: /* DECCKM -- Cursor key */
				x_set_mode(set, WIN_MODE_APPCURSOR);
				break;
			case 5: /* DECSCNM -- Reverse video */
				x_set_mode(set, WIN_MODE_REVERSE);
				break;
			case 6: /* DECOM -- Origin */
				MODBIT(term.cursor.state, set, CURSOR_ORIGIN);
				term_move_abs_to(0, 0);
				break;
			case 7: /* DECAWM -- Auto wrap */
				MODBIT(term.mode, set, TERM_MODE_WRAP);
				break;
			case 0:  /* Error (IGNORED) */
			case 2:  /* DECANM -- ANSI/VT52 (IGNORED) */
			case 3:  /* DECCOLM -- Column  (IGNORED) */
			case 4:  /* DECSCLM -- Scroll (IGNORED) */
			case 8:  /* DECARM -- Auto repeat (IGNORED) */
			case 18: /* DECPFF -- Printer feed (IGNORED) */
			case 19: /* DECPEX -- Printer extent (IGNORED) */
			case 42: /* DECNRCM -- National characters (IGNORED) */
			case 12: /* att610 -- Start blinking cursor (IGNORED) */
				break;
			case 25: /* DECTCEM -- Text Cursor Enable Mode */
				x_set_mode(!set, WIN_MODE_HIDE);
				break;
			case 9:    /* X10 mouse compatibility mode */
				x_set_pointer_motion(0);
				x_set_mode(0, WIN_MODE_MOUSE);
				x_set_mode(set, WIN_MODE_MOUSEX10);
				break;
			case 1000: /* 1000: report button press */
				x_set_pointer_motion(0);
				x_set_mode(0, WIN_MODE_MOUSE);
				x_set_mode(set, WIN_MODE_MOUSEBTN);
				break;
			case 1002: /* 1002: report motion on button press */
				x_set_pointer_motion(0);
				x_set_mode(0, WIN_MODE_MOUSE);
				x_set_mode(set, WIN_MODE_MOUSEMOTION);
				break;
			case 1003: /* 1003: enable all mouse motions */
				x_set_pointer_motion(set);
				x_set_mode(0, WIN_MODE_MOUSE);
				x_set_mode(set, WIN_MODE_MOUSEMANY);
				break;
			case 1004: /* 1004: send handler_focus events to tty */
				x_set_mode(set, WIN_MODE_FOCUS);
				break;
			case 1006: /* 1006: extended reporting mode */
				x_set_mode(set, WIN_MODE_MOUSESGR);
				break;
			case 1034: /* 1034: enable 8-bit mode for keyboard input */
				x_set_mode(set, WIN_MODE_8BIT);
				break;
			case 1049: /* swap screen & set/restore cursor as xterm */
				if (!allowaltscreen)
					break;
				term_cursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
				/* FALLTHROUGH */
			case 47: /* swap screen */
			case 1047: /*swap screen, clearing alternate screen */
				if (!allowaltscreen)
					break;
				if (set)
					tloadaltscreen(*args == 1049, *args == 1049);
				else
					tloaddefscreen(*args == 1047, *args == 1049);
				break;
				/* FALLTHROUGH */
			case 1048: /* save/restore cursor (like DECSC/DECRC) */
                if (!allowaltscreen)
                    break;
				term_cursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
				break;
			case 2004: /* 2004: bracketed paste mode */
				x_set_mode(set, WIN_MODE_BRCKTPASTE);
				break;
			/* Not implemented mouse modes. See comments there. */
			case 1001: /* mouse highlight mode; can hang the
				      terminal by design when implemented. */
			case 1005: /* UTF-8 mouse mode; will confuse
				      applications not supporting UTF-8
				      and luit. */
			case 1015: /* urxvt mangled mouse mode; incompatible
				      and can be mistaken for other control
				      codes. */
				break;
			default:
				fprintf(stderr,
					"erresc: unknown private set/reset mode %d\n",
					*args);
				break;
			}
		} else {
			switch (*args) {
			case 0:  /* Error (IGNORED) */
				break;
			case 2:
				x_set_mode(set, WIN_MODE_KBDLOCK);
				break;
			case 4:  /* IRM -- Insertion-replacement */
				MODBIT(term.mode, set, TERM_MODE_INSERT);
				break;
			case 12: /* SRM -- Send/Receive */
				MODBIT(term.mode, !set, TERM_MODE_ECHO);
				break;
			case 20: /* LNM -- Linefeed/new line */
				MODBIT(term.mode, set, TERM_MODE_CRLF);
				break;
			default:
				fprintf(stderr,
					"erresc: unknown set/reset mode %d\n",
					*args);
				break;
			}
		}
	}
}

void
control_seq_intro_handle(void)
{
	char buf[40];
	int n, x;

	switch (csiescseq.mode[0]) {
	default:
	unknown:
		fprintf(stderr, "erresc: unknown csi ");
		control_seq_intro_dump();
		/* die(""); */
		break;
	case '@': /* ICH -- Insert <n> blank char */
		DEFAULT(csiescseq.arg[0], 1);
		term_insert_blank(csiescseq.arg[0]);
		break;
	case 'A': /* CUU -- Cursor <n> Up */
		DEFAULT(csiescseq.arg[0], 1);
		term_move_to(term.cursor.x, term.cursor.y-csiescseq.arg[0]);
		break;
	case 'B': /* CUD -- Cursor <n> Down */
	case 'e': /* VPR --Cursor <n> Down */
		DEFAULT(csiescseq.arg[0], 1);
		term_move_to(term.cursor.x, term.cursor.y+csiescseq.arg[0]);
		break;
	case 'i': /* MC -- Media Copy */
		switch (csiescseq.arg[0]) {
		case 0:
			term_dump();
			break;
		case 1:
			term_dump_line(term.cursor.y);
			break;
		case 2:
			term_dump_sel();
			break;
		case 4:
			term.mode &= ~TERM_MODE_PRINT;
			break;
		case 5:
			term.mode |= TERM_MODE_PRINT;
			break;
		}
		break;
	case 'c': /* DA -- Device Attributes */
		if (csiescseq.arg[0] == 0)
			tty_write(vtiden, strlen(vtiden), 0);
		break;
	case 'b': /* REP -- if last char is printable print it <n> more times */
		LIMIT(csiescseq.arg[0], 1, 65535);
		if (term.lastc)
			while (csiescseq.arg[0]-- > 0)
				term_putc(term.lastc);
		break;
	case 'C': /* CUF -- Cursor <n> Forward */
	case 'a': /* HPR -- Cursor <n> Forward */
		DEFAULT(csiescseq.arg[0], 1);
		term_move_to(term.cursor.x+csiescseq.arg[0], term.cursor.y);
		break;
	case 'D': /* CUB -- Cursor <n> Backward */
		DEFAULT(csiescseq.arg[0], 1);
		term_move_to(term.cursor.x-csiescseq.arg[0], term.cursor.y);
		break;
	case 'E': /* CNL -- Cursor <n> Down and first col */
		DEFAULT(csiescseq.arg[0], 1);
		term_move_to(0, term.cursor.y+csiescseq.arg[0]);
		break;
	case 'F': /* CPL -- Cursor <n> Up and first col */
		DEFAULT(csiescseq.arg[0], 1);
		term_move_to(0, term.cursor.y-csiescseq.arg[0]);
		break;
	case 'g': /* TBC -- Tabulation clear */
		switch (csiescseq.arg[0]) {
		case 0: /* clear current tab stop */
			term.tabs[term.cursor.x] = 0;
			break;
		case 3: /* clear all the tabs */
			memset(term.tabs, 0, term.col * sizeof(*term.tabs));
			break;
		default:
			goto unknown;
		}
		break;
	case 'G': /* CHA -- Move to <col> */
	case '`': /* HPA */
		DEFAULT(csiescseq.arg[0], 1);
		term_move_to(csiescseq.arg[0]-1, term.cursor.y);
		break;
	case 'H': /* CUP -- Move to <row> <col> */
	case 'f': /* HVP */
		DEFAULT(csiescseq.arg[0], 1);
		DEFAULT(csiescseq.arg[1], 1);
		term_move_abs_to(csiescseq.arg[1]-1, csiescseq.arg[0]-1);
		break;
	case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
		DEFAULT(csiescseq.arg[0], 1);
		term_put_tab(csiescseq.arg[0]);
		break;
	case 'J': /* ED -- Clear screen */
		switch (csiescseq.arg[0]) {
		case 0: /* below */
			term_clear_region(term.cursor.x, term.cursor.y, term.col-1, term.cursor.y, 1);
			if (term.cursor.y < term.row-1) {
				term_clear_region(0, term.cursor.y+1, term.col-1, term.row-1, 1);
			}
			break;
		case 1: /* above */
			if (term.cursor.y >= 1)
				term_clear_region(0, 0, term.col-1, term.cursor.y-1, 1);
			term_clear_region(0, term.cursor.y, term.cursor.x, term.cursor.y, 1);
			break;
        case 2: /* all */
            if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)) {
                term_clear_region(0, 0, term.col-1, term.row-1, 1);
                break;
            }
            /* vte does this:
               term_scroll_up(0, term.row-1, term.row, SCROLL_SAVEHIST); */

            /* alacritty does this: */
            for (n = term.row-1; n >= 0 && term_line_len(term.line[n]) == 0; n--);
            if (n >= 0)
                term_scroll_up(0, term.row-1, n+1, SCROLL_SAVEHIST);
            term_scroll_up(0, term.row-1, term.row-n-1, SCROLL_NOSAVEHIST);
			break;
		default:
			goto unknown;
		}
		break;
	case 'K': /* EL -- Clear line */
		switch (csiescseq.arg[0]) {
		case 0: /* right */
			term_clear_region(term.cursor.x, term.cursor.y, term.col-1, term.cursor.y, 1);
			break;
		case 1: /* left */
			term_clear_region(0, term.cursor.y, term.cursor.x, term.cursor.y, 1);
			break;
		case 2: /* all */
			term_clear_region(0, term.cursor.y, term.col-1, term.cursor.y, 1);
			break;
		}
		break;
	case 'S': /* SU -- Scroll <n> line up */
		if (csiescseq.priv) break;
		DEFAULT(csiescseq.arg[0], 1);
		/* xterm, urxvt, alacritty save this in history */
		term_scroll_up(term.top, term.bot, csiescseq.arg[0], SCROLL_SAVEHIST);
		break;
	case 'T': /* SD -- Scroll <n> line down */
		DEFAULT(csiescseq.arg[0], 1);
		term_scroll_down(term.top, csiescseq.arg[0]);
		break;
	case 'L': /* IL -- Insert <n> blank lines */
		DEFAULT(csiescseq.arg[0], 1);
		term_insert_blank_line(csiescseq.arg[0]);
		break;
	case 'l': /* RM -- Reset Mode */
		term_set_mode(csiescseq.priv, 0, csiescseq.arg, csiescseq.narg);
		break;
	case 'M': /* DL -- Delete <n> lines */
		DEFAULT(csiescseq.arg[0], 1);
		term_delete_line(csiescseq.arg[0]);
		break;
	case 'X': /* ECH -- Erase <n> char */
		if (csiescseq.arg[0] < 0)
			return;
		DEFAULT(csiescseq.arg[0], 1);
		x = MIN(term.cursor.x + csiescseq.arg[0], term.col) - 1;
		term_clear_region(term.cursor.x, term.cursor.y, x, term.cursor.y, 1);
		break;
	case 'P': /* DCH -- Delete <n> char */
		DEFAULT(csiescseq.arg[0], 1);
		term_delete_char(csiescseq.arg[0]);
		break;
	case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
		DEFAULT(csiescseq.arg[0], 1);
		term_put_tab(-csiescseq.arg[0]);
		break;
	case 'd': /* VPA -- Move to <row> */
		DEFAULT(csiescseq.arg[0], 1);
		term_move_abs_to(term.cursor.x, csiescseq.arg[0]-1);
		break;
	case 'h': /* SM -- Set terminal mode */
		term_set_mode(csiescseq.priv, 1, csiescseq.arg, csiescseq.narg);
		break;
	case 'm': /* SGR -- Terminal attribute (color) */
		term_set_attr(csiescseq.arg, csiescseq.narg);
		break;
	case 'n': /* DSR -- Device Status Report */
		switch (csiescseq.arg[0]) {
		case 5: /* Status Report "OK" `0n` */
			tty_write("\033[0n", sizeof("\033[0n") - 1, 0);
			break;
		case 6: /* Report Cursor Position (CPR) "<row>;<column>R" */
			n = snprintf(buf, sizeof(buf), "\033[%i;%iR",
			               term.cursor.y+1, term.cursor.x+1);
			tty_write(buf, n, 0);
			break;
		default:
			goto unknown;
		}
		break;
	case 'r': /* DECSTBM -- Set Scrolling Region */
		if (csiescseq.priv) {
			goto unknown;
		} else {
			DEFAULT(csiescseq.arg[0], 1);
			DEFAULT(csiescseq.arg[1], term.row);
			term_set_scroll(csiescseq.arg[0]-1, csiescseq.arg[1]-1);
			term_move_abs_to(0, 0);
		}
		break;
	case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
		term_cursor(CURSOR_SAVE);
		break;
	case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
		if (csiescseq.priv) {
			goto unknown;
		} else {
			term_cursor(CURSOR_LOAD);
		}
		break;
	case ' ':
		switch (csiescseq.mode[1]) {
		case 'q': /* DECSCUSR -- Set Cursor Style */
			if (x_set_cursor(csiescseq.arg[0]))
				goto unknown;
			break;
		default:
			goto unknown;
		}
		break;
	}
}

void
control_seq_intro_dump(void)
{
	uint c;

	fprintf(stderr, "ESC[");
	for (size_t i = 0; i < csiescseq.len; i++) {
		c = csiescseq.buf[i] & 0xff;
		if (isprint(c)) {
			putc(c, stderr);
		} else if (c == '\n') {
			fprintf(stderr, "(\\n)");
		} else if (c == '\r') {
			fprintf(stderr, "(\\r)");
		} else if (c == 0x1b) {
			fprintf(stderr, "(\\e)");
		} else {
			fprintf(stderr, "(%02x)", c);
		}
	}
	putc('\n', stderr);
}

void
control_seq_intro_reset(void)
{
	memset(&csiescseq, 0, sizeof(csiescseq));
}

void
osc_color_response(int num, int index, int is_osc4)
{
	int n;
	char buf[32];
	unsigned char r, g, b;

	if (x_get_color(is_osc4 ? num : index, &r, &g, &b)) {
		fprintf(stderr, "erresc: failed to fetch %s color %d\n",
		        is_osc4 ? "osc4" : "osc",
		        is_osc4 ? num : index);
		return;
	}

	n = snprintf(buf, sizeof buf, "\033]%s%d;rgb:%02x%02x/%02x%02x/%02x%02x\007",
	             is_osc4 ? "4;" : "", num, r, r, g, g, b, b);
	if (n < 0 || n >= sizeof(buf)) {
		fprintf(stderr, "error: %s while printing %s response\n",
		        n < 0 ? "snprintf failed" : "truncation occurred",
		        is_osc4 ? "osc4" : "osc");
	} else {
		tty_write(buf, n, 1);
	}
}

void
string_handle(void)
{
	char *p = NULL, *dec;
	int j, narg, par;
	const struct { int idx; char *str; } osc_table[] = {
		{ default_foreground, "foreground" },
		{ default_background, "background" },
		{ default_cursor, "cursor" }
	};

	term.esc &= ~(ESC_STR_END|ESC_STR);
	string_parse();
	par = (narg = strescseq.narg) ? atoi(strescseq.args[0]) : 0;

	switch (strescseq.type) {
	case ']': /* OSC -- Operating System Command */
		switch (par) {
		case 0:
			if (narg > 1) {
				x_set_title(strescseq.args[1]);
				x_set_icon_title(strescseq.args[1]);
			}
			return;
		case 1:
			if (narg > 1)
				x_set_icon_title(strescseq.args[1]);
			return;
		case 2:
			if (narg > 1)
				x_set_title(strescseq.args[1]);
			return;
		case 52: /* manipulate selection data */
			if (narg > 2 && allowwindowops) {
				dec = base64_decode(strescseq.args[2]);
				if (dec) {
					x_set_sel(dec);
					x_clipboard_copy();
				} else {
					fprintf(stderr, "erresc: invalid base64\n");
				}
			}
			return;
		case 10: /* set dynamic VT100 text foreground color */
		case 11: /* set dynamic VT100 text background color */
		case 12: /* set dynamic text cursor color */
			if (narg < 2)
				break;
			p = strescseq.args[1];
			if ((j = par - 10) < 0 || j >= LEN(osc_table))
				break; /* shouldn't be possible */

			if (!strcmp(p, "?")) {
				osc_color_response(par, osc_table[j].idx, 0);
			} else if (x_set_color_name(osc_table[j].idx, p)) {
				fprintf(stderr, "erresc: invalid %s color: %s\n",
				        osc_table[j].str, p);
			} else {
				term_full_dirt();
			}
			return;
		case 4: /* color set */
			if (narg < 3)
				break;
			p = strescseq.args[2];
			/* FALLTHROUGH */
		case 104: /* color reset */
			j = (narg > 1) ? atoi(strescseq.args[1]) : -1;

			if (p && !strcmp(p, "?")) {
				osc_color_response(j, 0, 1);
			} else if (x_set_color_name(j, p)) {
				if (par == 104 && narg <= 1) {
					x_load_cols();
					return; /* color reset without parameter */
				}
				fprintf(stderr, "erresc: invalid color j=%d, p=%s\n",
				        j, p ? p : "(null)");
			} else {
				/*
				 * TODO if default_background color is changed, borders
				 * are dirty
				 */
				term_full_dirt();
			}
			return;
		case 110: /* reset dynamic VT100 text foreground color */
		case 111: /* reset dynamic VT100 text background color */
		case 112: /* reset dynamic text cursor color */
			if (narg != 1)
				break;
			if ((j = par - 110) < 0 || j >= LEN(osc_table))
				break; /* shouldn't be possible */
			if (x_set_color_name(osc_table[j].idx, NULL)) {
				fprintf(stderr, "erresc: %s color not found\n", osc_table[j].str);
			} else {
				term_full_dirt();
			}
			return;
		}
		break;
	case 'k': /* old title set compatibility */
		x_set_title(strescseq.args[0]);
		return;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
		return;
	}

	fprintf(stderr, "erresc: unknown str ");
	string_dump();
}

void
string_parse(void)
{
	int c;
	char *p = strescseq.buf;

	strescseq.narg = 0;
	strescseq.buf[strescseq.len] = '\0';

	if (*p == '\0')
		return;

	while (strescseq.narg < STR_ARG_SIZ) {
		strescseq.args[strescseq.narg++] = p;
		while ((c = *p) != ';' && c != '\0')
			++p;
		if (c == '\0')
			return;
		*p++ = '\0';
	}
}

void
externalpipe(const Arg *arg)
{
	int to[2];
	char buf[UTF_SIZ];
	void (*oldsigpipe)(int);
	Glyph *bp, *end;
	int lastpos, n, newline;

	if (pipe(to) == -1)
		return;

	switch (fork()) {
	case -1:
		close(to[0]);
		close(to[1]);
		return;
	case 0:
		dup2(to[0], STDIN_FILENO);
		close(to[0]);
		close(to[1]);
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "st: execvp %s\n", ((char **)arg->v)[0]);
		perror("failed");
		exit(0);
	}

	close(to[0]);
	/* ignore sigpipe for now, in case child exists early */
	oldsigpipe = signal(SIGPIPE, SIG_IGN);
	newline = 0;
	for (n = 0; n <= HISTSIZE + 2; n++) {
		bp = TLINE_HIST(n);
		lastpos = MIN(tlinehistlen(n) + 1, term.col) - 1;
		if (lastpos < 0)
			break;
        if (lastpos == 0)
            continue;
		end = &bp[lastpos + 1];
		for (; bp < end; ++bp)
			if (xwrite(to[1], buf, utf8_encode(bp->rune, buf)) < 0)
				break;
		if ((newline = TLINE_HIST(n)[lastpos].mode & ATTR_WRAP))
			continue;
		if (xwrite(to[1], "\n", 1) < 0)
			break;
		newline = 0;
	}
	if (newline)
		(void)xwrite(to[1], "\n", 1);
	close(to[1]);
	/* restore */
	signal(SIGPIPE, oldsigpipe);
}

void
string_dump(void)
{
	uint c;

	fprintf(stderr, "ESC%c", strescseq.type);
	for (size_t i = 0; i < strescseq.len; i++) {
		c = strescseq.buf[i] & 0xff;
		if (c == '\0') {
			putc('\n', stderr);
			return;
		} else if (isprint(c)) {
			putc(c, stderr);
		} else if (c == '\n') {
			fprintf(stderr, "(\\n)");
		} else if (c == '\r') {
			fprintf(stderr, "(\\r)");
		} else if (c == 0x1b) {
			fprintf(stderr, "(\\e)");
		} else {
			fprintf(stderr, "(%02x)", c);
		}
	}
	fprintf(stderr, "ESC\\\n");
}

void
string_reset(void)
{
	strescseq = (STREscape){
		.buf = xrealloc(strescseq.buf, STR_BUF_SIZ),
		.siz = STR_BUF_SIZ,
	};
}

void
user_send_break(const Arg *arg)
{
	if (tcsendbreak(cmdfd, 0))
		perror("Error sending break");
}

void
term_printer(char *s, size_t len)
{
	if (iofd != -1 && xwrite(iofd, s, len) < 0) {
		perror("Error writing to output file");
		close(iofd);
		iofd = -1;
	}
}

void
user_toggle_printer(const Arg *arg)
{
	term.mode ^= TERM_MODE_PRINT;
}

void
user_print_screen(const Arg *arg)
{
	term_dump();
}

void
user_print_sel(const Arg *arg)
{
	term_dump_sel();
}

void
term_dump_sel(void)
{
	char *ptr;

	if ((ptr = get_sel())) {
		term_printer(ptr, strlen(ptr));
		free(ptr);
	}
}

void
term_dump_line(int n)
{
    char str[(term.col + 1) * UTF_SIZ];
    term_printer(str, tgetline(str, &term.line[n][0]));
}

void
term_dump(void)
{
	for (int i = 0; i < term.row; ++i)
		term_dump_line(i);
}

void
term_put_tab(int n)
{
	uint x = term.cursor.x;

	if (n > 0) {
		while (x < term.col && n--)
			for (++x; x < term.col && !term.tabs[x]; ++x)
				/* nothing */ ;
	} else if (n < 0) {
		while (x > 0 && n++)
			for (--x; x > 0 && !term.tabs[x]; --x)
				/* nothing */ ;
	}
	term.cursor.x = LIMIT(x, 0, term.col-1);
}

void
term_def_utf8(char ascii)
{
	if (ascii == 'G')
		term.mode |= TERM_MODE_UTF8;
	else if (ascii == '@')
		term.mode &= ~TERM_MODE_UTF8;
}

void
term_def_tran(char ascii)
{
	static char cs[] = "0B";
	static int vcs[] = {CS_GRAPHIC0, CS_USA};
	char *p;

	if ((p = strchr(cs, ascii)) == NULL) {
		fprintf(stderr, "esc unhandled charset: ESC ( %c\n", ascii);
	} else {
		term.trantbl[term.icharset] = vcs[p - cs];
	}
}

void
term_dec_test(char c)
{
	int x, y;

	if (c == '8') { /* DEC screen alignment test. */
		for (x = 0; x < term.col; ++x) {
			for (y = 0; y < term.row; ++y)
				term_set_char('E', &term.cursor.attr, x, y);
		}
	}
}

void
term_str_sequence(uchar c)
{
	switch (c) {
	case 0x90:   /* DCS -- Device Control String */
		c = 'P';
		break;
	case 0x9f:   /* APC -- Application Program Command */
		c = '_';
		break;
	case 0x9e:   /* PM -- Privacy Message */
		c = '^';
		break;
	case 0x9d:   /* OSC -- Operating System Command */
		c = ']';
		break;
	}
	string_reset();
	strescseq.type = c;
	term.esc |= ESC_STR;
}

void
term_control_code(uchar ascii)
{
	switch (ascii) {
	case '\t':   /* HT */
		term_put_tab(1);
		return;
	case '\b':   /* BS */
		term_move_to(term.cursor.x-1, term.cursor.y);
		return;
	case '\r':   /* CR */
		term_move_to(0, term.cursor.y);
		return;
	case '\f':   /* LF */
	case '\v':   /* VT */
	case '\n':   /* LF */
		/* go to first col if the mode is set */
		term_new_line(TERM_MODE_IS_SET(TERM_MODE_CRLF));
		return;
	case '\a':   /* BEL */
		if (term.esc & ESC_STR_END) {
			/* backwards compatibility to xterm */
			string_handle();
		} else {
			x_bell();
		}
		break;
	case '\033': /* ESC */
		control_seq_intro_reset();
		term.esc &= ~(ESC_CSI|ESC_ALTCHARSET|ESC_TEST);
		term.esc |= ESC_START;
		return;
	case '\016': /* SO (LS1 -- Locking shift 1) */
	case '\017': /* SI (LS0 -- Locking shift 0) */
		term.charset = 1 - (ascii - '\016');
		return;
	case '\032': /* SUB */
		term_set_char('?', &term.cursor.attr, term.cursor.x, term.cursor.y);
		/* FALLTHROUGH */
	case '\030': /* CAN */
		control_seq_intro_reset();
		break;
	case '\005': /* ENQ (IGNORED) */
	case '\000': /* NUL (IGNORED) */
	case '\021': /* XON (IGNORED) */
	case '\023': /* XOFF (IGNORED) */
	case 0177:   /* DEL (IGNORED) */
		return;
	case 0x80:   /* TODO: PAD */
	case 0x81:   /* TODO: HOP */
	case 0x82:   /* TODO: BPH */
	case 0x83:   /* TODO: NBH */
	case 0x84:   /* TODO: IND */
		break;
	case 0x85:   /* NEL -- Next line */
		term_new_line(1); /* always go to first col */
		break;
	case 0x86:   /* TODO: SSA */
	case 0x87:   /* TODO: ESA */
		break;
	case 0x88:   /* HTS -- Horizontal tab stop */
		term.tabs[term.cursor.x] = 1;
		break;
	case 0x89:   /* TODO: HTJ */
	case 0x8a:   /* TODO: VTS */
	case 0x8b:   /* TODO: PLD */
	case 0x8c:   /* TODO: PLU */
	case 0x8d:   /* TODO: RI */
	case 0x8e:   /* TODO: SS2 */
	case 0x8f:   /* TODO: SS3 */
	case 0x91:   /* TODO: PU1 */
	case 0x92:   /* TODO: PU2 */
	case 0x93:   /* TODO: STS */
	case 0x94:   /* TODO: CCH */
	case 0x95:   /* TODO: MW */
	case 0x96:   /* TODO: SPA */
	case 0x97:   /* TODO: EPA */
	case 0x98:   /* TODO: SOS */
	case 0x99:   /* TODO: SGCI */
		break;
	case 0x9a:   /* DECID -- Identify Terminal */
		tty_write(vtiden, strlen(vtiden), 0);
		break;
	case 0x9b:   /* TODO: CSI */
	case 0x9c:   /* TODO: ST */
		break;
	case 0x90:   /* DCS -- Device Control String */
	case 0x9d:   /* OSC -- Operating System Command */
	case 0x9e:   /* PM -- Privacy Message */
	case 0x9f:   /* APC -- Application Program Command */
		term_str_sequence(ascii);
		return;
	}
	/* only CAN, SUB, \a and C1 chars interrupt a sequence */
	term.esc &= ~(ESC_STR_END|ESC_STR);
}

/*
 * returns 1 when the sequence is finished and it hasn't to read
 * more characters for this sequence, otherwise 0
 */
int
eschandle(uchar ascii)
{
	switch (ascii) {
	case '[':
		term.esc |= ESC_CSI;
		return 0;
	case '#':
		term.esc |= ESC_TEST;
		return 0;
	case '%':
		term.esc |= ESC_UTF8;
		return 0;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
	case ']': /* OSC -- Operating System Command */
	case 'k': /* old title set compatibility */
		term_str_sequence(ascii);
		return 0;
	case 'n': /* LS2 -- Locking shift 2 */
	case 'o': /* LS3 -- Locking shift 3 */
		term.charset = 2 + (ascii - 'n');
		break;
	case '(': /* GZD4 -- set primary charset G0 */
	case ')': /* G1D4 -- set secondary charset G1 */
	case '*': /* G2D4 -- set tertiary charset G2 */
	case '+': /* G3D4 -- set quaternary charset G3 */
		term.icharset = ascii - '(';
		term.esc |= ESC_ALTCHARSET;
		return 0;
	case 'D': /* IND -- Linefeed */
		if (term.cursor.y == term.bot) {
			term_scroll_up(term.top, term.bot, 1, SCROLL_SAVEHIST);
		} else {
			term_move_to(term.cursor.x, term.cursor.y+1);
		}
		break;
	case 'E': /* NEL -- Next line */
		term_new_line(1); /* always go to first col */
		break;
	case 'H': /* HTS -- Horizontal tab stop */
		term.tabs[term.cursor.x] = 1;
		break;
	case 'M': /* RI -- Reverse index */
		if (term.cursor.y == term.top) {
			term_scroll_down(term.top, 1);
		} else {
			term_move_to(term.cursor.x, term.cursor.y-1);
		}
		break;
	case 'Z': /* DECID -- Identify Terminal */
		tty_write(vtiden, strlen(vtiden), 0);
		break;
	case 'c': /* RIS -- Reset to initial state */
		term_reset();
		reset_title();
		x_load_cols();
		x_set_mode(0, WIN_MODE_HIDE);
		break;
	case '=': /* DECPAM -- Application keypad */
		x_set_mode(1, WIN_MODE_APPKEYPAD);
		break;
	case '>': /* DECPNM -- Normal keypad */
		x_set_mode(0, WIN_MODE_APPKEYPAD);
		break;
	case '7': /* DECSC -- Save Cursor */
		term_cursor(CURSOR_SAVE);
		break;
	case '8': /* DECRC -- Restore Cursor */
		term_cursor(CURSOR_LOAD);
		break;
	case '\\': /* ST -- String Terminator */
		if (term.esc & ESC_STR_END)
			string_handle();
		break;
	default:
		fprintf(stderr, "erresc: unknown sequence ESC 0x%02X '%c'\n",
			(uchar) ascii, isprint(ascii)? ascii:'.');
		break;
	}
	return 1;
}

void
term_putc(Rune u)
{
	char c[UTF_SIZ];
	int control;
	int width, len;
	Glyph *gp;

	control = ISCONTROL(u);
	if (u < 127 || !TERM_MODE_IS_SET(TERM_MODE_UTF8)) {
		c[0] = u;
		width = len = 1;
	} else {
		len = utf8_encode(u, c);
		if (!control && (width = wcwidth(u)) == -1)
			width = 1;
	}

	if (TERM_MODE_IS_SET(TERM_MODE_PRINT))
		term_printer(c, len);

	/*
	 * STR sequence must be checked before anything else
	 * because it uses all following characters until it
	 * receives a ESC, a SUB, a ST or any other C1 control
	 * character.
	 */
	if (term.esc & ESC_STR) {
		if (u == '\a' || u == 030 || u == 032 || u == 033 ||
		   ISCONTROLC1(u)) {
			term.esc &= ~(ESC_START|ESC_STR);
			term.esc |= ESC_STR_END;
			goto check_control_code;
		}

		if (strescseq.len+len >= strescseq.siz) {
			/*
			 * Here is a bug in terminals. If the user never sends
			 * some code to stop the str or esc command, then st
			 * will stop responding. But this is better than
			 * silently failing with unknown characters. At least
			 * then users will report back.
			 *
			 * In the case users ever get fixed, here is the code:
			 */
			/*
			 * term.esc = 0;
			 * string_handle();
			 */
			if (strescseq.siz > (SIZE_MAX - UTF_SIZ) / 2)
				return;
			strescseq.siz *= 2;
			strescseq.buf = xrealloc(strescseq.buf, strescseq.siz);
		}

		memmove(&strescseq.buf[strescseq.len], c, len);
		strescseq.len += len;
		return;
	}

check_control_code:
	/*
	 * Actions of control codes must be performed as soon they arrive
	 * because they can be embedded inside a control sequence, and
	 * they must not cause conflicts with sequences.
	 */
	if (control) {
		/* in UTF-8 mode ignore handling C1 control characters */
		if (TERM_MODE_IS_SET(TERM_MODE_UTF8) && ISCONTROLC1(u))
			return;
		term_control_code(u);
		/*
		 * control codes are not shown ever
		 */
		if (!term.esc)
			term.lastc = 0;
		return;
	} else if (term.esc & ESC_START) {
		if (term.esc & ESC_CSI) {
			csiescseq.buf[csiescseq.len++] = u;
			if (BETWEEN(u, 0x40, 0x7E)
					|| csiescseq.len >= \
					sizeof(csiescseq.buf)-1) {
				term.esc = 0;
				control_seq_intro_parse();
				control_seq_intro_handle();
			}
			return;
		} else if (term.esc & ESC_UTF8) {
			term_def_utf8(u);
		} else if (term.esc & ESC_ALTCHARSET) {
			term_def_tran(u);
		} else if (term.esc & ESC_TEST) {
			term_dec_test(u);
		} else {
			if (!eschandle(u))
				return;
			/* sequence already finished */
		}
		term.esc = 0;
		/*
		 * All characters which form part of a sequence are not
		 * printed
		 */
		return;
	}

    /* selected() takes relative coordinates */
	if (selected(term.cursor.x + term.scr, term.cursor.y + term.scr))
		selection_clear();

	gp = &term.line[term.cursor.y][term.cursor.x];
	if (TERM_MODE_IS_SET(TERM_MODE_WRAP) && (term.cursor.state & CURSOR_WRAPNEXT)) {
		gp->mode |= ATTR_WRAP;
		term_new_line(1);
		gp = &term.line[term.cursor.y][term.cursor.x];
	}

	if (TERM_MODE_IS_SET(TERM_MODE_INSERT) && term.cursor.x+width < term.col) {
		memmove(gp+width, gp, (term.col - term.cursor.x - width) * sizeof(Glyph));
		gp->mode &= ~ATTR_WIDE;
	}

	if (term.cursor.x+width > term.col) {
		if (TERM_MODE_IS_SET(TERM_MODE_WRAP))
			term_new_line(1);
		else
			term_move_to(term.col - width, term.cursor.y);
		gp = &term.line[term.cursor.y][term.cursor.x];
	}

	term_set_char(u, &term.cursor.attr, term.cursor.x, term.cursor.y);
	term.lastc = u;

	if (width == 2) {
		gp->mode |= ATTR_WIDE;
		if (term.cursor.x+1 < term.col) {
			if (gp[1].mode == ATTR_WIDE && term.cursor.x+2 < term.col) {
				gp[2].rune = ' ';
				gp[2].mode &= ~ATTR_WDUMMY;
			}
			gp[1].rune = '\0';
			gp[1].mode = ATTR_WDUMMY;
		}
	}
	if (term.cursor.x+width < term.col) {
		term_move_to(term.cursor.x+width, term.cursor.y);
	} else {
		term.wrapcwidth[TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN)] = width;
		term.cursor.state |= CURSOR_WRAPNEXT;
	}
}

int
term_write(const char *buf, int buflen, int show_ctrl)
{
	int charsize;
	Rune u;
	int n;

	for (n = 0; n < buflen; n += charsize) {
		if (TERM_MODE_IS_SET(TERM_MODE_UTF8)) {
			/* process a complete utf8 char */
			charsize = utf8_decode(buf + n, &u, buflen - n);
			if (charsize == 0)
				break;
		} else {
			u = buf[n] & 0xFF;
			charsize = 1;
		}
		if (show_ctrl && ISCONTROL(u)) {
			if (u & 0x80) {
				u &= 0x7f;
				term_putc('^');
				term_putc('[');
			} else if (u != '\n' && u != '\r' && u != '\t') {
				u ^= 0x40;
				term_putc('^');
			}
		}
		term_putc(u);
	}
	return n;
}

void
rscrolldown(int n)
{
	int j;
    Line temp;

    /* can never be true as of now
       if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN))
       return; */

    if ((n = MIN(n, term.histf)) <= 0)
        return;

    for (int i = term.cursor.y + n; i >= n; i--) {
        temp = term.line[i];
        term.line[i] = term.line[i-n];
        term.line[i-n] = temp;
    }
    for (int i = n - 1; i >= 0; i--) {
        temp = term.line[i];
        term.line[i] = term.hist[term.histi];
        term.hist[term.histi] = temp;
        term.histi = (term.histi - 1 + HISTSIZE) % HISTSIZE;
    }
    term.cursor.y += n;
    term.histf -= n;
    if ((j = term.scr - n) >= 0) {
        term.scr = j;
    } else {
        term.scr = 0;
        if (selection.ob.x != -1 && !selection.alt)
            selmove(-j);
    }
}



void
term_resize(int col, int row)
{
	int *bp;

	/* col and row are always MAX(_, 1)
	if (col < 1 || row < 1) {
		fprintf(stderr, "term_resize: error resizing to %dx%d\n", col, row);
		return;
	} */

	term.dirty = xrealloc(term.dirty, row * sizeof(*term.dirty));
	term.tabs = xrealloc(term.tabs, col * sizeof(*term.tabs));
 	if (col > term.col) {
 		bp = term.tabs + term.col;
 		memset(bp, 0, sizeof(*term.tabs) * (col - term.col));
 		while (--bp > term.tabs && !*bp)
 			/* nothing */ ;
 		for (bp += tabspaces; bp < term.tabs + col; bp += tabspaces)
 			*bp = 1;
 	}

	if (TERM_MODE_IS_SET(TERM_MODE_ALTSCREEN))
		tresizealt(col, row);
	else
		tresizedef(col, row);
}


void
tresizedef(int col, int row)
{
	/* return if dimensions haven't changed */
	if (term.col == col && term.row == row) {
		term_full_dirt();
		return;
	}
	if (col != term.col) {
		if (!selection.alt)
			selremove();
		treflow(col, row);
	} else {
		/* slide screen up if otherwise cursor would get out of the screen */
		if (term.cursor.y >= row) {
			term_scroll_up(0, term.row - 1, term.cursor.y - row + 1, SCROLL_RESIZE);
			term.cursor.y = row - 1;
 		}
		for (int i = row; i < term.row; i++)
			free(term.line[i]);

		/* handler_configure_notify to new height */
		term.line = xrealloc(term.line, row * sizeof(Line));
		/* allocate any new number_rows */
		for (int i = term.row; i < row; i++) {
			term.line[i] = xmalloc(col * sizeof(Glyph));
			for (int j = 0; j < col; j++)
				tclearglyph(&term.line[i][j], 0);
 		}
		/* scroll down as much as height has increased */
		rscrolldown(row - term.row);
	}
	/* update terminal size */
	term.col = col, term.row = row;
	/* reset scrolling region */
	term.top = 0, term.bot = row - 1;
	/* dirty all lines */
	term_full_dirt();
}



void
tresizealt(int col, int row)
{
    int i;

    /* return if dimensions haven't changed */
    if (term.col == col && term.row == row) {
        term_full_dirt();
        return;
    }
    if (selection.alt)
        selremove();
    /* slide screen up if otherwise cursor would get out of the screen */
    for (i = 0; i <= term.cursor.y - row; i++)
        free(term.line[i]);
    if (i > 0) {
        /* ensure that both src and dst are not NULL */
        memmove(term.line, term.line + i, row * sizeof(Line));
        term.cursor.y = row - 1;
    }
    for (i += row; i < term.row; i++)
        free(term.line[i]);
    /* handler_configure_notify to new height */
    term.line = xrealloc(term.line, row * sizeof(Line));
    /* handler_configure_notify to new width */
    for (i = 0; i < MIN(row, term.row); i++) {
        term.line[i] = xrealloc(term.line[i], col * sizeof(Glyph));
        for (int j = term.col; j < col; j++)
            tclearglyph(&term.line[i][j], 0);
    }
    /* allocate any new number_rows */
    for (/*i = MIN(row, term.row) */; i < row; i++) {
        term.line[i] = xmalloc(col * sizeof(Glyph));
        for (int j = 0; j < col; j++)
            tclearglyph(&term.line[i][j], 0);
    }
    /* update cursor */
    if (term.cursor.x >= col) {
        term.cursor.state &= ~CURSOR_WRAPNEXT;
        term.cursor.x = col - 1;
    } else {
        UPDATEWRAPNEXT(1, col);
    }
    /* update terminal size */
    term.col = col, term.row = row;
    /* reset scrolling region */
    term.top = 0, term.bot = row - 1;
    /* dirty all lines */
    term_full_dirt();
 }





void
treflow(int col, int row)
{
    int i;
    int oce, nce, bot, scr;
    int ox = 0, oy = -term.histf, nx = 0, ny = -1, len;
    int cy = -1; /* proxy for new y coordinate of cursor */
    int nlines;
    Line *buf, line;

    /* y coordinate of cursor line end */
    for (oce = term.cursor.y; oce < term.row - 1 &&
            tiswrapped(term.line[oce]); oce++);

    nlines = term.histf + oce + 1;
    if (col < term.col) {
        /* each line can take this many lines after reflow */
        int j = (term.col + col - 1) / col;
        nlines = j * nlines;
        if (nlines > HISTSIZE + RESIZEBUFFER + row) {
            nlines = HISTSIZE + RESIZEBUFFER + row;
            oy = -(nlines / j - oce - 1);
        }
    }
    buf = xmalloc(nlines * sizeof(Line));
    do {
        if (!nx)
            buf[++ny] = xmalloc(col * sizeof(Glyph));
        if (!ox) {
            line = TLINEABS(oy);
            len = term_line_len(line);
        }
        if (oy == term.cursor.y) {
            if (!ox)
                len = MAX(len, term.cursor.x + 1);
            /* update cursor */
            if (cy < 0 && term.cursor.x - ox < col - nx) {
                term.cursor.x = nx + term.cursor.x - ox, cy = ny;
                UPDATEWRAPNEXT(0, col);
            }
        }
        /* get reflowed lines in buf */
        if (col - nx > len - ox) {
            memcpy(&buf[ny][nx], &line[ox], (len-ox) * sizeof(Glyph));
            nx += len - ox;
            if (len == 0 || !(line[len - 1].mode & ATTR_WRAP)) {
                for (int j = nx; j < col; j++)
                    tclearglyph(&buf[ny][j], 0);
                nx = 0;
            } else if (nx > 0) {
                buf[ny][nx - 1].mode &= ~ATTR_WRAP;
            }
            ox = 0, oy++;
        } else if (col - nx == len - ox) {
            memcpy(&buf[ny][nx], &line[ox], (col-nx) * sizeof(Glyph));
            ox = 0, oy++, nx = 0;
        } else/* if (col - nx < len - ox) */ {
            memcpy(&buf[ny][nx], &line[ox], (col-nx) * sizeof(Glyph));
            ox += col - nx;
            buf[ny][col - 1].mode |= ATTR_WRAP;
            nx = 0;
        }
    } while (oy <= oce);
    if (nx)
        for (int j = nx; j < col; j++)
            tclearglyph(&buf[ny][j], 0);

    /* free extra lines */
    for (i = row; i < term.row; i++)
        free(term.line[i]);
    /* handler_configure_notify to new height */
    term.line = xrealloc(term.line, row * sizeof(Line));

    bot = MIN(ny, row - 1);
    scr = MAX(row - term.row, 0);
    /* update y coordinate of cursor line end */
    nce = MIN(oce + scr, bot);
    /* update cursor y coordinate */
    term.cursor.y = nce - (ny - cy);
    if (term.cursor.y < 0) {
        int j = nce;
		nce = MIN(nce + -term.cursor.y, bot);
        term.cursor.y += nce - j;
        while (term.cursor.y < 0) {
            free(buf[ny--]);
            term.cursor.y++;
        }
    }
    /* allocate new number_rows */
    for (i = row - 1; i > nce; i--) {
        term.line[i] = xmalloc(col * sizeof(Glyph));
        for (int j = 0; j < col; j++)
            tclearglyph(&term.line[i][j], 0);
    }
    /* fill visible area */
    for (/*i = nce */; i >= term.row; i--, ny--)
        term.line[i] = buf[ny];
    for (/*i = term.row - 1 */; i >= 0; i--, ny--) {
        free(term.line[i]);
        term.line[i] = buf[ny];
    }
    /* fill lines in history buffer and update term.histf */
    for (/*i = -1 */; ny >= 0 && i >= -HISTSIZE; i--, ny--) {
        int j = (term.histi + i + 1 + HISTSIZE) % HISTSIZE;
        free(term.hist[j]);
        term.hist[j] = buf[ny];
    }
    term.histf = -i - 1;
    term.scr = MIN(term.scr, term.histf);
    /* handler_configure_notify rest of the history lines */
    for (/*i = -term.histf - 1 */; i >= -HISTSIZE; i--) {
        int j = (term.histi + i + 1 + HISTSIZE) % HISTSIZE;
        term.hist[j] = xrealloc(term.hist[j], col * sizeof(Glyph));
    }
    free(buf);
}

void
reset_title(void)
{
	x_set_title(NULL);
}

void
draw_region(int x1, int y1, int x2, int y2)
{
	int y;

	for (y = y1; y < y2; y++) {
		if (!term.dirty[y])
			continue;

		term.dirty[y] = 0;
		x_draw_line(TLINE(y), x1, y, x2);
	}
}

void
draw(void)
{
	int cx = term.cursor.x, ocx = term.ocx, ocy = term.ocy;

	if (!x_start_draw())
		return;

	/* adjust cursor position */
	LIMIT(term.ocx, 0, term.col-1);
	LIMIT(term.ocy, 0, term.row-1);
	if (term.line[term.ocy][term.ocx].mode & ATTR_WDUMMY)
		term.ocx--;
	if (term.line[term.cursor.y][cx].mode & ATTR_WDUMMY)
		cx--;

	draw_region(0, 0, term.col, term.row);
	x_draw_cursor(cx, term.cursor.y, term.line[term.cursor.y][cx],
			term.ocx, term.ocy, term.line[term.ocy][term.ocx]);
	term.ocx = cx;
	term.ocy = term.cursor.y;
	x_finish_draw();
	if (ocx != term.ocx || ocy != term.ocy)
		x_xim_spot(term.ocx, term.ocy);
}

void
redraw(void)
{
	term_full_dirt();
	draw();
}

#endif /* ST_C */
