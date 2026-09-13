# Create + decompile the sysex-parameter handler snippets reached through the three big jump tables
# (switchdataD_0003de64/78/88), then summarise what each handler reads and which chip event it emits.
import sys, io, json, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import pyghidra
pyghidra.start(install_dir=r"E:\ghidra_12.1.2_PUBLIC")
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1].parent / "FS1R_DISASM"
tables = json.load(open(ROOT / "notes" / "switch_tables.json"))
want = {k: v for k, v in tables.items() if k in ("switchdataD_0003de64", "switchdataD_0003de78", "switchdataD_0003de88")}
out = {}
with pyghidra.open_program(None, project_location=str(ROOT / "FS1R_GHIDRA_PROJ"), project_name="FS1R",
                           program_name="fs1r_sh7044_flash_1.20_256k.bin", analyze=False, nested_project_location=False) as api:
    p = api.getCurrentProgram()
    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.util.task import ConsoleTaskMonitor
    opt = DecompileOptions(); opt.setRespectReadOnly(True)
    di = DecompInterface(); di.setOptions(opt); di.openProgram(p)
    mon = ConsoleTaskMonitor()
    cache = {}
    for tname, t in want.items():
        rows = []
        for idx, (target, fname) in enumerate(t["entries"]):
            a = api.toAddr(f"0x{target:x}")
            if target not in cache:
                f = api.getFunctionAt(a)
                if f is None:
                    api.disassemble(a)
                    f = api.createFunction(a, None)
                if f is None:
                    f = api.getFunctionContaining(a)
                text = None
                if f is not None:
                    r = di.decompileFunction(f, 30, mon)
                    if r.decompileCompleted():
                        text = str(r.getDecompiledFunction().getC())
                cache[target] = (f.getName() if f else None, text)
            name, text = cache[target]
            events = re.findall(r"FUN_000224f8\((0x[0-9a-f]+|\d+)\)", text or "")
            reads = sorted(set(re.findall(r"DAT_0102826[8c] \+ (0x[0-9a-f]+)|DAT_01028274 \+ (0x[0-9a-f]+)", text or "")))
            rows.append({"index": idx, "target": target, "func": name, "events": events,
                         "reads": [x for pair in reads for x in pair if x], "text": text})
        out[tname] = rows
    di.dispose()
json.dump(out, open(ROOT / "notes" / "param_handlers.json", "w"), indent=1)
for tname, rows in out.items():
    print("=====", tname, len(rows))
    for r in rows:
        if r["events"] or r["reads"]:
            print(f"  [{r['index']:3d}] {r['target']:06x} {r['func']}: events {r['events']} reads {r['reads']}")
