# Dump every switch jump table in the FS1R program: case index -> target address -> function name.
import sys, io, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import pyghidra
pyghidra.start(install_dir=r"E:\ghidra_12.1.2_PUBLIC")
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1].parent / "FS1R_DISASM"
out = {}
with pyghidra.open_program(None, project_location=str(ROOT / "FS1R_GHIDRA_PROJ"), project_name="FS1R",
                           program_name="fs1r_sh7044_flash_1.20_256k.bin", analyze=False, nested_project_location=False) as api:
    p = api.getCurrentProgram()
    st = p.getSymbolTable(); rm = p.getReferenceManager(); fm = p.getFunctionManager()
    for s in st.getAllSymbols(False):
        nm = s.getName()
        if not nm.startswith("switchD_"):
            continue
        a = s.getAddress()
        refs = list(rm.getReferencesFrom(a))
        targets = []
        for r in refs:
            if r.getReferenceType().isJump() or r.getReferenceType().isComputed():
                targets.append(int(r.getToAddress().getOffset()))
        # the jump table entries are also computed refs from the switch instruction; order by reference index is lost,
        # so read the table data from the switchdata symbol if present
        entry = {"switch": int(a.getOffset()), "targets_from_refs": sorted(set(targets))}
        out[nm] = entry
    # switch data tables: symbols named switchdataD_xxxx; read consecutive pointer data
    from ghidra.program.model.data import Pointer
    for s in st.getAllSymbols(False):
        nm = s.getName()
        if not nm.startswith("switchdataD_"):
            continue
        a = s.getAddress(); lst = p.getListing(); entries = []
        d = lst.getDataAt(a)
        while d is not None and len(entries) < 512:
            dt = d.getDataType()
            v = d.getValue()
            if v is None or not hasattr(v, "getOffset"):
                # could be a short offset table
                try:
                    entries.append(int(d.getValue().longValue()))
                except Exception:
                    break
            else:
                entries.append(int(v.getOffset()))
            nxt = lst.getDataAfter(d.getAddress())
            if nxt is None or nxt.getAddress() != d.getAddress().add(d.getLength()):
                break
            d = nxt
            if not str(d.getDataType()).lower().startswith(str(dt).lower()[:4]):
                break
        names = []
        for t in entries:
            f = fm.getFunctionContaining(api.toAddr(f"0x{t:x}"))
            names.append([t, f.getName() if f else None])
        out[nm] = {"table": int(a.getOffset()), "entries": names}
json.dump(out, open(ROOT / "notes" / "switch_tables.json", "w"), indent=1)
print("switches:", sum(1 for k in out if k.startswith("switchD_")), "tables:", sum(1 for k in out if k.startswith("switchdataD_")))
for k, v in out.items():
    if k.startswith("switchdataD_") and len(v["entries"]) > 8:
        print(k, "entries", len(v["entries"]), "first", [(hex(t), n) for t, n in v["entries"][:4]])
