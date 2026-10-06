#!/usr/bin/env python3
# kmsgrab.py <prefix>: what the panels show right now, read from the display controller: one <prefix>-crtc<id>-plane<id>.ppm
# per plane that is showing a buffer (the buffer as it is, before the controller scales or places it). For the times
# sway isn't drawing and grim has nothing to grab: a DS game (libdsflip's planes), Döda Kvarter, a black screen that
# shouldn't be. Run on the handheld as root; a line per plane says its buffer's size and format.
import fcntl, mmap, os, struct, sys
if len(sys.argv) != 2: sys.exit("usage: kmsgrab.py <prefix>")
fd = os.open("/dev/dri/card0", os.O_RDWR)
def io(req, buf):
    b = bytearray(buf); fcntl.ioctl(fd, req, b, True); return bytes(b)
io(0x4010640d, struct.pack("QQ", 2, 1))                                  # SET_CLIENT_CAP universal planes
n = struct.unpack("QI4x", io(0xc01064b5, struct.pack("QI4x", 0, 0)))[1]  # GETPLANERESOURCES: how many
import ctypes
ids = (ctypes.c_uint32 * n)()
io(0xc01064b5, struct.pack("QI4x", ctypes.addressof(ids), n))
for pid in ids:
    _, crtc, fb, *_ = struct.unpack("6IQ", io(0xc02064b6, struct.pack("6IQ", pid, 0, 0, 0, 0, 0, 0)))   # GETPLANE
    if not fb: continue
    r = struct.unpack("5I4I4I4I4x4Q", io(0xc06864ce, struct.pack("5I4I4I4I4x4Q", fb, *([0] * 20))))        # GETFB2
    w, h, fmt, handle, pitch, off = r[1], r[2], r[3], r[5], r[9], r[13]
    cc = struct.pack("I", fmt).decode("ascii", "replace")
    print(f"plane {pid} crtc {crtc}: {w}x{h} {cc} pitch {pitch}")
    if not handle or cc not in ("XR24", "AR24", "RG16", "XB24", "AB24"): continue
    try:                                                                                                    # MAP_DUMB,
        moff = struct.unpack("IIQ", io(0xc01064b3, struct.pack("IIQ", handle, 0, 0)))[2]
        m = mmap.mmap(fd, off + pitch * h, mmap.MAP_SHARED, mmap.PROT_READ, offset=moff)
    except OSError:                                                              # or, a buffer made elsewhere, as a dma-buf
        buf = struct.unpack("IIi", io(0xc00c642d, struct.pack("IIi", handle, 0x80002, -1)))[2]              # PRIME_HANDLE_TO_FD
        m = mmap.mmap(buf, off + pitch * h, mmap.MAP_SHARED, mmap.PROT_READ); os.close(buf)
    out = bytearray(w * h * 3)
    for y in range(h):
        row = m[off + y * pitch: off + y * pitch + w * (2 if cc == "RG16" else 4)]
        if cc == "RG16":
            px = struct.unpack(f"<{w}H", row)
            o = y * w * 3
            for x, p in enumerate(px):
                out[o + 3 * x] = (p >> 11) * 255 // 31; out[o + 3 * x + 1] = ((p >> 5) & 63) * 255 // 63; out[o + 3 * x + 2] = (p & 31) * 255 // 31
        elif cc in ("XR24", "AR24"):
            out[y * w * 3 + 0: (y + 1) * w * 3: 3] = row[2::4]; out[y * w * 3 + 1: (y + 1) * w * 3: 3] = row[1::4]; out[y * w * 3 + 2: (y + 1) * w * 3: 3] = row[0::4]
        else:
            out[y * w * 3 + 0: (y + 1) * w * 3: 3] = row[0::4]; out[y * w * 3 + 1: (y + 1) * w * 3: 3] = row[1::4]; out[y * w * 3 + 2: (y + 1) * w * 3: 3] = row[2::4]
    m.close()
    fcntl.ioctl(fd, 0x40086409, struct.pack("II", handle, 0))                                               # GEM_CLOSE
    with open(f"{sys.argv[1]}-crtc{crtc}-plane{pid}.ppm", "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h)); f.write(out)
