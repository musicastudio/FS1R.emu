# Probe: block permissions + decompile one function with readonly folding on.
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import pyghidra
pyghidra.start(install_dir=r"E:\ghidra_12.1.2_PUBLIC")
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1].parent / "FS1R_DISASM"
with pyghidra.open_program(None, project_location=str(ROOT / "FS1R_GHIDRA_PROJ"), project_name="FS1R",
                           program_name="fs1r_sh7044_flash_1.20_256k.bin", analyze=False, nested_project_location=False) as api:
    p = api.getCurrentProgram()
    for b in p.getMemory().getBlocks():
        print(b.getName(), b.getStart(), "init", b.isInitialized(), "W", b.isWrite(), "X", b.isExecute(), "vol", b.isVolatile())
    from ghidra.app.decompiler import DecompInterface, DecompileOptions
    from ghidra.util.task import ConsoleTaskMonitor
    opt = DecompileOptions()
    print("respectReadOnly default:", opt.isRespectReadOnly())
    opt.setRespectReadOnly(True)
    di = DecompInterface(); di.setOptions(opt); di.openProgram(p)
    for addr in (0x44c, 0x3b0108):
        f = api.getFunctionAt(api.toAddr(f"0x{addr:x}"))
        r = di.decompileFunction(f, 60, ConsoleTaskMonitor())
        print("=====", f.getName()); print(r.getDecompiledFunction().getC()[:3500])
    di.dispose()
