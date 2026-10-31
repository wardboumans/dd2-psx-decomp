// Lists indirect jumps (jr other than $ra) that have no COMPUTED_JUMP references, with the jump table and entry
// count read from the usual gcc pattern (sltiu bound, lui hi / lw lo). Read-only.
// Args: <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;

public class FindJumpTables extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
            if (!ins.getMnemonicString().equals("jr") || ins.toString().contains("ra"))
                continue;
            boolean has = false;
            for (Reference r : ins.getReferencesFrom())
                if (r.getReferenceType().isComputed() && r.getReferenceType().isJump())
                    has = true;
            if (has)
                continue;
            Function f = getFunctionContaining(ins.getAddress());
            long hi = -1, lo = 0, count = -1;
            String lwReg = null;
            Instruction p = ins.getPrevious();
            for (int i = 0; i < 24 && p != null; i++, p = p.getPrevious()) {
                String m = p.getMnemonicString();
                if (m.equals("lw") && lwReg == null && hi < 0) {
                    Object[] o = p.getOpObjects(1);
                    for (Object x : o)
                        if (x instanceof Scalar) lo = ((Scalar) x).getSignedValue();
                        else lwReg = x.toString();
                } else if (m.equals("lui") && lwReg != null && hi < 0 && p.getOpObjects(0)[0].toString().equals(lwReg)) {
                    hi = ((Scalar) p.getOpObjects(1)[0]).getUnsignedValue();
                } else if (m.equals("sltiu") && count < 0) {
                    count = ((Scalar) p.getOpObjects(2)[0]).getUnsignedValue();
                }
            }
            String table = hi >= 0 ? Long.toHexString(((hi << 16) + lo) & 0xffffffffL) : "?";
            out.println(ins.getAddress() + "\t" + (f == null ? "-" : f.getName()) + "\t" + table + "\t" + count + "\t" + ins);
        }
        out.close();
    }
}
