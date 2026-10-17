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

64-bit fields are two-cell methods (field(int v[2]) / Set_field), fixed
arrays (SCHEMA_FIELD_POINTER) element methods (field(index) / Set_field(index,
value) / field_Count()).

Next to classes.inc it writes enums.inc: the schema enums plugin authors use
(ENUMS below, from schema/entity/enums/*.h) as #defines under their C++
names. A name that is already defined (consts/*.inc or an earlier enum) is
left out, with a note.

Entity classes (CEntityInstance and everything under it) hold an entity
handle (index + serial); every other class (services, the scene node, ...) holds an object
reference the core hands out, valid as long as its owner.

    python3 tools/sp_schemagen/sp_schemagen.py <Source2Mod-sdk> <output .inc>
"""
import os
import re
import sys

INT_TYPES = {
    "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
    "char", "int", "uint", "uint32", "int32", "uint16", "int16", "uint8", "int8",
    "CEntityIndex", "CUtlStringToken", "WorldGroupId_t", "AttachmentHandle_t", "ModelConfigHandle_t",
    "HSequence", "GameTick_t", "CPlayerSlot", "ParticleIndex_t", "HitGroup_t",
    # s2sdk's own enums (not in schema/entity/enums)
    "MoveType_t", "MoveCollide_t", "SolidType_t", "DamageTypes_t", "Class_T", "soundlevel_t",
    "RenderMode_t", "RenderFx_t", "ObserverMode_t", "EntityDissolveType_t", "ItemFlagTypes_t",
}
# Do not fit a cell: two-cell accessors (GetInt64 / SetInt64).
INT64_TYPES = {"int64_t", "uint64_t", "int64", "uint64", "TakeDamageFlags_t"}

# Schema enums written to enums.inc: the ones plugins use. The rest (anim
# graph, particles, sound mixer, GC messages, ...) stay out.
ENUMS = [
    "ETeam", "EHitGroup", "CSPlayerState", "PlayerConnectedState",
    "gear_slot_t", "loadout_slot_t", "CSWeaponType", "CSWeaponCategory", "CSWeaponMode",
    "CSWeaponSilencerType", "GrenadeType_t", "EKillTypes_t", "WeaponSwitchReason_t",
    "MedalRank_t", "EInButtonState", "WaterLevel_t", "DoorState_t", "ShakeCommand_t",
    "ViewFadeMode_t", "ParticleAttachment_t", "PointWorldTextJustifyHorizontal_t",
    "PointWorldTextJustifyVertical_t", "PointWorldTextReorientMode_t", "vote_create_failed_t",
    "ChatIgnoreType_t", "BeamType_t", "ModifyDamageReturn_t",
    "ObserverInterpState_t", "CSPlayerBlockingUseAction_t",
    "Disposition_t", "Hull_t", "TrainVelocityType_t", "TrainOrientationType_t",
    "FuncDoorSpawnPos_t", "PropDoorRotatingSpawnPos_t", "PropDoorRotatingOpenDirection_e",
    "TimelineCompression_t", "QuestProgress__Reason",
    "AmmoPosition_t", "Explosions", "BrushSolidities_e", "filter_t", "EOverrideBlockLOS_t",
    "ValueRemapperInputType_t", "ValueRemapperOutputType_t", "ValueRemapperHapticsType_t",
    "ValueRemapperMomentumType_t", "ValueRemapperRatchetType_t",
]
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
    """{name: (underlying type, [(value name, value)])} from the enum headers."""
    enums = {}
    head_re = re.compile(r"enum\s+class\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_]+)")
    value_re = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?(?:0x[0-9A-Fa-f]+|\d+))\s*,?")
    for name in os.listdir(enums_dir):
        if not name.endswith(".h"):
            continue
        underlying, values = "int32_t", []
        with open(os.path.join(enums_dir, name), encoding="utf-8", errors="replace") as f:
            for line in f:
                m = head_re.search(line)
                if m:
                    underlying = m.group(2)
                    continue
                m = value_re.match(line)
                if m:
                    values.append((m.group(1), int(m.group(2), 0)))
        enums[name[:-2]] = (underlying, values)
    return enums


def is_int64(t, enums):
    return t in INT64_TYPES or (t in enums and enums[t][0] in ("int64_t", "uint64_t"))


def collect_defined(scripting_dir):
    """Every #define name in the hand-written consts/*.inc, so a schema enum
    value never redefines one."""
    names = set()
    consts = os.path.join(scripting_dir, "consts")
    if os.path.isdir(consts):
        for name in os.listdir(consts):
            with open(os.path.join(consts, name), encoding="utf-8") as f:
                for line in f:
                    m = re.match(r"#define\s+([A-Za-z_][A-Za-z0-9_]*)", line)
                    if m:
                        names.add(m.group(1))
    return names


def generate_enums(enums, out, defined):
    lines = [
        "// Generated by tools/sp_schemagen/sp_schemagen.py from the Source2Mod SDK's",
        "// schema enums (schema/entity/enums). Do not edit; regenerate when they change.",
        "// The C++ names, as #defines: the natives take them as int.",
        "",
        "#if defined _source2mod_schema_enums_included",
        " #endinput",
        "#endif",
        "#define _source2mod_schema_enums_included",
        "",
    ]
    written = left_out = 0
    for name in ENUMS:
        if name not in enums:
            continue
        underlying, values = enums[name]
        lines.append(f"// {name} ({underlying})")
        for vname, value in values:
            if vname in defined:
                lines.append(f"// {vname} = {value}: already defined")
                left_out += 1
                continue
            if value > 0x7FFFFFFF or value < -0x80000000:
                lines.append(f"// {vname} = {value}: does not fit a cell")
                left_out += 1
                continue
            defined.add(vname)
            lines.append(f"#define {vname} {value}")
            written += 1
        lines.append("")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    missing = [n for n in ENUMS if n not in enums]
    print(f"{written} enum values, {left_out} left out -> {out}")
    if missing:
        print("not in this SDK's schema enums: " + ", ".join(missing))


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
        return array_for(acc, t, field, classes, enums)

    if is_int64(t, enums):
        return [f"\tpublic void {field}(int value[2]) {{ Source2ModSchema.GetInt64(this, {acc}, value); }}",
                f"\tpublic void Set_{field}(const int value[2]) {{ Source2ModSchema.SetInt64(this, {acc}, value); }}"]

    if t in INT_TYPES or t in enums:
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


def array_for(acc, t, field, classes, enums):
    """Element methods for a fixed array field, or None."""
    count = f"\tpublic int {field}_Count() {{ return Source2ModSchema.GetArraySize({acc}); }}"
    if is_int64(t, enums):
        return [f"\tpublic void {field}(int index, int value[2]) {{ Source2ModSchema.GetArrayInt64(this, {acc}, index, value); }}",
                f"\tpublic void Set_{field}(int index, const int value[2]) {{ Source2ModSchema.SetArrayInt64(this, {acc}, index, value); }}",
                count]
    if t in INT_TYPES or t in enums:
        return [f"\tpublic int {field}(int index) {{ return Source2ModSchema.GetArrayInt(this, {acc}, index); }}",
                f"\tpublic void Set_{field}(int index, int value) {{ Source2ModSchema.SetArrayInt(this, {acc}, index, value); }}",
                count]
    if t == "bool":
        return [f"\tpublic bool {field}(int index) {{ return Source2ModSchema.GetArrayInt(this, {acc}, index) != 0; }}",
                f"\tpublic void Set_{field}(int index, bool value) {{ Source2ModSchema.SetArrayInt(this, {acc}, index, value); }}",
                count]
    if t in FLOAT_TYPES:
        return [f"\tpublic float {field}(int index) {{ return Source2ModSchema.GetArrayFloat(this, {acc}, index); }}",
                f"\tpublic void Set_{field}(int index, float value) {{ Source2ModSchema.SetArrayFloat(this, {acc}, index, value); }}",
                count]
    if t in VECTOR_TYPES:
        return [f"\tpublic void {field}(int index, float vec[3]) {{ Source2ModSchema.GetArrayVector(this, {acc}, index, vec); }}",
                f"\tpublic void Set_{field}(int index, const float vec[3]) {{ Source2ModSchema.SetArrayVector(this, {acc}, index, vec); }}",
                count]
    if t in STRING_TYPES:
        return [f"\tpublic int {field}(int index, char[] buffer, int maxlen) {{ return Source2ModSchema.GetArrayString(this, {acc}, index, buffer, maxlen); }}",
                count]
    m = re.fullmatch(r"CHandle<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>", t)
    if m or t == "CEntityHandle":
        target = m.group(1) if m else "CEntityInstance"
        if target not in classes or not is_entity(target, classes):
            target = "CBaseEntity"
        return [f"\tpublic {target} {field}(int index) {{ return view_as<{target}>(Source2ModSchema.GetArrayHandle(this, {acc}, index)); }}",
                f"\tpublic void Set_{field}(int index, CEntityInstance value) {{ Source2ModSchema.SetArrayHandle(this, {acc}, index, value); }}",
                count]
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

    scripting = os.path.dirname(os.path.dirname(os.path.abspath(out)))
    generate_enums(enums, os.path.join(os.path.dirname(out), "enums.inc"), collect_defined(scripting))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    generate(sys.argv[1], sys.argv[2])
