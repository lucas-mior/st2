#!/bin/bash

# previewer to trigger the bug.
# usage: in lfrc, set previewer <this script>

chafa --polite on --format=sixel --animate=false ~/0image.png \
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
