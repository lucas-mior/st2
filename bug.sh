#!/bin/sh

# --- CONFIGURATION ---
COLS=$(tput cols)
PANE_X=40
# A string long enough to wrap if the terminal 'forgets' its position
LONG_STR="ALT_SCREEN_TEST_####################################################################################################"

# 1. ENTER ALT SCREEN
# \033[?1049h is the standard sequence for 'smcup' (enter alt screen)
printf "\033[?1049h"

# 2. CLEAR AND RESET
printf "\033[H\033[2J"
printf "\033[?7h" # Ensure Wrap is ON

# 3. DRAW A BOUNDARY
# This helps visualize if the text is staying inside the 'pane'
for i in $(seq 1 20); do
    printf "\033[%d;${PANE_X}H|"
done

# 4. OUTPUT SIXEL
# We place the image so its right edge touches or exceeds the terminal width
printf "\033[5;${PANE_X}H"
chafa --polite on --format=sixel --animate=false --size="$((COLS - PANE_X))x10" "$1"

# 5. THE LF-STYLE MOVE
# Move to the line after the image at the same X-offset.
printf "\033[16;${PANE_X}H"

# 6. THE TEST
# If the bug exists, this text will start on line 17 or be indented,
# causing it to overflow the right side of the terminal.
printf "AFTER_SIXEL: %s" "$LONG_STR"

# 7. WAIT FOR USER
printf "\033[22;1H\033[7m TEST COMPLETE: Press any key to exit Alt Screen \033[0m"
read -r dummy

# 8. EXIT ALT SCREEN
printf "\033[?1049l"
