// Lists every load/store whose displacement is one of the given values (read-only).
// Args: <output file> <disp> [disp ...]   (hex, may be negative: -0x5da0)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class FindDisp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Set<Long> want = new HashSet<>();
        for (int i = 1; i < args.length; i++) {
            String s = args[i];
            boolean neg = s.startsWith("-");
            if (neg) s = s.substring(1);
            long v = Long.parseLong(s.replace("0x", ""), 16);
            want.add(neg ? -v : v);
        }
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                String m = ins.getMnemonicString();
                if (!m.matches("_?(lw|sw|lh|sh|lb|sb|lhu|lbu|lwl|lwr|swl|swr|lwc2|swc2)"))
                    continue;
                if (ins.getNumOperands() < 2)
                    continue;
                for (Object o : ins.getOpObjects(1)) {
                    if (o instanceof Scalar && want.contains(((Scalar) o).getSignedValue())) {
                        Function f = getFunctionContaining(ins.getAddress());
                        out.println(ins.getAddress() + " " + (f == null ? "?" : f.getName()) + " " + ins);
                    }
                }
            }
        }
    }
}
