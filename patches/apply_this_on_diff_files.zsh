#!/bin/zsh

alias rnc='rename_commit.sh'

set -x

rnc base64dec base64_decode
rnc base64dec_getc base64_decode_getc
rnc bmotion handler_button_motion
rnc borderpx border_pixels
rnc bpress handler_button_press
rnc brelease handler_button_release
rnc buttonmask button_mask
rnc chscale char_height_scale
rnc clipboard_copy user_clipboard_copy
rnc clipboard_paste user_clipboard_paste
rnc clipcopy clipboard_copy
rnc clippaste clipboard_paste
rnc cmessage handler_client_message
rnc cols number_cols
rnc csidump control_seq_intro_dump
rnc csihandle control_seq_intro_handle
rnc csiparse control_seq_intro_parse
rnc csireset control_seq_intro_reset
rnc cursorshape cursor_shape
rnc cwscale char_width_scale
rnc dc draw_context
rnc DC DrawingContext
rnc defaultattr default_attr
rnc defaultbg default_background
rnc defaultcs default_cursor
rnc defaultfg default_foreground
rnc defaultrcs default_reverse_cursor
rnc drawregion draw_region
rnc expose handler_expose
rnc focus handler_focus
rnc forcemousemod force_mouse_mod
rnc gc graphics
rnc getsel get_sel
rnc kpress handler_key_press
rnc kscrolldown user_scroll_down
rnc kscrollup user_scroll_up
rnc MODE_8BIT        WIN_MODE_8BIT
rnc MODE_ALTSCREEN TERM_MODE_ALTSCREEN
rnc MODE_APPCURSOR   WIN_MODE_APPCURSOR
rnc MODE_APPKEYPAD   WIN_MODE_APPKEYPAD
rnc MODE_BLINK       WIN_MODE_BLINK
rnc MODE_BRCKTPASTE  WIN_MODE_BRCKTPASTE
rnc MODE_CRLF TERM_MODE_CRLF
rnc MODE_ECHO TERM_MODE_ECHO
rnc MODE_FBLINK      WIN_MODE_FBLINK
rnc MODE_FOCUSED     WIN_MODE_FOCUSED
rnc MODE_FOCUS       WIN_MODE_FOCUS
rnc MODE_HIDE        WIN_MODE_HIDE
rnc MODE_INSERT TERM_MODE_INSERT
rnc MODE_KBDLOCK     WIN_MODE_KBDLOCK
rnc MODE_MOUSEBTN    WIN_MODE_MOUSEBTN
rnc MODE_MOUSEMANY   WIN_MODE_MOUSEMANY
rnc MODE_MOUSEMOTION WIN_MODE_MOUSEMOTION
rnc MODE_MOUSESGR    WIN_MODE_MOUSESGR
rnc MODE_MOUSE WIN_MODE_MOUSE
rnc MODE_MOUSEX10    WIN_MODE_MOUSEX10
rnc MODE_NUMLOCK     WIN_MODE_NUMLOCK
rnc MODE_PRINT TERM_MODE_PRINT
rnc MODE_REVERSE     WIN_MODE_REVERSE
rnc MODE_UTF8 TERM_MODE_UTF8
rnc MODE_VISIBLE     WIN_MODE_VISIBLE
rnc MODE_WRAP TERM_MODE_WRAP
rnc mouseaction mouse_action
rnc mousebg mouse_background
rnc mousefg mouse_foreground
rnc mouseshape mouse_shape
rnc numlock toggle_numlock
rnc printscreen user_print_screen
rnc printsel user_print_sel
rnc propnotify handler_prop_notify
rnc resettitle reset_title
rnc resize handler_configure_notify
rnc rows number_rows
rnc selclear_ handler_sel_clear
rnc selclear sel_clear
rnc SEL_EMPTY SELECTION_EMPTY
rnc selextend sel_extend
rnc SEL_IDLE SELECTION_IDLE
rnc selinit sel_init
rnc selnormalize sel_normalize
rnc selnotify handler_sel_notify
rnc selpaste sel_paste
rnc sel_paste user_sel_paste
rnc SEL_READY SELECTION_READY
rnc SEL_RECTANGULAR SELECTION_RECTANGULAR
rnc SEL_REGULAR SELECTION_REGULAR
rnc selrequest handler_sel_request
rnc selscroll sel_scroll
rnc sel selection
rnc selsnap sel_snap
rnc selstart sel_start
rnc sendbreak user_send_break
rnc sigchld handler_sigchld
rnc SNAP_LINE SELECTION_SNAP_LINE
rnc SNAP_WORD SELECTION_SNAP_WORD
rnc strdump string_dump
rnc strhandle string_handle
rnc strparse string_parse
rnc strreset string_reset
rnc tattrset term_attr_set
rnc tclearregion term_clear_region
rnc tcontrolcode term_control_code
rnc tcursor term_cursor
rnc tdectest term_dec_test
rnc tdefcolor term_def_color
rnc tdeftran term_def_tran
rnc tdefutf8 term_def_utf8
rnc tdeletechar term_delete_char
rnc tdeleteline term_delete_line
rnc tdumpline term_dump_line
rnc tdumpsel term_dump_sel
rnc tdump term_dump
rnc tfulldirt term_full_dirt
rnc th tty_height
rnc tinsertblankline term_insert_blank_line
rnc tinsertblank term_insert_blank
rnc tlinelen term_line_len
rnc tmoveato term_move_abs_to
rnc tmoveto term_move_to
rnc tnewline term_new_line
rnc tnew term_new
rnc toggle_numlock user_toggle_numlock
rnc toggleprinter user_toggle_printer
rnc tprinter term_printer
rnc tputc term_putc
rnc tputtab term_put_tab
rnc treset term_reset
rnc tresize term_resize
rnc tscrolldown term_scroll_down
rnc tscrollup term_scroll_up
rnc tsetattr term_set_attr
rnc tsetchar term_set_char
rnc tsetdirtattr term_set_dirt_attr
rnc tsetdirt term_set_dirt
rnc tsetmode term_set_mode
rnc tsetscroll term_set_scroll
rnc tstrsequence term_str_sequence
rnc tswapscreen term_swap_screen
rnc ttyhangup tty_hangup
rnc ttynew tty_new
rnc ttyread tty_read
rnc ttyresize tty_resize
rnc ttysend tty_send
rnc ttywriteraw tty_write_raw
rnc ttywrite tty_write
rnc twrite term_write
rnc tw tty_width
rnc unmap handler_unmap
rnc utf8decodebyte utf8_decode_byte
rnc utf8decode utf8_decode
rnc utf8encodebyte utf8_encode_byte
rnc utf8encode utf8_encode
rnc utf8validate utf8_validate
rnc utf8validate utf8_validate/g
rnc visibility handler_visibility
rnc xbell x_bell
rnc xclear x_clear
rnc xclipcopy x_clipboard_copy
rnc xdrawcursor x_draw_cursor
rnc xdrawglyphfontspecs x_draw_glyph_font_specs
rnc xdrawglyph x_draw_glyph
rnc xdrawline x_draw_line
rnc xfinishdraw x_finish_draw
rnc xgeommasktogravity x_geom_mask_to_gravity
rnc xgetcolor x_get_color
rnc xhints x_hints
rnc xicdestroy x_ic_destroy
rnc ximdestroy x_im_destroy
rnc ximinstantiante x_im_instantiate
rnc ximinstantiate x_im_instantiate
rnc ximopen x_im_open
rnc xinit x_init
rnc xloadcolor x_load_color
rnc xloadcols x_load_cols
rnc xloadfonts x_load_fonts
rnc xloadfont x_load_font
rnc xmakeglyphfontspecs x_make_glyph_font_specs
rnc xpev x_property_event
rnc xresize x_resize
rnc xsetcolorname x_set_color_name
rnc xsetcursor x_set_cursor
rnc xsetenv x_setenv
rnc xseticontitle x_set_icon_title
rnc xsetmode x_set_mode
rnc xsetpointermotion x_set_pointer_motion
rnc xsetsel x_set_sel
rnc xsettitle x_set_title
rnc xseturgency x_set_urgency
rnc xstartdraw x_start_draw
rnc xunloadfonts x_unload_fonts
rnc xunloadfont x_unload_font
rnc xw x_window
rnc xximspot x_xim_spot
rnc zoomabs zoom_abs
rnc zoom_reset user_zoom_reset
rnc zoomreset zoom_reset
rnc zoom user_zoom

for f in *.diff; do
	sed -Ei 's/(->|\.)u\>/\1rune/g' "$f"
done
git commit -a -m "use long, descriptive names (.u -> .rune)"

for f in *.diff; do
    sed -Ei 's/(_)?sel_/\1selection_/g' "$f"
done
git commit -a -m "use long, descriptive names (sel_ -> selection_)"

for f in *.diff; do
    sed -Ei 's/term\.c\>/term.cursor/g' "$f"
done
vim *.diff
git commit -a -m "use long, descriptive names (term.c -> term.cursor)"
