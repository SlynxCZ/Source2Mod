#!/usr/bin/env python3
# Generates source2mod/addresses.inc and src/sourcepawn/addresses_list.h from
# the SDK's ISource2ModAddresses.h. Usage: sp_addresses.py [sdk-dir]
# (default $SOURCE2MOD_SDK).
import os, re, sys
root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sdk = sys.argv[1] if len(sys.argv) > 1 else os.environ["SOURCE2MOD_SDK"]
src = open(os.path.join(sdk, "public/source2mod/ISource2ModAddresses.h")).read()
start = src.index("class ISource2ModAddresses")
names = re.findall(r"virtual\s+(\w+)_t\s+(\w+)\(\)\s*=\s*0;", src[start:])
names = [n for t, n in names if t == n]
inc = ["/**",
 " * Source2Mod SourcePawn API: the core's resolved game functions",
 " * (ISource2ModAddresses, the ADDR_* getters), for Source2ModFunction and",
 " * Source2ModHook.FromAddress. The list follows the C++ interface; the core",
 " * resolves them from its own gamedata, so a script never needs a signature",
 " * for these.",
 " */",
 "",
 "#if defined _source2mod_addresses_included",
 " #endinput",
 "#endif",
 "#define _source2mod_addresses_included",
 "",
 "enum Source2ModAddress",
 "{"]
for i, n in enumerate(names):
    inc.append(f"\tAddress_{n}{' = 0' if i == 0 else ''},")
inc.append("\tAddress_Count")
inc.append("};")
inc += ["",
 "methodmap Source2ModAddresses",
 "{",
 "\t/** Whether the core resolved this function on the running game. */",
 "\tpublic static native bool IsResolved(Source2ModAddress address);",
 "}",
 ""]
open(os.path.join(root, "configs/addons/source2mod/scripting/include/source2mod/addresses.inc"), "w", newline="\n").write("\n".join(inc))
h = ["// The ISource2ModAddresses getters, in the order of source2mod/addresses.inc's",
     "// Source2ModAddress enum. Regenerate both together when the interface changes.",
     "#define SOURCE2MOD_SP_ADDRESSES(X) \\"]
h += [f"    X({n}) \\" for n in names]
h.append("")
open(os.path.join(root, "src/sourcepawn/addresses_list.h"), "w", newline="\n").write("\n".join(h))
print(len(names))
