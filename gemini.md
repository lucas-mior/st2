# Preview textual output does not get truncated after sixel

When a previewer script outputs sixel graphics followed by long lines of text, lf stops manually truncating the text to the pane width. On some terminals (e.g., st (with or without sixel patch), contour), this causes the text to wrap and overflow into the left panes. On others (e.g., alacritty), the terminal seems to fall back to DECAWM (Auto-wrap) being disabled, which results in "overwriting the last column with the last characters of the line" truncation rather than lf's standard "clip at margin" truncation.

A minimal previewer example that shows this behavior:

```bash
#!/bin/bash

# previewer to trigger the bug.
# usage: in lfrc, set previewer <this script>

chafa --polite on --format=sixel --animate=false /tmp/image.png \
    | tee sixel_output.data
# also triggered by other sixel tools:
# cat ~/sixel_output.data
# magick ~/image.jpg sixel:-

# this text should truncate at the end of the pane,
# but it wraps and overflows to the next lines instead:
# down and to the left of the preview pane.
printf 'A%.0s' $(seq 1 80)
printf 'B%.0s' $(seq 1 80)
printf '\n'
# note that the first 'A's are in the correct column below the sixel image
```

## Minimal sixel escape sequence
In order to make it easier to reproduce the bug, I have created an minimal 4x4 uncompressed png of only gray pixels with the command below.

```sh
magick -size 4x4 xc:"#ababab" \
  -define png:compression-level=0 \
  -define png:compression-filter=0 \
  -strip \
  /tmp/image.png
```

then I run 

```sh
chafa --polite on --format=sixel --animate=false /tmp/image.png \
| tee sixel_output.data,
```

The exact sixel data that chafa sends to lf/terminal:

```
cat -v sixel_output.data
^[P0;1;0q"1;1;11;21#0;2;67;67;67#1;2;93;93;93#0!4N!7?---#0!11?^[\
```

## Why is this a problem
The DECAWM solution is not bad, but I am worried that sixel is making lf get to an inconsistent terminal state, which might cause other hidden bugs.
