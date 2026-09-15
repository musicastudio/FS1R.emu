#!/usr/bin/env python3
"""Decompile the controller-matrix destination handlers.

    python tools/ghidra_ctrl_dests.py            all 48 destinations
    python tools/ghidra_ctrl_dests.py 17 35 39   just these

FUN_00014DDC evaluates a performance's eight controller sets. For each set it sums the enabled source
values, then calls a per-destination handler from the pointer table at flash 0x0003DC04 with
(summed value, depth - 64). Ghidra never made functions at those 48 targets because nothing reaches
them except that table, so this creates them first and then decompiles.

The companion table at EPROM 0x0035C4C4 says whether a destination is per-part (and therefore gated by
the set's part switch at performance common 0x28 + n) or global.
"""
import io
import struct
import sys
from pathlib import Path

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import pyghidra

pyghidra.start(install_dir=r"E:\ghidra_12.1.2_PUBLIC")

ROOT = Path(__file__).resolve().parents[1]
DISASM = ROOT.parent / "FS1R_DISASM"
ROM = DISASM / "roms/fs1r_v120_eprom_cpuview.bin"
FLASH = DISASM / "roms/fs1r_sh7044_flash_1.20_256k.bin"

NAMES = [
    "off", "Insertion Param 1", "Insertion Param 2", "Insertion Param 3", "Insertion Param 4",
    "Insertion Param 5", "Insertion Param 6", "Insertion Param 7", "Insertion Param 8",
    "Insertion Param 9", "Insertion Param 10", "Insertion Param 11", "Insertion Param 12",
    "Insertion Param 13", "Insertion Param 14", "Send Insertion to Reverb", "Send Insertion to Variation",
    "Volume", "Panpot", "Reverb Send", "Variation Send", "Filter Cutoff", "Filter Resonance",
    "Filter EG Depth", "Attack Time", "Decay Time", "Release Time", "PEG Initial Level",
    "PEG Attack Time", "PEG Release Level", "PEG Release Time", "V/N Balance", "Formant", "FM",
    "Pitch Bias", "Amplitude EG Bias", "Frequency Bias", "Voiced Band Width", "Unvoiced Band Width",
    "LFO1 pitch mod", "LFO1 amp mod", "LFO1 frequency mod", "LFO1 filter mod", "LFO1 Speed",
    "LFO2 filter mod", "LFO2 Speed", "Fseq Speed", "Formant scratch",
]


def main():
    wanted = [int(a, 0) for a in sys.argv[1:]] or list(range(48))
    rom = ROM.read_bytes()
    flash = FLASH.read_bytes()
    perPart = struct.unpack(">48h", rom[0x35C4C4 - 0x200000: 0x35C4C4 - 0x200000 + 96])
    handlers = struct.unpack(">48I", flash[0x3DC04: 0x3DC04 + 192])

    with pyghidra.open_program(None, project_location=str(DISASM / "FS1R_GHIDRA_PROJ"),
                               project_name="FS1R",
                               program_name="fs1r_sh7044_flash_1.20_256k.bin",
                               analyze=False, nested_project_location=False) as api:
        program = api.getCurrentProgram()
        from ghidra.app.decompiler import DecompInterface, DecompileOptions
        opt = DecompileOptions()
        opt.setRespectReadOnly(True)
        di = DecompInterface()
        di.setOptions(opt)
        di.openProgram(program)
        made = 0
        for d in wanted:
            addr = api.toAddr(f"0x{handlers[d]:x}")
            if api.getFunctionAt(addr) is None:
                if api.createFunction(addr, f"ctrlDest_{d:02d}") is not None:
                    made += 1
        if made:
            print(f"// created {made} functions from the destination table", file=sys.stderr)
        for d in wanted:
            addr = api.toAddr(f"0x{handlers[d]:x}")
            fn = api.getFunctionAt(addr)
            print(f"===== destination {d} \"{NAMES[d]}\"  handler {handlers[d]:08x}  "
                  f"{'per-part' if perPart[d] else 'global'}")
            if fn is None:
                print("  (no function could be made here)")
                continue
            res = di.decompileFunction(fn, 60, api.getMonitor())
            print(res.getDecompiledFunction().getC() if res.decompileCompleted()
                  else "  " + str(res.getErrorMessage()))
        di.dispose()


if __name__ == "__main__":
    main()
