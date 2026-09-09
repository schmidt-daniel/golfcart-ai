"""HMI drawing layer.

Renders HMI screens to a PIL image (480x320, matching the ILI9488 TFT).
The image can be displayed on the physical display via luma.lcd, or saved /
logged when running headless (no display).

Provides primitives (rounded rects, text, progress bars, polygons) so screens
are declarative and easy to maintain.
"""

from __future__ import annotations

from typing import List, Optional, Tuple

from PIL import Image, ImageDraw, ImageFont

WIDTH = 480
HEIGHT = 320

# Palette (flat, high-contrast).
BG = (20, 24, 34)
FG = (240, 240, 245)
ACCENT = (76, 175, 80)     # green
WARN = (255, 152, 0)       # orange
DANGER = (244, 67, 54)     # red
BLUE = (33, 150, 243)      # blue
DIM = (120, 124, 134)

_FONT_CACHE = {}


def _font(size: int) -> ImageFont.FreeTypeFont:
    if size not in _FONT_CACHE:
        try:
            _FONT_CACHE[size] = ImageFont.truetype("DejaVuSans.ttf", size)
        except Exception:
            _FONT_CACHE[size] = ImageFont.load_default()
    return _FONT_CACHE[size]


class Screen:
    """A 480x320 canvas with drawing helpers."""

    def __init__(self) -> None:
        self.img = Image.new("RGB", (WIDTH, HEIGHT), BG)
        self.d = ImageDraw.Draw(self.img)

    def text(self, xy: Tuple[int, int], s: str, size: int = 18,
             fill=FG, anchor: Optional[str] = None) -> None:
        self.d.text(xy, s, font=_font(size), fill=fill, anchor=anchor)

    def rect(self, box: Tuple[int, int, int, int], fill=BG, outline=None,
             width: int = 1) -> None:
        self.d.rectangle(box, fill=fill, outline=outline, width=width)

    def rrect(self, box: Tuple[int, int, int, int], radius: int, fill=BG,
              outline=None, width: int = 1) -> None:
        self.d.rounded_rectangle(box, radius=radius, fill=fill, outline=outline,
                                 width=width)

    def line(self, xy: Tuple[Tuple[int, int], Tuple[int, int]], fill=FG,
             width: int = 1) -> None:
        self.d.line(xy, fill=fill, width=width)

    def polygon(self, pts: List[Tuple[int, int]], fill=None, outline=FG,
                width: int = 1) -> None:
        self.d.polygon(pts, fill=fill, outline=outline, width=width)

    def circle(self, xy: Tuple[int, int], r: int, fill=None, outline=FG,
               width: int = 1) -> None:
        self.d.ellipse([xy[0] - r, xy[1] - r, xy[0] + r, xy[1] + r],
                       fill=fill, outline=outline, width=width)

    def bar(self, x: int, y: int, w: int, h: int, frac: float,
            color=ACCENT) -> None:
        """Horizontal progress bar."""
        self.rect((x, y, x + w, y + h), fill=DIM)
        fw = max(0, int(w * max(0.0, min(1.0, frac))))
        if fw > 0:
            self.rect((x, y, x + w, y + h), fill=color)

    def header(self, title: str, subtitle: str = "") -> None:
        self.rect((0, 0, WIDTH, 44), fill=(30, 36, 50))
        self.text((16, 8), title, size=20, fill=FG)
        if subtitle:
            self.text((WIDTH - 16, 8), subtitle, size=16, fill=DIM, anchor="ra")

    def footer(self, hint: str) -> None:
        self.rect((0, HEIGHT - 30, WIDTH, HEIGHT), fill=(30, 36, 50))
        self.text((16, HEIGHT - 24), hint, size=14, fill=DIM)


def render_course_list(courses: List[Tuple[str, str]], cursor: int) -> Screen:
    """Course select screen. courses = [(id, name)]. cursor = selected index."""
    s = Screen()
    s.header("Select Course")
    y = 60
    for i, (cid, name) in enumerate(courses):
        fill = ACCENT if i == cursor else BG
        s.rrect((16, y, WIDTH - 16, y + 40), 8, fill=fill,
                outline=FG if i == cursor else DIM)
        s.text((28, y + 8), name, size=18, fill=FG)
        y += 48
    s.footer("Joystick: up/down move, press select")
    return s


def render_tee_select(tees: List[Tuple[str, str]], cursor: int) -> Screen:
    """Tee select screen. tees = [(id, name)]. cursor = selected index."""
    s = Screen()
    s.header("Select Tee")
    y = 60
    for i, (tid, name) in enumerate(tees):
        fill = ACCENT if i == cursor else BG
        s.rrect((16, y, WIDTH - 16, y + 40), 8, fill=fill,
                outline=FG if i == cursor else DIM)
        s.text((28, y + 8), name, size=18, fill=FG)
        y += 48
    s.footer("Joystick: up/down move, press select")
    return s


def render_hole(hole_number: int, hole_name: str, tee_name: str,
                distance_m: float, remaining_m: float,
                boundary: List[Tuple[float, float]],
                features: List[Tuple[str, float, float]],
                trolley: Optional[Tuple[float, float]]) -> Screen:
    """Hole screen: top-down layout with tee at bottom, green at top.

    boundary: list of (x, y) map-frame points.
    features: list of (type, x, y) map-frame points.
    trolley: (x, y) map-frame or None.
    """
    s = Screen()
    s.header(f"Hole {hole_number}", tee_name)
    if hole_name:
        s.text((16, 46), hole_name, size=16, fill=DIM)

    # Distance + remaining.
    s.text((16, 70), f"Dist: {distance_m:.0f} m", size=18, fill=FG)
    s.text((WIDTH - 16, 70), f"Rem: {remaining_m:.0f} m", size=18, fill=FG,
           anchor="ra")

    # Compute the map-frame bounding box of the hole.
    xs = [p[0] for p in boundary] + [f[1] for f in features]
    ys = [p[1] for p in boundary] + [f[2] for f in features]
    if trolley:
        xs.append(trolley[0])
        ys.append(trolley[1])
    if not xs or not ys:
        return s
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    spanx = max(maxx - minx, 1.0)
    spany = max(maxy - miny, 1.0)
    # Add margin.
    spanx *= 1.2
    spany *= 1.2
    cx = (minx + maxx) / 2.0
    cy = (miny + maxy) / 2.0

    # Map-frame -> screen. Green at top (screen y small), tee at bottom.
    # Screen area for the layout.
    ax, ay, aw, ah = 16, 100, WIDTH - 32, HEIGHT - 140
    scale = min(aw / spanx, ah / spany)
    def to_screen(x: float, y: float) -> Tuple[int, int]:
        # Map y increases north; screen y increases down. Flip y.
        sx = ax + (x - (cx - spanx / 2.0)) * scale
        sy = ay + (1.0 - (y - (cy - spany / 2.0)) / spany) * ah
        return int(sx), int(sy)

    # Boundary polygon.
    if len(boundary) >= 3:
        pts = [to_screen(x, y) for x, y in boundary]
        s.polygon(pts, fill=(40, 60, 40), outline=FG, width=2)

    # Features.
    for ftype, fx, fy in features:
        sx, sy = to_screen(fx, fy)
        if ftype == "GREEN":
            s.circle((sx, sy), 10, fill=ACCENT)
        elif ftype == "TEE_BOX":
            s.circle((sx, sy), 8, fill=BLUE)
        elif ftype == "WATER_HAZARD":
            s.circle((sx, sy), 6, fill=BLUE)
        else:
            s.circle((sx, sy), 4, fill=DIM)

    # Trolley position.
    if trolley:
        sx, sy = to_screen(trolley[0], trolley[1])
        s.circle((sx, sy), 6, fill=DANGER, outline=FG, width=2)

    s.footer("Press: back | long: home")
    return s