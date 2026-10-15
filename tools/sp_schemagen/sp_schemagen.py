#!/usr/bin/env python3
"""
Source2Mod: SourcePawn methodmaps for the SDK's schema classes.

Reads the SDK's generated class headers (public/source2mod/schema/entity/
classes/*.h) and writes source2mod/schema/classes.inc: one methodmap per
class, named like the C++ class and deriving from the same base, with a
property per SCHEMA_FIELD the script can use. Every property is a stock over
Source2ModSchema's generic accessors -- no native per field. The C++ helper
methods (Teleport, GiveNamedItem, ...) come from methods.inc next to this
script and are spliced into their class.

Entity classes (CEntityInstance and everything under it) hold an entity
handle (index + serial); every other class (services, the scene node, ...) holds an object
reference the core hands out, valid as long as its owner.

    python3 tools/sp_schemagen/sp_schemagen.py <Source2Mod-sdk> <output .inc>
"""
import os
import re
import sys

INT_TYPES = {
    "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t",
    "char", "int", "uint", "uint32", "int32", "uint16", "int16", "uint8", "int8",
    "CEntityIndex", "CUtlStringToken", "WorldGroupId_t", "AttachmentHandle_t", "ModelConfigHandle_t",
    "HSequence", "GameTick_t", "CPlayerSlot", "ParticleIndex_t", "HitGroup_t",
    # s2sdk's own enums (not in schema/entity/enums)
    "MoveType_t", "MoveCollide_t", "SolidType_t", "DamageTypes_t", "Class_T", "soundlevel_t",
}
FLOAT_TYPES = {"float", "float32", "GameTime_t"}
VECTOR_TYPES = {"Vector", "QAngle", "VectorWS"}
STRING_TYPES = {"CUtlSymbolLarge", "CUtlString", "CGlobalSymbol"}


def split_args(s):
    """'CHandle<A, B>, name' -> ['CHandle<A, B>', 'name'] (top-level commas only)."""
    parts, depth, cur = [], 0, ""
    for ch in s:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    parts.append(cur.strip())
    return parts


def parse_classes(classes_dir):
    classes = {}
    class_re = re.compile(r"^class\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*public\s+([A-Za-z_][A-Za-z0-9_:]*))?\s*$")
    field_re = re.compile(r"^\s*(SCHEMA_FIELD(?:_POINTER)?)\((.*)\);")
    for name in sorted(os.listdir(classes_dir)):
        if not name.endswith(".h"):
            continue
        current = None
        with open(os.path.join(classes_dir, name), encoding="utf-8", errors="replace") as f:
            for line in f:
                m = class_re.match(line.strip())
                if m:
                    current = {"name": m.group(1), "base": m.group(2), "fields": []}
                    classes[current["name"]] = current
                    continue
                m = field_re.match(line)
                if m and current:
                    args = split_args(m.group(2))
                    if len(args) == 2:
                        current["fields"].append((m.group(1), args[0], args[1]))
    return classes


def parse_enums(enums_dir):
    names = set()
    for name in os.listdir(enums_dir):
        if name.endswith(".h"):
            names.add(name[:-2])
    return names


def parse_methods(path):
    """methods.inc: '@Class' starts a block of lines spliced into that methodmap."""
    methods, current = {}, None
    if not os.path.exists(path):
        return methods
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("@"):
                current = line[1:].strip()
                methods.setdefault(current, [])
            elif current is not None:
                methods[current].append(line.rstrip("\n"))
    return methods


def is_entity(cls, classes):
    seen = set()
    while cls and cls not in seen:
        seen.add(cls)
        if cls in ("CEntityInstance", "CBaseEntity"):
            return True
        c = classes.get(cls)
        cls = c["base"] if c else None
    return False


def property_for(owner, macro, ctype, field, classes, enums):
    """Lines of SP for one field, or None when the type has no script form."""
    t = ctype.strip()
    acc = f'"{owner}", "{field}"'

    def prop(sp_type, getter, setter=None):
        lines = [f"\tproperty {sp_type} {field} {{",
                 f"\t\tpublic get() {{ return {getter}; }}"]
        if setter:
            lines.append(f"\t\tpublic set({sp_type} value) {{ {setter}; }}")
        lines.append("\t}")
        return lines

    if macro == "SCHEMA_FIELD_POINTER":
        if t == "char":
            return [f"\tpublic int {field}(char[] buffer, int maxlen) {{ return Source2ModSchema.GetString(this, {acc}, buffer, maxlen); }}",
                    f"\tpublic void Set_{field}(const char[] value) {{ Source2ModSchema.SetString(this, {acc}, value); }}"]
        return None

    if t in INT_TYPES or t in enums or t.endswith("_t") and t in enums:
        return prop("int", f"Source2ModSchema.GetInt(this, {acc})", f"Source2ModSchema.SetInt(this, {acc}, value)")
    if t in FLOAT_TYPES:
        return prop("float", f"Source2ModSchema.GetFloat(this, {acc})", f"Source2ModSchema.SetFloat(this, {acc}, value)")
    if t == "bool":
        return prop("bool", f"Source2ModSchema.GetBool(this, {acc})", f"Source2ModSchema.SetBool(this, {acc}, value)")
    if t in VECTOR_TYPES:
        return [f"\tpublic void {field}(float vec[3]) {{ Source2ModSchema.GetVector(this, {acc}, vec); }}",
                f"\tpublic void Set_{field}(const float vec[3]) {{ Source2ModSchema.SetVector(this, {acc}, vec); }}"]
    if t == "Color":
        return [f"\tpublic void {field}(int color[4]) {{ Source2ModSchema.GetColor(this, {acc}, color); }}",
                f"\tpublic void Set_{field}(const int color[4]) {{ Source2ModSchema.SetColor(this, {acc}, color); }}"]
    if t in STRING_TYPES:
        return [f"\tpublic int {field}(char[] buffer, int maxlen) {{ return Source2ModSchema.GetString(this, {acc}, buffer, maxlen); }}"]

    m = re.fullmatch(r"CHandle<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>", t)
    if m or t == "CEntityHandle":
        target = m.group(1) if m else "CEntityInstance"
        if target not in classes or not is_entity(target, classes):
            target = "CBaseEntity"
        return prop(target, f"view_as<{target}>(Source2ModSchema.GetHandle(this, {acc}))",
                    f"Source2ModSchema.SetHandle(this, {acc}, value)")

    m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s*\*", t)
    if m and m.group(1) in classes:
        target = m.group(1)
        if is_entity(target, classes):
            return prop(target, f"view_as<{target}>(Source2ModSchema.GetEntityPointer(this, {acc}))")
        return prop(target, f"view_as<{target}>(Source2ModSchema.GetObject(this, {acc}))")

    if t in classes and not is_entity(t, classes):
        return prop(t, f"view_as<{t}>(Source2ModSchema.GetObject(this, {acc}))")

    return None


def generate(sdk, out):
    base = os.path.join(sdk, "public", "source2mod", "schema", "entity")
    classes = parse_classes(os.path.join(base, "classes"))
    # A methodmap name must start with an uppercase letter; the lowercase
    # ones are small structs (fogparams_t, ...) left out for now.
    classes = {k: v for k, v in classes.items() if k[0].isupper()}
    enums = parse_enums(os.path.join(base, "enums"))
    methods = parse_methods(os.path.join(os.path.dirname(os.path.abspath(__file__)), "methods.inc"))

    lines = [
        "// Generated by tools/sp_schemagen/sp_schemagen.py from the Source2Mod SDK's",
        "// schema classes. Do not edit; regenerate when the SDK's classes change.",
        "",
        "#if defined _source2mod_schema_classes_included",
        " #endinput",
        "#endif",
        "#define _source2mod_schema_classes_included",
        "",
    ]

    # A methodmap's base has to be defined before it (property types may come later).
    order, done = [], set()

    def visit(n):
        if n in done or n not in classes:
            return
        done.add(n)
        visit(classes[n]["base"])
        order.append(n)

    for n in sorted(classes):
        visit(n)

    fields_out = skipped = 0
    for name in order:
        c = classes[name]
        base_name = c["base"]
        if not base_name or base_name not in classes and base_name != "CEntityInstance":
            base_name = "CEntityInstance" if is_entity(name, classes) else "SchemaObject"
        lines.append(f"methodmap {name} < {base_name}")
        lines.append("{")
        for macro, ctype, field in c["fields"]:
            p = property_for(name, macro, ctype, field, classes, enums)
            if p is None:
                skipped += 1
                continue
            fields_out += 1
            lines.extend(p)
        for extra in methods.get(name, []):
            lines.append(extra)
        lines.append("}")
        lines.append("")

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"{len(classes)} classes, {fields_out} fields, {skipped} skipped (no script form yet) -> {out}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    generate(sys.argv[1], sys.argv[2])
