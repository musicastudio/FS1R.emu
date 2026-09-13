# Print the instruction listing for address ranges of the FS1R flash program.
#   python tools/ghidra_disasm.py 0x10dc4-0x10efe [0x1df96-0x1dfce ...]
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import pyghidra
pyghidra.start(install_dir=r"E:\ghidra_12.1.2_PUBLIC")
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1].parent / "FS1R_DISASM"
ranges = [tuple(int(x, 16) for x in a.split("-")) for a in sys.argv[1:]]
with pyghidra.open_program(None, project_location=str(ROOT / "FS1R_GHIDRA_PROJ"), project_name="FS1R",
                           program_name="fs1r_sh7044_flash_1.20_256k.bin", analyze=False, nested_project_location=False) as api:
    p = api.getCurrentProgram()
    for lo, hi in ranges:
        print(f"===== {lo:x}-{hi:x}")
        for ins in p.getListing().getInstructions(api.toAddr(f"0x{lo:x}"), True):
            if ins.getAddress().getOffset() >= hi:
                break
            print(f"{ins.getAddress()}  {ins}")
