#!/usr/bin/env python3
"""Generate HMI mockup SVGs (480x640 portrait) from the new HMI spec."""

from __future__ import annotations

from pathlib import Path

W = 480
H = 640

BG = "#0F172A"
SURFACE = "#1E293B"
SURFACE2 = "#334155"
TEXT = "#F8FAFC"
DIM = "#94A3B8"
ACCENT = "#38BDF8"
OK = "#34D399"
WARN = "#FBBF24"
DANGER = "#F87171"
INFO = "#818CF8"

OUT = Path(__file__).resolve().parents[1] / "docs" / "hmi"


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def head(title: str) -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">\n'
        f'  <defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">'
        f'<stop offset="0" stop-color="{BG}"/><stop offset="1" stop-color="#0B1220"/>'
        f'</linearGradient></defs>\n'
        f'  <rect width="{W}" height="{H}" fill="url(#bg)"/>\n'
        f'  <rect x="0" y="0" width="{W}" height="40" fill="{SURFACE}"/>\n'
        f'  <text x="16" y="26" font-family="Arial,sans-serif" font-size="13" '
        f'font-weight="bold" fill="{TEXT}" letter-spacing="1">{esc(title)}</text>\n'
        f'  <text x="452" y="26" font-family="Arial,sans-serif" font-size="13" '
        f'fill="{DIM}" text-anchor="end">14:32</text>\n'
    )


def foot(hint: str = "● Select   ◉ Back   ◎ Home") -> str:
    return (
        f'  <rect x="0" y="{H - 40}" width="{W}" height="40" fill="{SURFACE}"/>\n'
        f'  <text x="16" y="{H - 16}" font-family="Arial,sans-serif" font-size="13" '
        f'fill="{DIM}">{esc(hint)}</text>\n</svg>\n'
    )


def item(y: int, label: str, value: str = "", selected: bool = False,
         danger: bool = False, h: int = 52) -> str:
    fill = ACCENT if selected else (DANGER if danger else SURFACE)
    tc = "#0F172A" if (selected or danger) else TEXT
    mark = "x " if selected and not value else ""
    s = f'  <rect x="16" y="{y}" width="448" height="{h}" rx="12" fill="{fill}"/>\n'
    s += f'  <text x="36" y="{y + h // 2 + 6}" font-family="Arial,sans-serif" ' \
         f'font-size="18" font-weight="bold" fill="{tc}">{esc(mark + label)}</text>\n'
    if value:
        s += f'  <text x="444" y="{y + h // 2 + 6}" font-family="Arial,sans-serif" ' \
             f'font-size="18" font-weight="bold" fill="{tc}" text-anchor="end">{esc(value)}</text>\n'
    return s


def menu_rows(rows) -> str:
    """rows: list of (label, value, selected, danger) or (label, value)."""
    y = 52
    s = ""
    for r in rows:
        label, value = r[0], (r[1] if len(r) > 1 else "")
        selected = r[2] if len(r) > 2 else False
        danger = r[3] if len(r) > 3 else False
        s += item(y, label, value, selected=selected, danger=danger)
        y += 62
        if y >= H - 60:
            break
    return s


def write(name: str, content: str) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(content)
    print(f"wrote {name}")


def main_menu() -> str:
    items = [("MAP", "", True), ("MODE", ""), ("ASSIST", ""), ("CHANGE HOLE", ""),
             ("SELECT COURSE", ""), ("WIFI", ""), ("DEBUG", ""), ("SHUTDOWN", "", False, True)]
    return head("MAIN MENU") + menu_rows(items) + foot()


def course_selection() -> str:
    items = [("Red Course", "", True), ("Blue Course", ""), ("City Links", ""),
             ("Main Menu", "")]
    return head("COURSES") + menu_rows(items) + foot()


def teebox_selection() -> str:
    items = [("Red", "", True), ("Blue", ""), ("White", ""), ("Yellow", ""),
             ("Course Selection", "")]
    return head("TEEBOXES") + menu_rows(items) + foot()


def mode_selection() -> str:
    items = [("Manual/Push Ass.", "", True), ("Follow Me", ""), ("Autonomous", ""),
             ("Main Menu", "")]
    return head("MODE") + menu_rows(items) + foot()


def assist() -> str:
    items = [("Push Assist", "ON", True), ("Assist Level", "3"), ("Hill Assist", "ON"),
             ("Main Menu", "")]
    return head("ASSIST") + menu_rows(items) + foot()


def change_hole() -> str:
    items = [("Hole 1", "380 m", True), ("Hole 2", "350 m"), ("Hole 3", "410 m"),
             ("Course Selection", "")]
    return head("CHANGE HOLE") + menu_rows(items) + foot()


def wifi() -> str:
    s = head("WIFI")
    s += (
        f'  <text x="36" y="90" font-family="Arial,sans-serif" font-size="18" fill="{TEXT}">SSID: golfcart-1a2b</text>\n'
        f'  <text x="36" y="126" font-family="Arial,sans-serif" font-size="18" fill="{TEXT}">Pass: golfcart2026</text>\n'
        f'  <rect x="120" y="170" width="240" height="240" rx="8" fill="#FFFFFF"/>\n'
        f'  <g fill="#0F172A">\n'
        f'    <rect x="150" y="200" width="10" height="10"/><rect x="170" y="200" width="10" height="10"/>'
        f'<rect x="190" y="200" width="10" height="10"/><rect x="210" y="200" width="10" height="10"/>'
        f'<rect x="230" y="200" width="10" height="10"/><rect x="250" y="200" width="10" height="10"/>'
        f'<rect x="270" y="200" width="10" height="10"/><rect x="290" y="200" width="10" height="10"/>'
        f'<rect x="150" y="220" width="10" height="10"/><rect x="180" y="220" width="10" height="10"/>'
        f'<rect x="210" y="220" width="10" height="10"/><rect x="240" y="220" width="10" height="10"/>'
        f'<rect x="270" y="220" width="10" height="10"/><rect x="150" y="240" width="10" height="10"/>'
        f'<rect x="200" y="240" width="10" height="10"/><rect x="250" y="240" width="10" height="10"/>'
        f'<rect x="160" y="260" width="10" height="10"/><rect x="190" y="260" width="10" height="10"/>'
        f'<rect x="240" y="260" width="10" height="10"/><rect x="290" y="260" width="10" height="10"/>'
        f'<rect x="170" y="280" width="10" height="10"/><rect x="200" y="280" width="10" height="10"/>'
        f'<rect x="230" y="280" width="10" height="10"/><rect x="260" y="280" width="10" height="10"/>'
        f'<rect x="150" y="300" width="10" height="10"/><rect x="190" y="300" width="10" height="10"/>'
        f'<rect x="230" y="300" width="10" height="10"/><rect x="280" y="300" width="10" height="10"/>'
        f'<rect x="160" y="320" width="10" height="10"/><rect x="250" y="320" width="10" height="10"/>'
        f'<rect x="180" y="340" width="10" height="10"/><rect x="220" y="340" width="10" height="10"/>'
        f'<rect x="260" y="340" width="10" height="10"/>\n'
        f'  </g>\n'
        f'  <text x="108" y="440" font-family="Arial,sans-serif" font-size="13" fill="{DIM}">Scan to connect</text>\n'
    )
    s += foot("PRESS: refresh   ◉ Back")
    return s


def debug_menu() -> str:
    items = [("System", ""), ("GPS", ""), ("LiDAR", ""), ("Camera", ""),
             ("IMU", ""), ("Navigation", ""), ("Main Menu", "")]
    return head("DEBUG") + menu_rows(items) + foot()


def kv_view(title: str, rows) -> str:
    s = head(title)
    y = 60
    for label, value in rows:
        s += f'  <text x="17" y="{y}" font-family="Arial,sans-serif" font-size="17" fill="{DIM}">{esc(label)}</text>\n'
        s += f'  <text x="452" y="{y}" font-family="Arial,sans-serif" font-size="17" fill="{TEXT}" '
        s += f'text-anchor="end" font-weight="bold">{esc(value)}</text>\n'
        y += 40
    s += foot()
    return s


def system() -> str:
    rows = [("Battery", "82%"), ("CPU", "35%"), ("Disk", "61%"), ("Uptime", "3:42"),
            ("Memory", "1.8 GB / 8 GB"), ("Temp", "57.2 C")]
    return kv_view("SYSTEM", rows)


def gps() -> str:
    rows = [("Latitude", "48.123456"), ("Longitude", "11.67890"), ("Altitude", "234.1 m"),
            ("Speed", "1.2 m/s"), ("Heading", "47.3 deg"), ("Satellites", "9"),
            ("Timestamp", "12:34:56"), ("Accuracy x/y", "1.8 m"), ("Acc elev", "2.4 m"),
            ("Status", "FIX")]
    return kv_view("GPS", rows)


def imu() -> str:
    rows = [("Roll", "+0.02 rad"), ("Pitch", "+0.31 rad"), ("Heading", "- none -"),
            ("Accel x", "+0.02 m/s²"), ("Accel y", "+0.11 m/s²"), ("Accel z", "-9.81 m/s²"),
            ("Temperature", "n/a"), ("Status", "VALID")]
    return kv_view("IMU", rows)


def lidar() -> str:
    s = head("LIDAR")
    import math as _m
    cx, cy, rmax = 240, 300, 200
    # rings
    for rr in (50, 100, 150, 200):
        s += f'  <circle cx="{cx}" cy="{cy}" r="{rr}" fill="none" stroke="{SURFACE2}" stroke-width="1"/>\n'
    # scan points
    import random
    rnd = random.Random(7)
    for ang in range(0, 360, 6):
        rad = _m.radians(ang)
        dist = rmax * rnd.uniform(0.2, 1.0)
        x = cx + dist * _m.cos(rad)
        y = cy + dist * _m.sin(rad)
        s += f'  <circle cx="{x:.0f}" cy="{y:.0f}" r="3" fill="{OK}"/>\n'
    s += f'  <text x="17" y="{H - 90}" font-family="Arial,sans-serif" font-size="17" fill="{DIM}">Obstacle</text>\n'
    s += f'  <text x="452" y="{H - 90}" font-family="Arial,sans-serif" font-size="17" fill="{OK}" text-anchor="end" font-weight="bold">NONE</text>\n'
    s += f'  <text x="17" y="{H - 50}" font-family="Arial,sans-serif" font-size="17" fill="{DIM}">Nearest</text>\n'
    s += f'  <text x="452" y="{H - 50}" font-family="Arial,sans-serif" font-size="17" fill="{TEXT}" text-anchor="end" font-weight="bold">1.2 m</text>\n'
    s += foot("PRESS: cloud/range   ◉ Back")
    return s


def camera() -> str:
    s = head("CAMERA")
    s += f'  <rect x="40" y="90" width="400" height="320" fill="#0B0F1A" stroke="{SURFACE2}" stroke-width="2"/>\n'
    # simple scenery
    s += f'  <rect x="40" y="320" width="400" height="90" fill="{SURFACE2}"/>\n'
    s += f'  <circle cx="240" cy="200" r="28" fill="{ACCENT}"/>\n'
    s += f'  <text x="17" y="460" font-family="Arial,sans-serif" font-size="17" fill="{DIM}">Segmentation</text>\n'
    s += f'  <text x="452" y="460" font-family="Arial,sans-serif" font-size="17" fill="{WARN}" text-anchor="end" font-weight="bold">OFF</text>\n'
    s += f'  <text x="17" y="500" font-family="Arial,sans-serif" font-size="17" fill="{DIM}">Detections</text>\n'
    s += f'  <text x="452" y="500" font-family="Arial,sans-serif" font-size="17" fill="{TEXT}" text-anchor="end" font-weight="bold">1</text>\n'
    s += foot("LEFT/RIGHT: feeds   PRESS: seg   ◉ Back")
    return s


def navigation() -> str:
    s = head("NAVIGATION")
    s += f'  <text x="17" y="76" font-family="Arial,sans-serif" font-size="17" fill="{DIM}">Status</text>\n'
    s += f'  <text x="452" y="76" font-family="Arial,sans-serif" font-size="17" fill="{OK}" text-anchor="end" font-weight="bold">NAVIGATING</text>\n'
    # costmap
    s += f'  <rect x="40" y="110" width="400" height="360" fill="#0B1220" stroke="{SURFACE2}" stroke-width="2"/>\n'
    s += f'  <polygon points="120,420 360,420 180,150" fill="#1B3A2B" stroke="{OK}" stroke-width="2"/>\n'
    # planned path
    s += f'  <polyline points="120,420 180,360 220,300 180,150" fill="none" stroke="{ACCENT}" stroke-width="3"/>\n'
    s += f'  <circle cx="120" cy="420" r="8" fill="{TEXT}"/>\n'
    s += kv_row(500, "Path length", "45.2 m")
    s += kv_row(540, "Target", "Hole 5 green")
    s += foot()
    return s


def kv_row(y: int, label: str, value: str) -> str:
    return (
        f'  <text x="17" y="{y}" font-family="Arial,sans-serif" font-size="17" fill="{DIM}">{esc(label)}</text>\n'
        f'  <text x="452" y="{y}" font-family="Arial,sans-serif" font-size="17" fill="{TEXT}" '
        f'text-anchor="end" font-weight="bold">{esc(value)}</text>\n'
    )


def kv_view(title: str, rows) -> str:
    s = head(title)
    y = 60
    for label, value in rows:
        s += kv_row(y, label, value)
        y += 40
    s += foot()
    return s


def list_item(y, label, value="", selected=False, danger=False, h=52):
    return item(y, label, value, selected, danger, h)


def map_view() -> str:
    s = head("HOLE 3")
    # header info line
    s += f'  <text x="17" y="62" font-family="Arial,sans-serif" font-size="16" fill="{DIM}">Red tee  ·  380 m</text>\n'
    s += f'  <text x="452" y="62" font-family="Arial,sans-serif" font-size="16" fill="{TEXT}" text-anchor="end" font-weight="bold">Rem 120 m</text>\n'
    # map frame
    s += f'  <rect x="24" y="84" width="432" height="480" fill="#0B1220" stroke="{SURFACE2}" stroke-width="2"/>\n'
    # boundary polygon (hole shape)
    s += f'  <polygon points="360,120 300,300 120,360 60,200 220,90" fill="#12311F" stroke="{OK}" stroke-width="2"/>\n'
    # fairway path
    s += f'  <polyline points="70,500 200,460 260,320 130,140" fill="none" stroke="#2E4B36" stroke-width="6"/>\n'
    # teebox (bottom) blue
    s += f'  <rect x="230" y="470" width="60" height="20" rx="4" fill="{INFO}"/>\n'
    s += f'  <text x="260" y="484" font-family="Arial,sans-serif" font-size="11" fill="#0F172A" text-anchor="middle" font-weight="bold">TEE</text>\n'
    # green (top) accent
    s += f'  <circle cx="150" cy="150" r="22" fill="{OK}"/>\n'
    s += f'  <text x="150" y="156" font-family="Arial,sans-serif" font-size="11" fill="#0F172A" text-anchor="middle" font-weight="bold">PIN</text>\n'
    # water hazard
    s += f'  <circle cx="360" cy="300" r="26" fill="#1D3A5F" stroke="{INFO}" stroke-width="1"/>\n'
    # trolley position (danger dot on the path)
    s += f'  <circle cx="210" cy="430" r="8" fill="{DANGER}" stroke="{TEXT}" stroke-width="2"/>\n'
    s += f'  <text x="210" y="418" font-family="Arial,sans-serif" font-size="11" fill="{TEXT}" text-anchor="middle">YOU</text>\n'
    # speed bar footer
    s += f'  <text x="17" y="596" font-family="Arial,sans-serif" font-size="16" fill="{DIM}">Speed</text>\n'
    s += f'  <text x="70" y="596" font-family="Arial,sans-serif" font-size="16" fill="{TEXT}" text-anchor="end">−</text>\n'
    s += f'  <text x="420" y="596" font-family="Arial,sans-serif" font-size="16" fill="{TEXT}" text-anchor="end">+</text>\n'
    s += f'  <rect x="90" y="580" width="300" height="12" fill="{SURFACE}"/>\n'
    s += f'  <rect x="90" y="580" width="120" height="12" fill="{ACCENT}"/>\n'
    s += f'  <text x="256" y="596" font-family="Arial,sans-serif" font-size="14" fill="{TEXT}" text-anchor="middle">0.8 m/s</text>\n'
    s += foot()
    return s


if __name__ == "__main__":
    write("map.svg", map_view())
    write("main-menu.svg", main_menu())
    write("course-selection.svg", course_selection())
    write("teebox-selection.svg", teebox_selection())
    write("mode-selection.svg", mode_selection())
    write("assist.svg", assist())
    write("change-hole.svg", change_hole())
    write("wifi.svg", wifi())
    write("debug-menu.svg", debug_menu())
    write("system.svg", system())
    write("gps.svg", gps())
    write("imu.svg", imu())
    write("lidar.svg", lidar())
    write("camera.svg", camera())
    write("navigation.svg", navigation())
    print("done")
