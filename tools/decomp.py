#!/usr/bin/env python3
"""Query the decompilation databases produced by build_fs1r_ghidra.py.

    python tools/decomp.py 0x3b0108              print one function (address or FUN_ name)
    python tools/decomp.py --grep 'DAT_00c0'     list functions whose C text matches the regex
    python tools/decomp.py --refs 0xc00000-0xc007ff   addresses in the range referenced, with the functions using them
    python tools/decomp.py --calls 0x3b0108      callers and callees
    add --plg to use the PLG150-DX database instead of the FS1R one
"""
import re
import sqlite3
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1].parent / "FS1R_DISASM"
ADDR_RE = re.compile(r"\b(?:DAT|PTR|LAB|FUN|UNK|Ram|BYTE|WORD|DWORD|thunk_FUN)_?([0-9a-fA-F]{8})\b|\b0x([0-9a-fA-F]{5,8})\b")


def db(plg):
    p = ROOT / ("PLG150DX_GHIDRA_ANALYSIS" if plg else "FS1R_GHIDRA_ANALYSIS") / "decomp.db"
    return sqlite3.connect(f"file:{p.as_posix()}?mode=ro", uri=True)


def parse_addr(s):
    m = re.search(r"([0-9a-fA-F]{4,8})$", s)
    return int(m.group(1), 16) if not s.lower().startswith("0x") else int(s, 16)


def main():
    args = sys.argv[1:]
    plg = "--plg" in args
    args = [a for a in args if a != "--plg"]
    c = db(plg)
    if not args:
        print(__doc__)
        return
    if args[0] == "--grep":
        rx = re.compile(args[1])
        for a, n, s, src in c.execute("select address,name,size,raw_decomp from decompilations where status='decompiled'"):
            hits = rx.findall(src)
            if hits:
                print(f"{a:08x} {n} size {s}: {len(hits)} hits")
    elif args[0] == "--refs":
        lo, hi = (int(x, 16) for x in args[1].split("-"))
        byaddr = Counter()
        funcs = defaultdict(set)
        for a, n, src in c.execute("select address,name,raw_decomp from decompilations where status='decompiled'"):
            for m in ADDR_RE.finditer(src):
                v = int(m.group(1) or m.group(2), 16)
                if lo <= v <= hi:
                    byaddr[v] += 1
                    funcs[v].add(a)
        for v in sorted(byaddr):
            print(f"{v:08x} x{byaddr[v]:<4} {' '.join(f'{f:x}' for f in sorted(funcs[v])[:10])}")
    elif args[0] == "--calls":
        a = parse_addr(args[1])
        name = c.execute("select name from decompilations where address=?", (a,)).fetchone()
        print("function", name)
        rx = re.compile(rf"\b(?:thunk_)?FUN_{a:08x}\b")
        print("callers:")
        for fa, fn, src in c.execute("select address,name,raw_decomp from decompilations where status='decompiled'"):
            if fa != a and rx.search(src):
                print(f"  {fa:08x} {fn}")
        src = c.execute("select raw_decomp from decompilations where address=?", (a,)).fetchone()[0]
        print("callees:", sorted(set(re.findall(r"\b(?:thunk_)?FUN_[0-9a-f]{8}", src))))
    else:
        a = parse_addr(args[0])
        r = c.execute("select name,size,raw_decomp from decompilations where address=?", (a,)).fetchone()
        if not r:
            print("no function at", hex(a))
            return
        print(f"// {r[0]} size {r[1]}")
        print(r[2])


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
