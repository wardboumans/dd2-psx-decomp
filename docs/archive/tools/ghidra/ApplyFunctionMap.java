// Rename functions from mapping/function_map.json: hand-checked entries and
// high-confidence matches. Renames only: never creates or deletes functions.
// Args: <path to function_map.json>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.SourceType;
import com.google.gson.*;
import java.io.*;
import java.util.*;

public class ApplyFunctionMap extends GhidraScript {

    public void run() throws Exception {
        JsonObject map = JsonParser.parseReader(new FileReader(getScriptArgs()[0])).getAsJsonObject();
        AddressSpace ov1 = currentProgram.getAddressFactory().getAddressSpace("ov1");
        int renamed = 0, skipped = 0;
        for (Map.Entry<String, JsonElement> e : map.entrySet()) {
            JsonObject v = e.getValue().getAsJsonObject();
            String evidence = v.get("evidence").getAsString();
            boolean checked = evidence.equals("manual") || evidence.equals("graph-checked")
                || !v.get("status").getAsString().equals("mapped");
            if (!checked && !v.get("confidence").getAsString().equals("high")) { skipped++; continue; }
            String k = e.getKey();
            long off = Long.parseLong(k.substring(k.indexOf(':') + 1), 16);
            Address a = k.startsWith("RESULTS:") ? ov1.getAddress(off) : toAddr(off);
            Function f = getFunctionAt(a);
            if (f == null) { println("no function at " + k + " (" + v.get("name").getAsString() + ")"); continue; }
            String name = v.get("name").getAsString();
            if (!f.getName().equals(name)) {
                f.setName(name, SourceType.USER_DEFINED);
                renamed++;
            }
        }
        println("renamed " + renamed + ", low confidence skipped " + skipped);
    }
}
