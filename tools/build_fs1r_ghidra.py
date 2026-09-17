#!/usr/bin/env python3
"""Import, auto-analyze, and batch-decompile the FS1R / PLG150-DX firmware (SH-2) with pyghidra.

    python tools/build_fs1r_ghidra.py --dry-run
    python tools/build_fs1r_ghidra.py --only fs1r

Layout: ROM images live in the sibling FS1R_DISASM/roms folder (see its README.md); Ghidra projects
and the decomp.db sqlite files are written next to them.  Modelled on FM8.plus/tools/build_fm8_ghidra.py.
"""
from __future__ import annotations

import argparse
import io
import json
import os
import sqlite3
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

if sys.stdout.encoding != "utf-8":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")

GHIDRA_INSTALL_DIR = os.environ.get("GHIDRA_INSTALL_DIR", r"E:\ghidra_12.1.2_PUBLIC")
ROOT = Path(os.environ.get("FS1R_DISASM", Path(__file__).resolve().parents[1].parent / "FS1R_DISASM"))
ROMS = ROOT / "roms"
LANG = "SuperH:BE:32:SH-2"

# SH7040-series address map (SH7044: 256 KB on-chip flash, 4 KB on-chip RAM, 4 external CS areas of 4 MB).
# blocks: (name, start, size, file|None, volatile)
SH704X_COMMON = [
    ("cs1", 0x00400000, 0x400000, None, False),
    ("cs2", 0x00800000, 0x400000, None, False),
    ("cs3", 0x00C00000, 0x400000, None, False),
    ("dram", 0x01000000, 0x80000, None, False),    # 512 KB work RAM, the only thing populated in the SH7044's dedicated
                                                   # DRAM area 0x01000000-0x01FFFFFF (literal pools use 0x0100xxxx-0x0106xxxx)
    ("periph", 0xFFFF8000, 0x7000, None, True),   # on-chip peripheral registers (SCI, MTU, INTC, BSC, DMAC, PFC, ports, A/D, flash ctl)
    ("ram", 0xFFFFF000, 0x1000, None, False),      # on-chip RAM, SP reset value is 0xFFFFFFFC
]

VERSIONS = [
    dict(key="fs1r", proj_name="FS1R", program="fs1r_v120",
         base=ROMS / "fs1r_sh7044_flash_1.20_256k.bin",   # loaded at 0 by the BinaryLoader
         blocks=[("eprom", 0x00200000, 0x200000, ROMS / "fs1r_v120_eprom_cpuview.bin", False)] + SH704X_COMMON,
         vectors=[0x0, 0x8000],
         proj=ROOT / "FS1R_GHIDRA_PROJ", analysis=ROOT / "FS1R_GHIDRA_ANALYSIS"),
    dict(key="plg150dx", proj_name="PLG150DX", program="plg150dx",
         base=ROMS / "PLG150-DX.BIN",                     # SH7043 in ROM-less mode: external ROM is CS0 at 0
         blocks=SH704X_COMMON,
         vectors=[0x0],
         proj=ROOT / "PLG150DX_GHIDRA_PROJ", analysis=ROOT / "PLG150DX_GHIDRA_ANALYSIS"),
    dict(key="fs1r_v11", proj_name="FS1R_V11", program="fs1r_v11",
         base=ROMS / "fs1r_sh7044_flash_1.20_256k.bin",   # no v1.1 flash dump exists; the loader part is version independent
         blocks=[("eprom", 0x00200000, 0x200000, ROMS / "fs1r_v11_eprom_cpuview.bin", False)] + SH704X_COMMON,
         vectors=[0x0, 0x8000],
         proj=ROOT / "FS1R_V11_GHIDRA_PROJ", analysis=ROOT / "FS1R_V11_GHIDRA_ANALYSIS"),
]
DEFAULT_KEYS = {"fs1r", "plg150dx"}


def log(msg: str) -> None:
    print(f"[{datetime.now():%H:%M:%S}] {msg}", flush=True)


def analysis_done_path(v) -> Path:
    return v["analysis"] / "ghidra_analysis_done.json"


def is_complete(v) -> bool:
    db = v["analysis"] / "decomp.db"
    if not db.exists():
        return False
    try:
        c = sqlite3.connect(f"file:{db.as_posix()}?mode=ro", uri=True)
        n = c.execute("SELECT COUNT(*) FROM decompilations WHERE status='decompiled'").fetchone()[0]
        c.close()
        return n > 200
    except Exception:
        return False


def project_exists(v) -> bool:
    return (v["proj"] / f"{v['proj_name']}.gpr").exists() and (v["proj"] / f"{v['proj_name']}.rep").exists()


def count_functions(program) -> int:
    return sum(1 for _ in program.getFunctionManager().getFunctions(True))


def init_db(path: Path) -> sqlite3.Connection:
    path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(path))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS decompilations (
            address INTEGER PRIMARY KEY, name TEXT, size INTEGER,
            raw_decomp TEXT, status TEXT DEFAULT 'pending', error TEXT,
            decomp_time_ms INTEGER, created_at TEXT DEFAULT (datetime('now')))""")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS progress (
            id INTEGER PRIMARY KEY CHECK (id = 1), total_functions INTEGER,
            completed INTEGER DEFAULT 0, errors INTEGER DEFAULT 0, updated_at TEXT)""")
    conn.execute("INSERT OR IGNORE INTO progress (id, total_functions) VALUES (1, 0)")
    conn.commit()
    return conn


def build_memory(api, program, v):
    """Add the extra memory blocks and mark the vector tables (only on first import)."""
    from java.io import FileInputStream
    from ghidra.program.model.data import Pointer32DataType
    from ghidra.util.task import ConsoleTaskMonitor

    mem = program.getMemory()
    mon = ConsoleTaskMonitor()
    first = mem.getBlocks()[0]
    first.setName("flash" if v["key"].startswith("fs1r") else "rom")
    first.setWrite(False)
    first.setExecute(True)
    for name, start, size, file, volatile in v["blocks"]:
        if mem.getBlock(name) is not None:
            continue
        addr = api.toAddr(f"0x{start:x}")  # string form: 0xFFFFxxxx overflows the Java int overload
        if file is not None:
            blk = mem.createInitializedBlock(name, addr, FileInputStream(str(file)), size, mon, False)
            blk.setWrite(False)
            blk.setExecute(True)
        else:
            blk = mem.createUninitializedBlock(name, addr, size, False)
            blk.setWrite(True)
            blk.setExecute(False)
            blk.setVolatile(volatile)
        blk.setRead(True)
    # SH-2 exception vector table(s): 256 x 32-bit; entries 1 and 3 are stack pointers, everything else is code.
    ptr = Pointer32DataType()
    nfun = 0
    for tbl in v["vectors"]:
        for i in range(256):
            a = api.toAddr(f"0x{tbl + i * 4:x}")
            try:
                api.createData(a, ptr)
            except Exception:
                pass
            if i in (1, 3):
                continue
            target = mem.getInt(a) & 0xFFFFFFFF
            blk = mem.getBlock(api.toAddr(f"0x{target:x}"))
            if blk is not None and blk.isInitialized() and blk.isExecute() and (target & 1) == 0:
                if api.getFunctionAt(api.toAddr(f"0x{target:x}")) is None:
                    if api.createFunction(api.toAddr(f"0x{target:x}"), None) is not None:
                        nfun += 1
    names = [str(b.getName()) + "@" + str(b.getStart()) for b in mem.getBlocks()]
    log(f"  memory: {names}, {nfun} vector targets")


def analyze(program, v):
    from ghidra.app.plugin.core.analysis import AutoAnalysisManager
    from ghidra.app.script import GhidraScriptUtil
    from ghidra.util.task import ConsoleTaskMonitor

    mgr = AutoAnalysisManager.getAnalysisManager(program)
    GhidraScriptUtil.acquireBundleHostReference()
    t0 = time.time()
    try:
        mgr.initializeOptions()
        mgr.reAnalyzeAll(None)
        mgr.startAnalysis(ConsoleTaskMonitor())
    finally:
        GhidraScriptUtil.releaseBundleHostReference()
    n = count_functions(program)
    analysis_done_path(v).parent.mkdir(parents=True, exist_ok=True)
    analysis_done_path(v).write_text(json.dumps({
        "status": "complete", "binary": str(v["base"]), "project_dir": str(v["proj"]),
        "functions": n, "duration_seconds": round(time.time() - t0, 1),
        "completed_at": datetime.now().isoformat()}, indent=2))
    log(f"  analysis done: {n} functions in {timedelta(seconds=int(time.time()-t0))}")


def decompile(program, v):
    from ghidra.app.decompiler import DecompInterface
    from ghidra.util.task import ConsoleTaskMonitor

    conn = init_db(v["analysis"] / "decomp.db")
    try:
        done = {r[0] for r in conn.execute("SELECT address FROM decompilations WHERE status='decompiled'")}
        funcs = list(program.getFunctionManager().getFunctions(True))
        pending = [f for f in funcs if int(f.getEntryPoint().getOffset()) not in done]
        conn.execute("UPDATE progress SET total_functions=? WHERE id=1", (len(funcs),))
        conn.commit()
        log(f"  decompile: {len(pending)} pending of {len(funcs)}")
        if pending:
            from ghidra.app.decompiler import DecompileOptions
            opt = DecompileOptions()
            opt.setRespectReadOnly(True)     # fold literal-pool loads from read-only ROM into constants
            di = DecompInterface()
            di.setOptions(opt)
            di.openProgram(program)
            cmon = ConsoleTaskMonitor()
            ok = err = 0
            t_start = time.time()
            try:
                for func in pending:
                    addr = int(func.getEntryPoint().getOffset())
                    name = str(func.getName())
                    size = int(func.getBody().getNumAddresses())
                    t = time.time()
                    try:
                        res = di.decompileFunction(func, max(20, min(90, size // 100)), cmon)
                        ms = int((time.time() - t) * 1000)
                        if res.decompileCompleted():
                            conn.execute("INSERT OR REPLACE INTO decompilations "
                                "(address,name,size,raw_decomp,status,decomp_time_ms,created_at) "
                                "VALUES (?,?,?,?,'decompiled',?,datetime('now'))",
                                (addr, name, size, str(res.getDecompiledFunction().getC()), ms))
                            ok += 1
                        else:
                            conn.execute("INSERT OR REPLACE INTO decompilations "
                                "(address,name,size,status,error,decomp_time_ms,created_at) "
                                "VALUES (?,?,?,'error',?,?,datetime('now'))",
                                (addr, name, size, res.getErrorMessage() or "unknown", ms))
                            err += 1
                    except Exception as e:
                        conn.execute("INSERT OR REPLACE INTO decompilations "
                            "(address,name,size,status,error,decomp_time_ms,created_at) "
                            "VALUES (?,?,?,'error',?,?,datetime('now'))",
                            (addr, name, size, str(e), int((time.time() - t) * 1000)))
                        err += 1
                    if (ok + err) % 500 == 0:
                        conn.execute("UPDATE progress SET completed=?, errors=?, updated_at=datetime('now') WHERE id=1",
                                     (len(done) + ok, err))
                        conn.commit()
                        el = time.time() - t_start
                        rate = (ok + err) / el
                        left = (len(pending) - ok - err) / max(rate, 0.01)
                        log(f"  {ok+err}/{len(pending)} ({err} err), {rate:.1f}/s, eta {timedelta(seconds=int(left))}")
            finally:
                di.dispose()
            conn.execute("UPDATE progress SET completed=?, errors=?, updated_at=datetime('now') WHERE id=1",
                         (len(done) + ok, err))
            conn.commit()
            log(f"  decompile done: {ok} ok, {err} err")
        (v["analysis"] / "decompile_done.json").write_text(json.dumps({
            "status": "complete", "project_dir": str(v["proj"]),
            "db": str(v["analysis"] / "decomp.db"), "total": len(funcs),
            "completed_at": datetime.now().isoformat()}, indent=2))
    finally:
        conn.close()


def process(v, redecomp=False):
    import pyghidra
    v["analysis"].mkdir(parents=True, exist_ok=True)
    first_import = not project_exists(v)
    log(f"{v['key']}: {'IMPORT+ANALYZE' if first_import else 'open existing'} -> decompile")
    with pyghidra.open_program(
        str(v["base"]) if first_import else None,
        project_location=str(v["proj"]), project_name=v["proj_name"],
        analyze=False, language=LANG if first_import else None, compiler="default" if first_import else None,
        program_name=v["base"].name if not first_import else None,   # the domain file keeps the imported file's name
        nested_project_location=False,
    ) as flat_api:
        program = flat_api.getCurrentProgram()
        if first_import:
            build_memory(flat_api, program, v)
        for b in program.getMemory().getBlocks():   # ROM must be read-only or the decompiler shows DAT_xxx instead of constants
            if b.isInitialized() and b.isWrite():
                b.setWrite(False)
                log(f"  {b.getName()}: cleared write flag")
        if redecomp:
            db = v["analysis"] / "decomp.db"
            if db.exists():
                c = sqlite3.connect(str(db)); c.execute("DELETE FROM decompilations"); c.commit(); c.close()
                log("  cleared decomp.db")
        if analysis_done_path(v).exists() and count_functions(program) > 200:
            log("  analysis already done, skipping")
        else:
            analyze(program, v)
        decompile(program, v)
    log(f"{v['key']}: complete")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="", help="comma list of keys: fs1r,plg150dx,fs1r_v11")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--redecomp", action="store_true", help="throw away decomp.db and decompile again")
    args = ap.parse_args()
    keys = {k.strip() for k in args.only.split(",") if k.strip()} or DEFAULT_KEYS
    todo = [v for v in VERSIONS if v["key"] in keys]
    pending = []
    for v in todo:
        if is_complete(v) and not args.redecomp:
            print(f"  {v['key']:<9} DONE     -> {v['analysis'] / 'decomp.db'}")
        elif not v["base"].exists():
            print(f"  {v['key']:<9} MISSING  -> {v['base']}")
        else:
            state = "skip" if analysis_done_path(v).exists() else "yes"
            print(f"  {v['key']:<9} PENDING  -> analyze={state}, decompile=yes")
            pending.append(v)
    if args.dry_run or not pending:
        return
    import pyghidra
    log("Starting pyghidra JVM...")
    pyghidra.start(install_dir=GHIDRA_INSTALL_DIR)
    t0 = time.time()
    for v in pending:
        process(v, args.redecomp)
    print(f"\nAll done in {timedelta(seconds=int(time.time() - t0))}.")


if __name__ == "__main__":
    main()
