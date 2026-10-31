// Export every function in initialized memory to mapping/psx_functions.json:
// address, name, block, size, instruction count, referenced strings, numeric
// constants (non-memory immediates), data references, callees and callers.
// Read-only: changes nothing in the program.
// Args: <path to psx_functions.json>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.lang.OperandType;
import java.io.*;
import java.util.*;

public class ExportPsxFuncs extends GhidraScript {

    String esc(String s) {
        StringBuilder b = new StringBuilder();
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20 || c > 0x7e) b.append(String.format("\\u%04x", (int) c));
            else b.append(c);
        }
        return b.toString();
    }

    String list(Collection<?> items, boolean quote) {
        StringBuilder b = new StringBuilder();
        for (Object x : items) {
            if (b.length() > 0) b.append(',');
            b.append(quote ? "\"" + x + "\"" : x.toString());
        }
        return b.toString();
    }

    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();
        PrintWriter out = new PrintWriter(new FileWriter(getScriptArgs()[0]));
        out.println("[");
        boolean first = true;
        int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            MemoryBlock blk = mem.getBlock(f.getEntryPoint());
            if (blk == null || !blk.isInitialized()) continue;
            TreeSet<String> strings = new TreeSet<>();
            TreeSet<String> dataRefs = new TreeSet<>();
            TreeSet<Long> consts = new TreeSet<>();
            int instrs = 0;
            for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
                instrs++;
                for (Reference r : ins.getReferencesFrom()) {
                    if (r.getReferenceType().isFlow()) continue;
                    Address to = r.getToAddress();
                    Data d = listing.getDataContaining(to);
                    if (d != null && d.hasStringValue()) strings.add(esc(String.valueOf(d.getValue())));
                    else if (mem.contains(to)) dataRefs.add(to.toString());
                }
                if (ins.getMnemonicString().equals("lui") || ins.getReferencesFrom().length > 0) continue;
                for (int i = 0; i < ins.getNumOperands(); i++) {
                    int t = ins.getOperandType(i);
                    if ((t & OperandType.SCALAR) == 0 || (t & OperandType.DYNAMIC) != 0) continue;
                    for (Object o : ins.getOpObjects(i)) {
                        if (o instanceof Scalar) {
                            long v = ((Scalar) o).getSignedValue();
                            if (Math.abs(v) > 16) consts.add(v);
                        }
                    }
                }
            }
            List<String> calls = new ArrayList<>();
            for (Function c : f.getCalledFunctions(monitor))
                calls.add("[\"" + c.getEntryPoint() + "\",\"" + esc(c.getName()) + "\"]");
            List<String> callers = new ArrayList<>();
            for (Function c : f.getCallingFunctions(monitor)) callers.add(c.getEntryPoint().toString());
            List<String> quoted = new ArrayList<>();
            for (String x : strings) quoted.add(x);
            if (!first) out.println(",");
            first = false;
            out.print("{\"address\":\"" + f.getEntryPoint() + "\",\"name\":\"" + esc(f.getName()) + "\",\"block\":\"" + esc(blk.getName())
                + "\",\"size\":" + f.getBody().getNumAddresses() + ",\"instructions\":" + instrs
                + ",\"strings\":[" + list(quoted, true) + "],\"constants\":[" + list(consts, false)
                + "],\"data_refs\":[" + list(dataRefs, true) + "],\"calls\":[" + list(calls, false)
                + "],\"callers\":[" + list(callers, true) + "]}");
            n++;
        }
        out.println("\n]");
        out.close();
        println("exported " + n);
    }
}
