# Copyright (c) 2024 Hajime Nakagami
# Released under the BSD license.
# https://github.com/nakagami/pyplotsixel/blob/master/pyplotsixel.py
# Re-implemented using Chafa for perfect transparency and anti-aliasing.

import sys
import io
import shutil
import subprocess
import matplotlib
from matplotlib.backend_bases import _Backend, FigureManagerBase
from matplotlib.backends.backend_agg import FigureCanvasAgg

class SixelFigureManager(FigureManagerBase):
    def show(self):
        # 1. Save the Matplotlib figure to memory as a high-quality PNG
        # Using transparent=True forces Matplotlib to preserve the alpha channel.
        buf = io.BytesIO()
        self.canvas.figure.savefig(buf, format='png', transparent=True)
        
        # 2. Pipe the PNG directly into Chafa
        try:
            subprocess.run(
                ["chafa", "--format=sixel", "-"],
                input=buf.getvalue(),
                check=True
            )
        except FileNotFoundError:
            sys.stderr.write("Error: 'chafa' is not installed or not in your PATH.\n")
        except subprocess.CalledProcessError as e:
            sys.stderr.write(f"Error: chafa failed to encode the image (Exit code {e.returncode}).\n")


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
