"""Capture a window straight from its own DC (no screen-coordinate guessing)."""
import ctypes, ctypes.wintypes as wt, sys, zlib, struct

u = ctypes.windll.user32
g = ctypes.windll.gdi32
u.SetProcessDPIAware()

cls = sys.argv[1] if len(sys.argv) > 1 else "RoachPetSettings"
out = sys.argv[2] if len(sys.argv) > 2 else "build/winshot.png"

h = u.FindWindowW(cls, None)
if not h:
    print("window not found:", cls)
    raise SystemExit(1)

r = wt.RECT()
u.GetWindowRect(h, ctypes.byref(r))
w, hgt = r.right - r.left, r.bottom - r.top
print("hwnd", h, "size", w, hgt)

hdc = u.GetWindowDC(h)
mdc = g.CreateCompatibleDC(hdc)
bmp = g.CreateCompatibleBitmap(hdc, w, hgt)
g.SelectObject(mdc, bmp)

# PW_RENDERFULLCONTENT = 2, needed for layered/DWM-composed windows
ok = u.PrintWindow(h, mdc, 2)
if not ok:
    ok = u.PrintWindow(h, mdc, 0)
print("PrintWindow ->", ok)

class BMIH(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
                ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]

bi = BMIH()
bi.biSize = ctypes.sizeof(BMIH)
bi.biWidth = w
bi.biHeight = -hgt          # top-down
bi.biPlanes = 1
bi.biBitCount = 32
bi.biCompression = 0

buf = ctypes.create_string_buffer(w * hgt * 4)
g.GetDIBits(mdc, bmp, 0, hgt, buf, ctypes.byref(bi), 0)

# BGRA -> RGB, write a PNG
rows = []
for y in range(hgt):
    line = buf.raw[y * w * 4:(y + 1) * w * 4]
    px = bytearray(w * 3)
    px[0::3] = line[2::4]
    px[1::3] = line[1::4]
    px[2::3] = line[0::4]
    rows.append(b"\x00" + bytes(px))
raw = b"".join(rows)

def chunk(tag, data):
    c = tag + data
    return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", w, hgt, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 6))
       + chunk(b"IEND", b""))
open(out, "wb").write(png)
print("wrote", out, len(png), "bytes")
