// Writes the decompiled C of the functions at the given addresses to a file (read-only).
// Args: <output file> <addr> [addr ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                Function f = getFunctionAt(toAddr(args[i]));
                if (f == null) {
                    out.println("// no function at " + args[i]);
                    continue;
                }
                DecompileResults r = decomp.decompileFunction(f, 60, monitor);
                out.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
                out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
            }
        }
        decomp.dispose();
        println("wrote " + (args.length - 1) + " functions");
    }
}
