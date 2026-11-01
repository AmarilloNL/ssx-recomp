"""Find guest functions the codegen missed, from a dumped image (SSX_DUMP_IMAGE).

Usage: python -I find_missing_funcs.py <image.bin> <generated/default/ssx_register.cpp>
Prints TOML [functions] lines for addresses that are referenced as function
pointers but not registered.
"""
import re
import struct
import sys

BASE = 0x82000000

img = open(sys.argv[1], "rb").read()
known = {int(a, 16) for a in re.findall(r"SetFunction\(0x([0-9A-F]+)", open(sys.argv[2]).read())}


def u32be(off):
    return struct.unpack_from(">I", img, off)[0]


# PE headers of the loaded basefile.
pe = struct.unpack_from("<I", img, 0x3C)[0]
nsec = struct.unpack_from("<H", img, pe + 6)[0]
opt_size = struct.unpack_from("<H", img, pe + 20)[0]
sections = []
for i in range(nsec):
    o = pe + 24 + opt_size + i * 40
    name = img[o:o + 8].rstrip(b"\0").decode(errors="replace")
    vsize, rva = struct.unpack_from("<II", img, o + 8)
    chars = struct.unpack_from("<I", img, o + 36)[0]
    vsize = max(0, min(vsize, len(img) - rva))  # dump stops at 0x83A00000
    sections.append((name, rva, vsize, chars))
    print(f"# section {name:8} {BASE + rva:08X}-{BASE + rva + vsize:08X} chars={chars:08X}", file=sys.stderr)

code = [(BASE + rva, BASE + rva + vs) for _, rva, vs, c in sections if c & 0x20000000]


def in_code(a):
    return a % 4 == 0 and any(lo <= a < hi for lo, hi in code)


missing = {}
extents = []  # (begin, end) of every .pdata function

# 1. .pdata: (begin, packed) big-endian pairs; packed bits 8-29 = length in words.
for name, rva, vsize, _ in sections:
    if name == ".pdata":
        for off in range(rva, rva + vsize - 7, 8):
            begin = u32be(off)
            if not begin or not in_code(begin):
                continue
            extents.append((begin, begin + ((u32be(off + 4) >> 8) & 0x3FFFFF) * 4))
            if begin not in known:
                missing.setdefault(begin, "pdata")
extents.sort()
starts = [b for b, _ in extents]


def inside_pdata_func(a):
    """True if a lies strictly inside a .pdata function (an interior label, not a start)."""
    import bisect
    i = bisect.bisect_right(starts, a) - 1
    return i >= 0 and extents[i][0] < a < extents[i][1]

interior = set()

# 2. Pointer tables in data sections: a code pointer adjacent to a known function pointer.
# Repeat until nothing new: each find can vouch for its own table neighbors.
grew = True
while grew:
    grew = False
    for name, rva, vsize, chars in sections:
        if chars & 0x20000000:
            continue
        for off in range(rva, rva + vsize - 3, 4):
            v = u32be(off)
            if not in_code(v) or v in known or v in missing or v in interior:
                continue
            neighbors = [u32be(o) for o in (off - 4, off + 4) if rva <= o < rva + vsize - 3]
            if any(n in known or n in missing for n in neighbors):
                grew = True
                if inside_pdata_func(v):
                    interior.add(v)
                else:
                    missing[v] = f"table@{BASE + off:08X}"

# 3. Addresses built in code with lis rX,hi + addi/ori rY,rX,lo (callbacks, vtable thunks).
# Only accepted where the target follows a function terminator (blr / bctr / b / padding).
def after_terminator(a):
    p = u32be(a - 4 - BASE)
    return p in (0x4E800020, 0x4E800420, 0, 0x60000000) or (p >> 26 == 18 and p & 3 == 0)


for lo_, hi_ in code:
    hi = {}
    for a in range(lo_, hi_, 4):
        ins = u32be(a - BASE)
        op, rd, ra = ins >> 26, (ins >> 21) & 31, (ins >> 16) & 31
        if op == 15 and ra == 0:  # lis
            hi[rd] = (ins & 0xFFFF) << 16
            continue
        if op == 14 and ra in hi:  # addi
            v = (hi[ra] + ((ins & 0xFFFF) ^ 0x8000) - 0x8000) & 0xFFFFFFFF
        elif op == 24 and rd in hi:  # ori rA,rS,uimm (rS is bits 21-25)
            v = hi[rd] | (ins & 0xFFFF)
        else:
            continue
        if (in_code(v) and v not in known and v not in missing
                and u32be(v - BASE) != 0 and not inside_pdata_func(v) and after_terminator(v)):
            missing[v] = f"lis/addi@{a:08X}"

for a in sorted(missing):
    print(f"0x{a:08X} = {{}}  # {missing[a]}")
print(f"# {len(missing)} candidates, {len(interior)} interior pointers skipped", file=sys.stderr)
