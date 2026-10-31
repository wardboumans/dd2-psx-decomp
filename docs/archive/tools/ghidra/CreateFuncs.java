// Creates functions at an explicit list of addresses that are already disassembled and in no function.
// Args: <dry|apply> <address>...
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class CreateFuncs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        boolean apply = a[0].equals("apply");
        for (int i = 1; i < a.length; i++) {
            Address at = toAddr(a[i]);
            if ((at.getOffset() & 3) != 0 || getInstructionAt(at) == null || getFunctionContaining(at) != null) {
                println("skip " + at + ": not an aligned free instruction");
                continue;
            }
            if (!apply) {
                println("would create function at " + at + " (" + getInstructionAt(at) + ")");
                continue;
            }
            Function f = createFunction(at, null);
            println("created " + (f == null ? "nothing" : f.getName() + " " + f.getBody().getNumAddresses() + " bytes") + " at " + at);
        }
        println("function count " + currentProgram.getFunctionManager().getFunctionCount());
    }
}
