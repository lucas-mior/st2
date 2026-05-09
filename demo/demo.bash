#!/usr/bin/env bash

# Set up colors for the prompt
CYAN='\033[1;36m'
NC='\033[0m' # No Color

# Function to echo and execute commands with a pause
run_cmd() {
    echo -e "${CYAN}$ $@${NC}"
    sleep 0.8
    eval "$@"
    echo ""
    read -p "Press [Enter] to continue to the next feature..."
    echo ""
}

echo "============================================="
echo "        st Terminal Feature Demo             "
echo "============================================="
echo ""

echo "--> 1. ncmpcpp Visualizer (requires mpd running locally)"
echo "Note: The terminal's transparent background should seamlessly blend with your wallpaper here."
echo "Press 'q' inside ncmpcpp to exit."
run_cmd "mpc play; ncmpcpp -s visualizer"

echo "--> 2. Displaying a Sixel Image"
run_cmd 'chafa --format sixel st.png'

echo "--> 3. Generating and displaying Matplotlib plots"
python_plot_sixel() {
    bat "$1"
    run_cmd "python3 $1"
}
python_plot_sixel ./plot-white.py
python_plot_sixel ./plot-trans.py
python_plot_sixel ./plot-semi.py

echo "--> 4. Showing git diff"
run_cmd 'mkdir -p /tmp/st_demo_repo && cd /tmp/st_demo_repo && git init -q;
echo -e "Line 0 has to be removed.\nLine 1\nLine 2 has a removed word\nLine 3" > file.txt && git add file.txt && git commit -q -m Initial;
echo -e "Line 1 (Modified)\nLine 2 has a word\nLine 3 (Modified)\nLine 4 (new)" > file.txt;
git diff --color=always;'

echo "--> 5. Vim Scrollback (user_vim_select)"
echo "Populating the screen with some text..."
ls -la
echo -e "${CYAN}Press Alt+Escape to trigger user_vim_select...${NC}"
read -r

echo "Cleaning up temporary files..."
rm -rf /tmp/demo_img.png /tmp/demo_plot.py /tmp/demo_plot.png /tmp/st_demo_repo

echo "Demo complete!"
