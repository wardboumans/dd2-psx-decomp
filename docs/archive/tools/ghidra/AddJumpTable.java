// Adds COMPUTED_JUMP references from an indirect jump to the entries of its jump table and lets the
// function body follow them, so the decompiler can recover the switch. Targets must already be
// instructions, inside this function or in no function; no code is disassembled or created.
// Args: <dry|apply> <jr address> <table address> <entry count>
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.SourceType;

import java.util.LinkedHashSet;
import java.util.Set;

public class AddJumpTable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        boolean apply = a[0].equals("apply");
        Address jr = toAddr(a[1]);
        Address table = toAddr(a[2]);
        int count = Integer.parseInt(a[3]);
        Instruction ins = getInstructionAt(jr);
        if (ins == null || !ins.getMnemonicString().startsWith("jr")) {
            println("no jr at " + jr + ": " + ins);
            return;
        }
        Function f = getFunctionContaining(jr);
        Set<Address> targets = new LinkedHashSet<>();
        for (int i = 0; i < count; i++) {
            Address t = toAddr(getInt(table.add(i * 4L)) & 0xffffffffL);
            Function owner = getFunctionContaining(t);
            if (getInstructionAt(t) == null || (owner != null && owner != f)) {
                println("target " + t + " is not free code of " + f + " (owner " + owner + "); stopping");
                return;
            }
            targets.add(t);
        }
        long before = f.getBody().getNumAddresses();
        println((apply ? "adding" : "would add") + " " + targets.size() + " references from " + jr + " (" + ins + ") in "
                + f.getName() + " (" + before + " bytes): " + targets);
        if (apply) {
            for (Address t : targets)
                currentProgram.getReferenceManager().addMemoryReference(jr, t, RefType.COMPUTED_JUMP, SourceType.USER_DEFINED, 0);
            CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
            // The decompiler takes switch targets from a jump-table override, in table order.
            java.util.ArrayList<Address> ordered = new java.util.ArrayList<>();
            for (int i = 0; i < count; i++)
                ordered.add(toAddr(getInt(table.add(i * 4L)) & 0xffffffffL));
            new ghidra.program.model.pcode.JumpTable(jr, ordered, true, 0).writeOverride(f);
            println(f.getName() + " body now " + f.getBody().getNumAddresses() + " bytes, jump table override written");
        }
    }
}
