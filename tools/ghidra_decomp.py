"""Decompile FS1R flash functions headless with Ghidra, readonly folding on so the EPROM tables fold in.

    python tools/ghidra_decomp.py FUN_0000d050 [more...]   or   0xd050      -> /tmp/gh/<name>.c and stdout

Needs Ghidra at /opt/ghidra, pyghidra (pip), and FS1R_DISASM beside this repo. The signature edit on
FUN_00203000 (the signed divide, r1 / r0) is what makes the conversion maths readable; the cached
decomp.db in FS1R_DISASM lost it, so every "/ 889" there reads as a bare call. The transaction is
rolled back on exit, the shared project is never written."""
import sys, io, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import pyghidra
from pyghidra.launcher import HeadlessPyGhidraLauncher
l = HeadlessPyGhidraLauncher(install_dir="/opt/ghidra"); l.add_vmargs("-Duser.name=Home"); l.start()
PROJ = "/work/FS1R_DISASM/FS1R_GHIDRA_PROJ"
os.makedirs("/tmp/gh", exist_ok=True)
with pyghidra.open_program(None, project_location=PROJ, project_name="FS1R",
                           program_name="fs1r_sh7044_flash_1.20_256k.bin", analyze=False, nested_project_location=False) as api:
    p = api.getCurrentProgram()
    # FUN_00203000 is the signed divide with a custom convention: r1 / r0 -> r0. Ghidra drops its
    # arguments without this, which is why every conversion reads as FUN_00203000() with no operands.
    from ghidra.program.model.listing import ParameterImpl, ReturnParameterImpl
    from ghidra.program.model.listing.Function import FunctionUpdateType
    from ghidra.program.model.data import IntegerDataType
    from ghidra.program.model.symbol import SourceType
    tx = p.startTransaction("sdiv sig")
    try:
        dv = api.getFunctionAt(api.toAddr("0x203000"))
        r0, r1 = p.getRegister("r0"), p.getRegister("r1")
        dv.updateFunction(None, ReturnParameterImpl(IntegerDataType.dataType, r0, p),
                          FunctionUpdateType.CUSTOM_STORAGE, True, SourceType.USER_DEFINED,
                          [ParameterImpl("num", IntegerDataType.dataType, r1, p), ParameterImpl("den", IntegerDataType.dataType, r0, p)])
        dv.setName("sdiv", SourceType.USER_DEFINED)
        from ghidra.app.decompiler import DecompInterface, DecompileOptions
        from ghidra.util.task import ConsoleTaskMonitor
        opt = DecompileOptions(); opt.setRespectReadOnly(True)
        di = DecompInterface(); di.setOptions(opt); di.openProgram(p)
        for name in sys.argv[1:]:
            addr = int(name.split('_')[-1], 16) if name.startswith('FUN_') else int(name, 16)
            f = api.getFunctionAt(api.toAddr(f"0x{addr:x}"))
            if f is None:
                f = api.createFunction(api.toAddr(f"0x{addr:x}"), f"FUN_{addr:08x}")
            r = di.decompileFunction(f, 90, ConsoleTaskMonitor())
            c = r.getDecompiledFunction().getC() if r.getDecompiledFunction() else "DECOMPILE FAILED " + str(r.getErrorMessage())
            open(f"/tmp/gh/{f.getName()}.c", "w").write(c)
            print("=====", f.getName()); print(c)
    except Exception as e:
        import traceback; traceback.print_exc(file=sys.stdout)
    finally:
        di.dispose()
        p.endTransaction(tx, False)
