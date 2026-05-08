# Copyright (c) 2024 Hajime Nakagami
# Released under the BSD license.
# https://github.com/nakagami/pyplotsixel/blob/master/pyplotsixel.py
# Re-implemented using libsixel-python for high-performance rendering.

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
    sys.stderr.write("Please install it using: pip install libsixel-python\n")
    sys.exit(1)


class SixelFigureManager(FigureManagerBase):
    def show(self):
        # Force a draw so the canvas renders the figure to the buffer
        self.canvas.draw()
        width, height = self.canvas.get_width_height()
        
        # Extract raw RGBA pixels into a NumPy array (Height x Width x 4)
        pixels = np.frombuffer(self.canvas.buffer_rgba(), dtype=np.uint8).reshape((height, width, 4))

        # --- ALPHA COMPOSITING ---
        # Sixel cannot handle the partial transparency (alpha channel) needed 
        # for smooth anti-aliased edges. If the user sets facecolor="none", 
        # we must mathematically blend the image over a solid terminal background.
        
        # Change this if your terminal is not black (e.g., White = [255, 255, 255])
        terminal_bg_color = np.array([0, 0, 0], dtype=np.float32)

        rgb = pixels[:, :, :3].astype(np.float32)
        alpha = (pixels[:, :, 3] / 255.0)[..., np.newaxis]
        
        # Blend: Output = Foreground * Alpha + Background * (1 - Alpha)
        blended = (rgb * alpha + terminal_bg_color * (1 - alpha)).astype(np.uint8)

        # Encode as pure RGB (no alpha) to ensure perfect resolution
        encoder = Encoder()
        encoder.encode_bytes(
            blended.tobytes(),
            width,
            height,
            libsixel.SIXEL_PIXELFORMAT_RGB888,
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
