import matplotlib
matplotlib.use("module://pyplotsixel")
import matplotlib.pyplot as plt
import numpy as np

plt.figure(figsize=(6,4), facecolor="white")

ax = plt.axes()
ax.set_facecolor("white")

ax.tick_params(colors="black")
for spine in ax.spines.values():
    spine.set_edgecolor("black")

x = np.linspace(0, 10, 100)
y = np.sin(x)

plt.plot(x, y, linewidth=2)
plt.title("Sine Wave in Terminal", color="white")

plt.show()
