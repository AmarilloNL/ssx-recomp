"""Remove scanner-added [functions] entries that codegen shows to be interior labels.

Usage: python -I drop_splitting_funcs.py <codegen output> <ssx_register.cpp> <ssx_config.toml>
An added entry is interior if an unresolved branch jumps across it, or if the
function an unresolved branch sits in starts at it.
"""
import bisect
import re
import sys

log, reg, cfg = sys.argv[1:4]
pairs = [(int(t, 16), int(s, 16)) for t, s in re.findall(
    r"Unresolved (?:conditional branch to|b target) 0x([0-9A-F]+) from 0x([0-9A-F]+)", open(log).read())]
starts = sorted(int(a, 16) for a in re.findall(r"SetFunction\(0x([0-9A-F]+)", open(reg).read()))
owners = {starts[bisect.bisect_right(starts, min(p)) - 1] for p in pairs}
out, dropped = [], 0
for line in open(cfg).read().splitlines(keepends=True):
    m = re.match(r"0x([0-9A-F]+) = \{\}  # (?:lis/addi|table)", line)
    if m:
        x = int(m.group(1), 16)
        if x in owners or any(min(t, s) < x <= max(t, s) for t, s in pairs):
            dropped += 1
            continue
    out.append(line)
open(cfg, "w").writelines(out)
print(f"{len(pairs)} unresolved branches; dropped {dropped} entries")
