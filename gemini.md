# My problem

consider lf file manager. The previewer executes a program, and the programs
output must be contained in the preview pane (which have less columns than the
entire terminal). Also, long lines must be truncated (no wrap).

this works fine on my terminal, until I display an image using
chafa --format=sixel
Long lines of the previewer textual output start wrapping, overflowing the
preview pane. This happens with other sixel tools (like image magick) but not on
other terminals, so I know it must be a bug within my terminal. 

# About TERM_MODE_WRAP / DECAWM
The bug is *not* caused by TERM_MODE_WRAP being on, because if sixel is not
invoked, lines on he preview pane are truncated just fine, even with
TERM_MODE_WRAP being on.

disabling TERM_MODE_WRAP is only for a very specific type of truncation, in
which the last column is overwritten by the last char in the long line. LF
preview pane truncation is completely different: it simply truncates (the last
column is whatever the last char that there was space to print). For instance,
consider a pane of width 4 and the long line "AAAAABBBBB"

In lf preview pane, the line would should "AAAA". In the TERM_MODE_WRAP disabled
mode, the line would should "AAAB".

So, sixel is causing some terminal state to change. In particular,
some terminal setup made by lf for the preview gets unset by the sixel.

A minimal previewer example that shows this behavior:

```sh
#!/bin/sh

chafa --polite on --format=sixel --animate=false ~/0image.png
# or magick ~/image.jpg sixel:-

cat ~/0text.txt
```

Note: doing printf "\033[?7l" before the cat command and printf "\033[?7h"
after the cat command works as imperfect workaround, because after "\033[?7l"
additional characters overwrite the last column instead of wrapping.  # which is
not the default preview pane behavior: long lines are simply truncated, the last
char is not considered special. This is the same as enabling TERM_MODE_WRAP
(DECAWM) and disabling it later.

# Minimal sixel escape sequence
In order to make it easier to reproduce the bug,
I have created an minimal 4x4 uncompressed png of only gray pixels with the
command

magick -size 4x4 xc:"#ababab" \
  -define png:compression-level=0 \
  -define png:compression-filter=0 \
  -strip \
  output.png

See how the file is encoded below:

```
$ xxd output.png
00000000: 8950 4e47 0d0a 1a0a 0000 000d 4948 4452  .PNG........IHDR
00000010: 0000 0004 0000 0004 0800 0000 008c 9ac1  ................
00000020: a200 0000 1f49 4441 5408 1d01 1400 ebff  .....IDAT.......
00000030: 01ab 0000 0002 0000 0000 0200 0000 0002  ................
00000040: 0000 0000 0d15 00b3 c333 9d66 0000 0000  .........3.f....
00000050: 4945 4e44 ae42 6082                      IEND.B`.
```

then I run 

chafa --polite on --format=sixel --animate=false ~/0image.png \
| tee chafa_output2.bin,

Now I can give you the exact sixel data that chafa sends to lf/terminal:

```
cat -v chafa_output2.bin
^[P0;1;0q"1;1;11;21#0;2;67;67;67#1;2;93;93;93#0!4N!7?---#0!11?^[\
```

Write me an explanation of what this sequence of bytes does and wait for
further instructions.
