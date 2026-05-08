import matplotlib
matplotlib.use("module://pyplotsixel")
import matplotlib.pyplot as plt
import numpy as np

plt.figure(figsize=(6,4), facecolor="white")

ax_left = plt.axes()
ax_left.set_facecolor("white")

ax_left.tick_params(colors="black")
for spine in ax_left.spines.values():
    spine.set_edgecolor("black")

ax_right = ax_left.twinx()

ax_right.tick_params(colors="black")
for spine in ax_right.spines.values():
    spine.set_edgecolor("black")

x = np.linspace(0, 10, 100)
y1 = np.sin(x)
y2 = np.exp(x)

ax_left.plot(x, y1, linewidth=2, color="tab:blue", label="sin(x)")
ax_right.plot(x, y2, linewidth=2, color="tab:orange", label="exp(x)")

ax_left.legend(loc="lower left", bbox_to_anchor=(0, 1),
               frameon=False, labelcolor="black")
ax_right.legend(loc="lower right", bbox_to_anchor=(1, 1),
                frameon=False, labelcolor="black")

plt.title("Functions", color="black", pad=25)

plt.show()
