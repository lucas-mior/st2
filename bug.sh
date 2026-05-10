#!/bin/sh

# Simulating an lf preview pane (e.g., Column 50 to 80)
PANE_X=50
PANE_Y=5
PANE_WIDTH=30

# A "normal" line that is shorter than the pane width
# We don't truncate this in the script because we want to see if the
# terminal incorrectly wraps it.
TEXT_LINE="AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHHIIIIJJJJKKKKLLLLMMMM"

# 1. Clear screen
printf "\033[H\033[2J"

# 2. Draw a visual boundary for the "Pane"
for i in $(seq 1 20); do
    printf "\033[%d;${PANE_X}H|" 
    printf "\033[%d;$((PANE_X + PANE_WIDTH))H|"
done

# 3. Simulate lf's Sixel Output
# We position the cursor inside the pane, then fire chafa.
# The key here is the width: we tell chafa to fill the pane.
printf "\033[${PANE_Y};${PANE_X}H"
chafa --polite on --format=sixel --animate=false --size="${PANE_WIDTH}x10" "$1"

# 4. Simulate tcell's drawing behavior
# tcell uses absolute positioning (CUP) to draw lines.
# We move the cursor to the line immediately after the image.
# If your terminal bug exists, the cursor will 'remember' the wrap state 
# from the Sixel image and force the next line to the next row or indent it.
printf "\033[$((PANE_Y + 11));${PANE_X}H"
printf "Line 1: %s" "$TEXT_LINE"

printf "\033[$((PANE_Y + 12));${PANE_X}H"
printf "Line 2: %s" "$TEXT_LINE"

# Reset cursor to bottom
printf "\033[25;1H"
