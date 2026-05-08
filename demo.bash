#!/usr/bin/env bash

# Set up colors for the prompt
CYAN='\033[1;36m'
NC='\033[0m' # No Color

# Function to echo and execute commands with a pause
run_cmd() {
    echo -e "${CYAN}$ $@${NC}"
    sleep 0.5
    eval "$@"
    echo ""
    read -p "Press [Enter] to continue to the next feature..."
    echo ""
}

echo "============================================="
echo "        st Terminal Feature Demo             "
echo "============================================="
echo ""

# 1. Sixel Images
echo "--> 1. Displaying a Sixel Image"
run_cmd 'chafa --format sixel st.png'

echo "--> 2. Generating and displaying a Matplotlib plot"
cp pyplotsixel.py /tmp/
run_cmd 'cat << EOF > /tmp/demo_plot.py
import matplotlib
matplotlib.use("module://pyplotsixel")
import matplotlib.pyplot as plt
import numpy as np

# Set background to transparent to showcase terminal alpha
plt.figure(facecolor="none", edgecolor="none")
ax = plt.axes()
ax.set_facecolor("none")

x = np.linspace(0, 10, 100)
y = np.sin(x)
plt.plot(x, y, color="cyan", linewidth=2)
plt.title("Sine Wave in Terminal", color="white")
plt.tick_params(colors="white")

plt.show()
EOF
python3 /tmp/demo_plot.py'

echo "--> 3. Showing git diff"
run_cmd 'mkdir -p /tmp/st_demo_repo && cd /tmp/st_demo_repo && git init -q;
echo -e "Line 1\nLine 2\nLine 3" > file.txt && git add file.txt && git commit -q -m Initial;
echo -e "Line 1 (Modified)\nLine 2\nLine 3 (Modified)" > file.txt;
git diff --color=always;'

echo "--> 4. ncmpcpp Visualizer (requires mpd running locally)"
echo "Note: The terminal's transparent background should seamlessly blend with your wallpaper here."
echo "Press 'q' inside ncmpcpp to exit."
run_cmd "mpc play; ncmpcpp -s visualizer"

# 5. user_vim_select (Scrollback Buffer in Vim)
echo "--> 5. Vim Scrollback (user_vim_select)"
echo "Populating the screen with some text..."
ls -la
echo -e "${CYAN}Press Alt+Escape to trigger user_vim_select...${NC}"
read -r
# echo ""
# echo "A new st window running vim should have popped up containing your terminal buffer!"
# echo "You can close that vim instance (:q) when you are done."
# echo ""

# Cleanup
echo "Cleaning up temporary files..."
rm -rf /tmp/demo_img.png /tmp/demo_plot.py /tmp/demo_plot.png /tmp/st_demo_repo

echo "Demo complete!"
