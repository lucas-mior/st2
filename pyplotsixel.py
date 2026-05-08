# Copyright (c) 2024 Hajime Nakagami
# Released under the BSD license.
# https://github.com/nakagami/pyplotsixel/blob/master/pyplotsixel.py
# Re-implemented using libsixel-python with 1-Bit Alpha Compositing

import sys
import shutil
import matplotlib
import numpy as np
from matplotlib.backend_bases import _Backend, FigureManagerBase
from matplotlib.backends.backend_agg import FigureCanvasAgg

try:
    import libsixel
    from libsixel.encoder import Encoder
except ImportError:
    sys.stderr.write("Error: 'libsixel-python' is not installed.\n")
    sys.exit(1)


class SixelFigureManager(FigureManagerBase):
    def show(self):
        # 1. Force Matplotlib to render to the buffer
        self.canvas.draw()
        width, height = self.canvas.get_width_height()
        
        # 2. Extract raw RGBA pixels
        pixels = np.frombuffer(self.canvas.buffer_rgba(), dtype=np.uint8).reshape((height, width, 4))

        # --- 1-BIT ALPHA COMPOSITING ---
        # Identify pixels that are 100% transparent (the true background)
        is_true_background = (pixels[:, :, 3] == 0)

        # Set your terminal background color here for blending the edges (e.g., Black)
        terminal_bg = np.array([0, 0, 0], dtype=np.float32)

        # Extract RGB and float Alpha
        rgb = pixels[:, :, :3].astype(np.float32)
        alpha = (pixels[:, :, 3] / 255.0)[..., np.newaxis]
        
        # Blend the semi-transparent edges against the terminal background
        blended_rgb = (rgb * alpha + terminal_bg * (1 - alpha)).astype(np.uint8)

        # Reconstruct the RGBA array with strictly 1-bit transparency
        final_rgba = np.empty_like(pixels)
        final_rgba[:, :, :3] = blended_rgb
        final_rgba[:, :, 3] = 255             # Make everything 100% opaque...
        final_rgba[is_true_background, 3] = 0 # ...EXCEPT the true background

        # 3. Encode with libsixel
        encoder = Encoder()
        encoder.encode_bytes(
            final_rgba.tobytes(),
            width,
            height,
            libsixel.SIXEL_PIXELFORMAT_RGBA8888,
            None
        )


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
