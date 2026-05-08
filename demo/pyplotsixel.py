# Copyright (c) 2024 Hajime Nakagami
# Released under the BSD license.
# https://github.com/nakagami/pyplotsixel/blob/master/pyplotsixel.py

import sys
import io
import shutil
import matplotlib
import numpy as np
from PIL import Image
from matplotlib.backend_bases import _Backend, FigureManagerBase
from matplotlib.backends.backend_agg import FigureCanvasAgg


def _convert_line(data):
    height, width = np.shape(data)
    colors = list(set(data.flatten()))
    six_list = dict([(color, []) for color in colors])

    six = dict([(color, 0) for color in colors])
    for x in range(width):
        for y in range(height):
            six[data[y, x]] |= 1 << y
        for color in colors:
            six_list[color].append(six[color])
            six[color] = 0

    buf = []
    for color in colors:
        start_and_six = [(0, six_list[color][0])]
        for i, six in enumerate(six_list[color][1:], start=1):
            if start_and_six[-1][1] != six:
                start_and_six.append((i, six))

        node = []
        for i, (start, six) in enumerate(start_and_six[:-1]):
            next_start = start_and_six[i + 1][0]
            node.append((six, next_start - start))
        start, six = start_and_six[-1]
        node.append((six, width - start))

        buf.append((color, node))

    return buf


def output_sixel(image, output):
    width, height = image.size

    # --- 1-BIT ALPHA COMPOSITING ---
    if image.mode == 'RGBA':
        r, g, b, a = image.split()

        # 1. Composite the smooth anti-aliased edges against a solid black background
        bg = Image.new("RGB", image.size, (0, 0, 0))
        bg.paste(image, mask=a)

        # 2. Quantize the blended image to 255 colors (reserving 1 for transparency)
        image_p = bg.quantize(255)

        # 3. Identify truly transparent pixels and assign them to the reserved index (255)
        alpha_data = np.array(a)
        p_data = np.array(image_p)

        transparent_idx = 255
        p_data[alpha_data == 0] = transparent_idx

        # 4. Reconstruct the paletted image
        image = Image.fromarray(p_data, mode='P')
        palette = image_p.getpalette()
        if palette is None:
            palette = []
        # Pad the palette out to 256 colors
        palette.extend([0, 0, 0] * (256 - len(palette) // 3))
        image.putpalette(palette)
    else:
        image = image.quantize(256).convert("P", palette=Image.ADAPTIVE, colors=256)
        transparent_idx = -1

    # header
    output.write(f'\x1bP7;1;75q"1;1;{width};{height}')

    # palette
    palette = np.array(image.getpalette())
    palette = np.reshape(palette, (palette.size // 3, 3))
    for i in set(image.getdata()):
        if i == transparent_idx:
            continue  # Do not emit a color definition for the transparent background!

        p = palette[i]
        output.write(f'#{i};2;{p[0]*100//256};{p[1]*100//256};{p[2]*100//256}')

    # body
    data = np.array(image.getdata())
    data = np.reshape(data, (data.size // width, width))
    for y in range(0, height, 6):
        for n, node in _convert_line(data[y:y+6]):
            if n == transparent_idx:
                continue  # Sixel Magic: Simply skip drawing to let the terminal show through

            output.write(f"#{n}")
            for six, count in node:
                if count < 4:
                    output.write(chr(0x3f + six) * count)
                else:
                    output.write(f'!{count}{chr(0x3f+six)}')
            output.write("$")
        output.write("-")

    # terminate
    output.write('\x1b\\\n')
    output.flush()


class SixelFigureManager(FigureManagerBase):
    def show(self):
        buf = io.BytesIO()
        fig = self.canvas.figure

        # Pass the exact configured facecolor from the figure instead of forcing transparent=True
        fig.savefig(buf, format='png',
                    facecolor=fig.get_facecolor(),
                    edgecolor=fig.get_edgecolor())

        buf.seek(0)
        with Image.open(buf) as image:
            output_sixel(image, sys.stdout)


class SixelFigureCanvas(FigureCanvasAgg):
    manager_class = SixelFigureManager


@_Backend.export
class _BackendSixelAgg(_Backend):
    FigureCanvas = SixelFigureCanvas
    FigureManager = SixelFigureManager

    @classmethod
    def new_figure_manager(cls, num, *args, **kwargs):
        provided_figsize = kwargs.get("figsize")
        rc_figsize = matplotlib.rcParams["figure.figsize"]

        if provided_figsize is not None and tuple(provided_figsize) != tuple(rc_figsize):
            return super().new_figure_manager(num, *args, **kwargs)

        dpi = kwargs.get("dpi")
        if dpi is None:
            dpi = matplotlib.rcParams["figure.dpi"]

        try:
            import fcntl
            import termios
            import struct
            res = fcntl.ioctl(sys.stdout.fileno(), termios.TIOCGWINSZ, struct.pack('HHHH', 0, 0, 0, 0))
            res_tuple = struct.unpack('HHHH', res)
            xpixels = res_tuple[2]
            ypixels = res_tuple[3]

            if xpixels > 0 and ypixels > 0:
                kwargs["figsize"] = (xpixels / dpi, (ypixels - 60) / dpi)
                return super().new_figure_manager(num, *args, **kwargs)
        except Exception:
            pass

        term_size = shutil.get_terminal_size(fallback=(80, 24))
        cols = term_size.columns
        lines = term_size.lines

        kwargs["figsize"] = ((cols * 10) / dpi, ((lines - 3) * 20) / dpi)

        return super().new_figure_manager(num, *args, **kwargs)
