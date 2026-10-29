"""Small static-analysis helpers for the D2R runtime image dump (memory layout: file offset == RVA).

The dump is produced by tools/probe (cowportal-probe.dll) and is NOT part of this repository
(it is Blizzard's code). Point D2R_IMAGE at your own dump.

Deterministic, read-only. Usage:
    D2R_IMAGE=/path/to/d2r_image_93847.bin python3 -i tools/re/d2img.py
    >>> print(dis_func(0x5DA2A0))          # disassemble the cow portal function
    >>> riprel_xrefs(0x1D0E510)             # code referencing the "Cow Portal" string
    >>> call_xrefs(0x5155C0)                # callers of QUESTS_ChangeLevel
Section bounds below are for build 93847; read them from the probe's .txt sidecar for other builds.
"""
import bisect
import os
import struct

import numpy as np
from capstone import CS_ARCH_X86, CS_MODE_64, Cs

IMG_PATH = os.environ.get("D2R_IMAGE", "d2r_image_93847.bin")
BASE = 0x140000000
TEXT = (0x1000, 0x1000 + 0x01CAF000)
RDATA = (0x01CB0000, 0x01CB0000 + 0x005DF000)
DATA = (0x0228F000, 0x0228F000 + 0x01970000)
PDATA = (0x03BFF000, 0x03BFF000 + 0x001BA000)

img = open(IMG_PATH, "rb").read()
arr = np.frombuffer(img, dtype=np.uint8)
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False

# --- .pdata function table -------------------------------------------------
_funcs = []
for off in range(PDATA[0], PDATA[1], 12):
    b, e, u = struct.unpack_from("<III", img, off)
    if b == 0 and e == 0:
        continue
    if TEXT[0] <= b < TEXT[1] and b < e:
        _funcs.append((b, e))
_funcs.sort()
_starts = [f[0] for f in _funcs]


def func_of(rva):
    """Return (begin, end) of the .pdata entry containing rva (may be a chained fragment)."""
    i = bisect.bisect_right(_starts, rva) - 1
    if i >= 0 and _funcs[i][0] <= rva < _funcs[i][1]:
        return _funcs[i]
    return None


def i32_view(lo, hi):
    """int32 at every byte offset in [lo, hi)."""
    n = hi - lo
    a = arr[lo:hi + 3].astype(np.int64)
    v = a[0:n] | (a[1:n + 1] << 8) | (a[2:n + 2] << 16) | (a[3:n + 3] << 24)
    v = np.where(v >= 2**31, v - 2**32, v)
    return v


_text_i32 = None


def text_i32():
    global _text_i32
    if _text_i32 is None:
        _text_i32 = i32_view(TEXT[0], TEXT[1])
    return _text_i32


def riprel_xrefs(target, extra=(0, 1, 2, 4)):
    """Offsets of disp32 fields in .text with disp_end + disp == target, where disp_end = disp_pos+4+extra
    (extra = size of a trailing immediate). Returns sorted list of (disp_pos, extra)."""
    v = text_i32()
    pos = np.arange(TEXT[0], TEXT[1], dtype=np.int64)
    out = []
    for x in extra:
        hit = np.nonzero(pos + 4 + x + v == target)[0]
        out += [(int(TEXT[0] + h), x) for h in hit]
    return sorted(out)


def call_xrefs(target):
    """Offsets of E8/E9 rel32 instructions targeting target."""
    v = text_i32()
    pos = np.arange(TEXT[0], TEXT[1], dtype=np.int64)
    hit = np.nonzero(pos + 4 + v == target)[0]
    res = []
    for h in hit:
        p = int(TEXT[0] + h) - 1
        if img[p] in (0xE8, 0xE9):
            res.append((p, "call" if img[p] == 0xE8 else "jmp"))
    return res


def find_insn_containing(disp_pos, back=15):
    """Find an instruction start s (disp_pos-back..disp_pos) whose decoding covers disp_pos."""
    for s in range(disp_pos - back, disp_pos):
        insns = list(md.disasm(img[s:s + 16], BASE + s, count=1))
        if insns and s + insns[0].size > disp_pos + 3 and insns[0].size >= (disp_pos - s) + 4:
            return insns[0]
    return None


def dis(start, end=None, count=None):
    end = end or start + 0x200
    out = []
    for ins in md.disasm(img[start:end], BASE + start):
        out.append(f"{ins.address - BASE:08X}: {ins.bytes.hex():<24} {ins.mnemonic} {ins.op_str}")
        if count and len(out) >= count:
            break
    return "\n".join(out)


def dis_func(rva):
    f = func_of(rva)
    if not f:
        return f"no pdata func for {rva:X}"
    return f"; func {f[0]:08X}-{f[1]:08X}\n" + dis(f[0], f[1])


def cstr(rva, n=200):
    s = img[rva:rva + n]
    return s.split(b"\0")[0].decode("latin1")


def q(rva):
    return struct.unpack_from("<Q", img, rva)[0]


def abs_ptr_xrefs(target_rva, lo=RDATA[0], hi=DATA[1]):
    """Find 8-byte absolute pointers (BASE+target) in data sections."""
    needle = struct.pack("<Q", BASE + target_rva)
    res, i = [], img.find(needle, lo, hi)
    while i != -1:
        res.append(i)
        i = img.find(needle, i + 1, hi)
    return res
