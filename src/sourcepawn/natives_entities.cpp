/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Entity natives: entity.inc (entity props, entity data, refs) and the
 * entity half of sdktools (CreateEntityByName, DispatchKeyValue/Spawn,
 * AcceptEntityInput + SetVariant*, TeleportEntity, FindEntityByClassname)
 * on Source2Mod's schema access. SourceMod's smn_entities.cpp and the
 * SDKTools extension (AlliedModders LLC) are the reference for behaviour.
 *
 * Entity indices are CS2's; a client index is its controller's entity index
 * (slot + 1), which is SourceMod's rule too. Props are found in the running
 * game's schema by name, on the entity's class and its bases -- Prop_Send and
 * Prop_Data are the same thing here -- and a path "a.b" follows embedded
 * classes and pointers ("m_pGameSceneNode.m_vecAbsOrigin",
 * "m_pWeaponServices.m_hActiveWeapon"). A networked field written through
 * SetEntProp* is marked changed for the owning entity.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "utils/log.h"

#include <source2mod/schema/schema.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CBaseModelEntity.h>
#include <source2mod/schema/entity/classes/CBasePlayerWeapon.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>
#include <source2mod/schema/entity/classes/CCSPlayer_ItemServices.h>

#include <entity2/entitykeyvalues.h>
#include <schemalib/schemaclassinfo.h>
#include <schemalib/schematype.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        // CS2's entity list: 15 bits of index.
        constexpr cell_t kMaxEntities = 32768;
        constexpr uint32_t kRefFlag = 1u << 31;

        CEntityInstance* InstanceAt(cell_t index)
        {
            if (index < 0 || index >= kMaxEntities)
                return nullptr;
            return CBaseEntity::FromIndex<CEntityInstance>(index);
        }

        // An index or a reference (EntIndexToEntRef) to the entity, or nullptr.
        CEntityInstance* Resolve(cell_t entity)
        {
            const uint32_t raw = static_cast<uint32_t>(entity);
            if (raw & kRefFlag)
            {
                const cell_t index = static_cast<cell_t>(raw & 0x7FFF);
                CEntityInstance* inst = InstanceAt(index);
                if (!inst)
                    return nullptr;
                const uint32_t serial = (raw >> 15) & 0xFFFF;
                return (static_cast<uint32_t>(inst->GetRefEHandle().GetSerialNumber()) & 0xFFFF) == serial ? inst : nullptr;
            }
            return InstanceAt(entity);
        }

        CEntityInstance* CheckEntity(IPluginContext* ctx, cell_t entity)
        {
            CEntityInstance* inst = Resolve(entity);
            if (!inst)
            {
                if (static_cast<uint32_t>(entity) & kRefFlag)
                    ctx->ThrowNativeError("Entity %d (%d) is invalid", static_cast<int>(static_cast<uint32_t>(entity) & 0x7FFF), entity);
                else
                    ctx->ThrowNativeError("Entity %d (%d) is invalid", entity, entity);
            }
            return inst;
        }

        cell_t IndexOf(CEntityInstance* inst)
        {
            return inst ? static_cast<cell_t>(inst->GetEntityIndex().Get()) : -1;
        }

        cell_t RefOf(CEntityInstance* inst)
        {
            if (!inst)
                return -1;
            const CEntityHandle h = inst->GetRefEHandle();
            return static_cast<cell_t>(kRefFlag | ((static_cast<uint32_t>(h.GetSerialNumber()) & 0xFFFF) << 15)
                                       | (static_cast<uint32_t>(h.GetEntryIndex()) & 0x7FFF));
        }

        // ---- props --------------------------------------------------------------

        enum class PropKind
        {
            Int,
            Float,
            Bool,
            Handle,
            Vector,     // Vector, QAngle: three floats
            CharArray,
            Symbol,     // CUtlSymbolLarge: a const char* at the field
            UtlString,  // CUtlString: a char* at the field
            Other,
        };

        struct Prop
        {
            uint8_t* base = nullptr;    // the object the field is in
            const char* declClass = nullptr;
            const char* field = nullptr;
            int offset = 0;             // within base
            PropKind kind = PropKind::Other;
            int size = 0;               // of one element
            bool isSigned = true;
            int count = 1;              // fixed array: elements
            std::string typeName;
        };

        SchemaClassFieldData_t* FindField(CSchemaClassInfo* cls, const char* name, CSchemaClassInfo** declared)
        {
            for (CSchemaClassInfo* c = cls; c; c = c->m_nBaseClassCount ? c->m_pBaseClasses[0].m_pClass : nullptr)
            {
                for (uint16 i = 0; i < c->m_nFieldCount; i++)
                {
                    if (strcmp(c->m_pFields[i].m_pszName, name) == 0)
                    {
                        *declared = c;
                        return &c->m_pFields[i];
                    }
                }
            }
            return nullptr;
        }

        void Classify(CSchemaType* type, Prop& p)
        {
            p.typeName = type->m_sTypeName.Get();
            int size = 0;
            uint8 align = 0;
            type->GetSizeAndAlignment(size, align);
            p.size = size;

            switch (type->m_eTypeCategory)
            {
            case SCHEMA_TYPE_BUILTIN:
            {
                const auto builtin = static_cast<CSchemaType_Builtin*>(type)->m_eBuiltinType;
                switch (builtin)
                {
                case SCHEMA_BUILTIN_TYPE_FLOAT32:
                case SCHEMA_BUILTIN_TYPE_FLOAT64:
                    p.kind = PropKind::Float;
                    break;
                case SCHEMA_BUILTIN_TYPE_BOOL:
                    p.kind = PropKind::Bool;
                    break;
                case SCHEMA_BUILTIN_TYPE_UINT8:
                case SCHEMA_BUILTIN_TYPE_UINT16:
                case SCHEMA_BUILTIN_TYPE_UINT32:
                case SCHEMA_BUILTIN_TYPE_UINT64:
                    p.kind = PropKind::Int;
                    p.isSigned = false;
                    break;
                default:
                    p.kind = PropKind::Int;
                    break;
                }
                return;
            }
            case SCHEMA_TYPE_DECLARED_ENUM:
                p.kind = PropKind::Int;
                return;
            case SCHEMA_TYPE_FIXED_ARRAY:
            {
                auto* arr = static_cast<CSchemaType_FixedArray*>(type);
                Prop elem;
                Classify(arr->m_pElementType, elem);
                if (elem.typeName == "char")
                {
                    p.kind = PropKind::CharArray;
                    return;
                }
                p.kind = elem.kind;
                p.isSigned = elem.isSigned;
                p.size = arr->m_nElementSize;
                p.count = arr->m_nElementCount;
                return;
            }
            default:
                break;
            }

            const std::string& n = p.typeName;
            if (n.rfind("CHandle<", 0) == 0 || n == "CEntityHandle")
                p.kind = PropKind::Handle;
            else if (n == "Vector" || n == "QAngle" || n == "VectorWS")
                p.kind = PropKind::Vector;
            else if (n == "CUtlSymbolLarge")
                p.kind = PropKind::Symbol;
            else if (n == "CUtlString")
                p.kind = PropKind::UtlString;
            else if (n == "GameTime_t")
            {
                p.kind = PropKind::Float;
                p.size = 4;
            }
            else if (n == "GameTick_t" || n == "CEntityIndex" || n == "CPlayerSlot" || n == "CUtlStringToken")
            {
                p.kind = PropKind::Int;
                p.size = 4;
            }
        }

        // SourceMod names that are not CS2 fields.
        const char* Alias(const char* name)
        {
            static const std::pair<const char*, const char*> kAliases[] = {
                { "m_vecOrigin", "m_pGameSceneNode.m_vecAbsOrigin" },
                { "m_vecAbsOrigin", "m_pGameSceneNode.m_vecAbsOrigin" },
                { "m_angRotation", "m_pGameSceneNode.m_angAbsRotation" },
                { "m_angAbsRotation", "m_pGameSceneNode.m_angAbsRotation" },
                { "m_hOwner", "m_hOwnerEntity" },
            };
            for (const auto& [from, to] : kAliases)
                if (strcmp(name, from) == 0)
                    return to;
            return name;
        }

        bool ResolveProp(CEntityInstance* ent, const char* path, Prop& out)
        {
            CSchemaClassInfo* cls = ent->Schema_DynamicBinding().Get();
            uint8_t* base = reinterpret_cast<uint8_t*>(ent);
            int offset = 0;

            std::string rest = Alias(path);
            while (cls)
            {
                const size_t dot = rest.find('.');
                const std::string name = rest.substr(0, dot);

                CSchemaClassInfo* declared = nullptr;
                SchemaClassFieldData_t* field = FindField(cls, name.c_str(), &declared);
                if (!field)
                    return false;

                if (dot == std::string::npos)
                {
                    out.base = base;
                    out.declClass = declared->m_pszName;
                    out.field = field->m_pszName;
                    out.offset = offset + field->m_nSingleInheritanceOffset;
                    Classify(field->m_pType, out);
                    return true;
                }

                rest = rest.substr(dot + 1);
                CSchemaType* type = field->m_pType;
                if (type->m_eTypeCategory == SCHEMA_TYPE_DECLARED_CLASS)
                {
                    // Embedded: the next field is in the same object, further in.
                    offset += field->m_nSingleInheritanceOffset;
                    base += offset;
                    offset = 0;
                    cls = static_cast<CSchemaType_DeclaredClass*>(type)->m_pClassInfo;
                }
                else if (type->m_eTypeCategory == SCHEMA_TYPE_POINTER)
                {
                    CSchemaType* pointee = static_cast<CSchemaType_Ptr*>(type)->m_pObjectType;
                    if (!pointee || pointee->m_eTypeCategory != SCHEMA_TYPE_DECLARED_CLASS)
                        return false;
                    base = *reinterpret_cast<uint8_t**>(base + offset + field->m_nSingleInheritanceOffset);
                    if (!base)
                        return false;
                    offset = 0;
                    cls = static_cast<CSchemaType_DeclaredClass*>(pointee)->m_pClassInfo;
                }
                else
                    return false;
            }
            return false;
        }

        // A client index is the controller, but CS2 keeps health, armor,
        // movement and the rest on the pawn: a prop the controller does not
        // have is looked up on the pawn it drives.
        CEntityInstance* PawnOf(CEntityInstance* ent)
        {
            if (!ent || strcmp(ent->GetClassname(), "cs_player_controller") != 0)
                return nullptr;
            return static_cast<CCSPlayerController*>(ent)->GetPlayerPawn();
        }

        bool FindProp(IPluginContext* ctx, CEntityInstance* ent, const char* path, Prop& out)
        {
            CEntityInstance* pawn = PawnOf(ent);
            if (ResolveProp(ent, path, out))
            {
                // Declared on CBaseEntity and the like, the controller has
                // the field too, but the live value is the pawn one.
                Prop onPawn;
                if (pawn && !strstr(out.declClass, "Controller") && ResolveProp(pawn, path, onPawn))
                    out = onPawn;
                return true;
            }
            if (pawn && ResolveProp(pawn, path, out))
                return true;
            ctx->ThrowNativeError("Property \"%s\" not found (entity %d/%s)", path, IndexOf(ent), ent->GetClassname());
            return false;
        }

        bool CheckElement(IPluginContext* ctx, const Prop& p, cell_t element)
        {
            if (element < 0 || element >= p.count)
            {
                ctx->ThrowNativeError("Element %d is out of bounds (Prop %s has %d elements).", element, p.field, p.count);
                return false;
            }
            return true;
        }

        uint8_t* Addr(const Prop& p, cell_t element)
        {
            return p.base + p.offset + element * p.size;
        }

        // A networked field changed: tell the owning entity, through the
        // class's chainer when it has one (services, scene node, embedded
        // properties) -- what SCHEMA_FIELD's setter does.
        void Changed(const Prop& p, cell_t element)
        {
            const SchemaKey key = schema::GetOffset(p.declClass, hash_32_fnv1a_const(p.declClass), p.field, hash_32_fnv1a_const(p.field));
            if (!key.valid || !key.networked)
                return;
            const int extra = element * p.size;
            const int16_t chain = schema::FindChainOffset(p.declClass);
            if (chain != 0)
                ChainNetworkStateChanged(reinterpret_cast<uintptr_t>(p.base) + chain, key.offset + extra);
            else
                EntityNetworkStateChanged(reinterpret_cast<uintptr_t>(p.base), key.offset + extra);
        }

        cell_t ReadInt(const uint8_t* addr, int size, bool isSigned)
        {
            switch (size)
            {
            case 1:
                return isSigned ? *reinterpret_cast<const int8_t*>(addr) : *addr;
            case 2:
                return isSigned ? *reinterpret_cast<const int16_t*>(addr) : *reinterpret_cast<const uint16_t*>(addr);
            case 8:
                return static_cast<cell_t>(*reinterpret_cast<const int64_t*>(addr));
            default:
                return *reinterpret_cast<const int32_t*>(addr);
            }
        }

        void WriteInt(uint8_t* addr, int size, cell_t value)
        {
            switch (size)
            {
            case 1:
                *addr = static_cast<uint8_t>(value);
                break;
            case 2:
                *reinterpret_cast<int16_t*>(addr) = static_cast<int16_t>(value);
                break;
            case 8:
                *reinterpret_cast<int64_t*>(addr) = value;
                break;
            default:
                *reinterpret_cast<int32_t*>(addr) = value;
                break;
            }
        }

        cell_t HandleToIndex(const uint8_t* addr)
        {
            const CEntityHandle h = *reinterpret_cast<const CEntityHandle*>(addr);
            CEntityInstance* inst = h.IsValid() ? GetEntitySystem()->GetEntityInstance(h) : nullptr;
            return IndexOf(inst);
        }

        // GetEntProp(entity, PropType type, const char[] prop, int size=4, int element=0)
        cell_t N_GetEntProp(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return 0;
            const cell_t element = params[0] >= 5 ? params[5] : 0;
            if (!CheckElement(ctx, p, element))
                return 0;
            switch (p.kind)
            {
            case PropKind::Int:
            case PropKind::Bool:
                return ReadInt(Addr(p, element), p.size, p.isSigned);
            case PropKind::Handle:
                return *reinterpret_cast<const int32_t*>(Addr(p, element));
            default:
                return ctx->ThrowNativeError("Property \"%s\" is not an integer (%s)", p.field, p.typeName.c_str());
            }
        }

        // SetEntProp(entity, PropType type, const char[] prop, any value, int size=4, int element=0)
        cell_t N_SetEntProp(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return 0;
            const cell_t element = params[0] >= 6 ? params[6] : 0;
            if (!CheckElement(ctx, p, element))
                return 0;
            if (p.kind != PropKind::Int && p.kind != PropKind::Bool)
                return ctx->ThrowNativeError("Property \"%s\" is not an integer (%s)", p.field, p.typeName.c_str());
            WriteInt(Addr(p, element), p.size, p.kind == PropKind::Bool ? (params[4] != 0) : params[4]);
            Changed(p, element);
            return 0;
        }

        cell_t N_GetEntPropFloat(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return 0;
            const cell_t element = params[0] >= 4 ? params[4] : 0;
            if (!CheckElement(ctx, p, element))
                return 0;
            if (p.kind != PropKind::Float)
                return ctx->ThrowNativeError("Property \"%s\" is not a float (%s)", p.field, p.typeName.c_str());
            const uint8_t* addr = Addr(p, element);
            const float value = p.size == 8 ? static_cast<float>(*reinterpret_cast<const double*>(addr)) : *reinterpret_cast<const float*>(addr);
            return sp_ftoc(value);
        }

        cell_t N_SetEntPropFloat(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return 0;
            const cell_t element = params[0] >= 5 ? params[5] : 0;
            if (!CheckElement(ctx, p, element))
                return 0;
            if (p.kind != PropKind::Float)
                return ctx->ThrowNativeError("Property \"%s\" is not a float (%s)", p.field, p.typeName.c_str());
            uint8_t* addr = Addr(p, element);
            if (p.size == 8)
                *reinterpret_cast<double*>(addr) = sp_ctof(params[4]);
            else
                *reinterpret_cast<float*>(addr) = sp_ctof(params[4]);
            Changed(p, element);
            return 0;
        }

        cell_t N_GetEntPropEnt(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return -1;
            const cell_t element = params[0] >= 4 ? params[4] : 0;
            if (!CheckElement(ctx, p, element))
                return -1;
            if (p.kind != PropKind::Handle)
                return ctx->ThrowNativeError("Property \"%s\" is not an entity handle (%s)", p.field, p.typeName.c_str());
            return HandleToIndex(Addr(p, element));
        }

        cell_t N_SetEntPropEnt(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return 0;
            const cell_t element = params[0] >= 5 ? params[5] : 0;
            if (!CheckElement(ctx, p, element))
                return 0;
            if (p.kind != PropKind::Handle)
                return ctx->ThrowNativeError("Property \"%s\" is not an entity handle (%s)", p.field, p.typeName.c_str());
            CEntityHandle h;
            if (params[4] != -1)
            {
                CEntityInstance* other = CheckEntity(ctx, params[4]);
                if (!other)
                    return 0;
                h = other->GetRefEHandle();
            }
            *reinterpret_cast<CEntityHandle*>(Addr(p, element)) = h;
            Changed(p, element);
            return 0;
        }

        cell_t N_GetEntPropVector(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return 0;
            const cell_t element = params[0] >= 5 ? params[5] : 0;
            if (!CheckElement(ctx, p, element))
                return 0;
            if (p.kind != PropKind::Vector)
                return ctx->ThrowNativeError("Property \"%s\" is not a vector (%s)", p.field, p.typeName.c_str());
            const float* v = reinterpret_cast<const float*>(Addr(p, element));
            cell_t* out;
            ctx->LocalToPhysAddr(params[4], &out);
            for (int i = 0; i < 3; i++)
                out[i] = sp_ftoc(v[i]);
            return 0;
        }

        cell_t N_SetEntPropVector(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            const char* path = LocalString(ctx, params[3]);
            cell_t* in;
            ctx->LocalToPhysAddr(params[4], &in);
            const Vector v(sp_ctof(in[0]), sp_ctof(in[1]), sp_ctof(in[2]));

            // The scene node's transform is not set by writing it: teleport.
            if (ent)
            {
                const char* alias = Alias(path);
                if (strcmp(alias, "m_pGameSceneNode.m_vecAbsOrigin") == 0)
                {
                    static_cast<CBaseEntity*>(ent)->Teleport(&v, nullptr, nullptr);
                    return 0;
                }
                if (strcmp(alias, "m_pGameSceneNode.m_angAbsRotation") == 0)
                {
                    const QAngle a(v.x, v.y, v.z);
                    static_cast<CBaseEntity*>(ent)->Teleport(nullptr, &a, nullptr);
                    return 0;
                }
            }

            Prop p;
            if (!ent || !FindProp(ctx, ent, path, p))
                return 0;
            const cell_t element = params[0] >= 5 ? params[5] : 0;
            if (!CheckElement(ctx, p, element))
                return 0;
            if (p.kind != PropKind::Vector)
                return ctx->ThrowNativeError("Property \"%s\" is not a vector (%s)", p.field, p.typeName.c_str());
            float* dest = reinterpret_cast<float*>(Addr(p, element));
            dest[0] = v.x;
            dest[1] = v.y;
            dest[2] = v.z;
            Changed(p, element);
            return 0;
        }

        // GetEntPropString(entity, type, prop, char[] buffer, int maxlen, int element=0)
        cell_t N_GetEntPropString(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            if (!ent)
                return 0;
            const char* path = LocalString(ctx, params[3]);
            size_t written = 0;

            // Source 1's names for what CS2 keeps in the entity identity.
            if (strcmp(path, "m_iClassname") == 0 || strcmp(path, "m_designerName") == 0)
            {
                ctx->StringToLocalUTF8(params[4], params[5], ent->GetClassname(), &written);
                return static_cast<cell_t>(written);
            }
            if (strcmp(path, "m_iName") == 0)
            {
                const char* name = ent->m_pEntity->m_name.String();
                ctx->StringToLocalUTF8(params[4], params[5], name ? name : "", &written);
                return static_cast<cell_t>(written);
            }

            Prop p;
            if (!FindProp(ctx, ent, path, p))
                return 0;
            const uint8_t* addr = p.base + p.offset;
            const char* str = nullptr;
            std::string tmp;
            switch (p.kind)
            {
            case PropKind::CharArray:
                tmp.assign(reinterpret_cast<const char*>(addr), strnlen(reinterpret_cast<const char*>(addr), static_cast<size_t>(p.size)));
                str = tmp.c_str();
                break;
            case PropKind::Symbol:
            case PropKind::UtlString:
                str = *reinterpret_cast<const char* const*>(addr);
                break;
            default:
                return ctx->ThrowNativeError("Property \"%s\" is not a string (%s)", p.field, p.typeName.c_str());
            }
            ctx->StringToLocalUTF8(params[4], params[5], str ? str : "", &written);
            return static_cast<cell_t>(written);
        }

        // SetEntPropString(entity, type, prop, const char[] buffer, int element=0): char arrays.
        cell_t N_SetEntPropString(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return 0;
            if (p.kind != PropKind::CharArray)
                return ctx->ThrowNativeError("Property \"%s\" is not a writable string (%s; only char arrays are)", p.field, p.typeName.c_str());
            const char* src = LocalString(ctx, params[4]);
            const size_t len = std::min(strlen(src), static_cast<size_t>(p.size) - 1);
            char* dest = reinterpret_cast<char*>(p.base + p.offset);
            memcpy(dest, src, len);
            dest[len] = '\0';
            Changed(p, 0);
            return static_cast<cell_t>(len);
        }

        cell_t N_GetEntPropArraySize(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !FindProp(ctx, ent, LocalString(ctx, params[3]), p))
                return 0;
            return p.count > 1 ? p.count : 0;
        }

        // HasEntProp is a stock over FindDataMapInfo / FindSendPropInfo in
        // entity.inc; these give the offset or -1.
        // FindDataMapInfo(entity, prop, &type, &num_bits, &local_offset)
        cell_t N_FindDataMapInfo(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            Prop p;
            if (!ent || !ResolveProp(ent, LocalString(ctx, params[2]), p))
                return -1;
            if (p.base != reinterpret_cast<uint8_t*>(ent))
                return -1;      // behind a pointer: no offset from the entity
            const cell_t offset = p.offset;
            cell_t* out;
            if (params[0] >= 5 && ctx->LocalToPhysAddr(params[5], &out) == SP_ERROR_NONE)
                *out = offset;
            return offset;
        }

        // FindSendPropInfo(cls, prop, &type, &num_bits, &local_offset, &array_size)
        cell_t N_FindSendPropInfo(IPluginContext* ctx, const cell_t* params)
        {
            const char* cls = LocalString(ctx, params[1]);
            const char* prop = LocalString(ctx, params[2]);
            const SchemaKey key = schema::GetOffset(cls, hash_32_fnv1a_const(cls), prop, hash_32_fnv1a_const(prop));
            if (!key.valid)
                return -1;
            cell_t* out;
            if (params[0] >= 5 && ctx->LocalToPhysAddr(params[5], &out) == SP_ERROR_NONE)
                *out = key.offset;
            return key.offset;
        }

        // ---- entity data by offset ----------------------------------------------

        uint8_t* DataAt(IPluginContext* ctx, cell_t entity, cell_t offset, cell_t size, CEntityInstance** out = nullptr)
        {
            CEntityInstance* ent = CheckEntity(ctx, entity);
            if (!ent)
                return nullptr;
            if (offset <= 0 || offset > 65535)
            {
                ctx->ThrowNativeError("Offset %d is invalid", offset);
                return nullptr;
            }
            if (out)
                *out = ent;
            return reinterpret_cast<uint8_t*>(ent) + offset;
        }

        void DataChanged(CEntityInstance* ent, cell_t offset, bool change)
        {
            if (change)
                EntityNetworkStateChanged(reinterpret_cast<uintptr_t>(ent), offset);
        }

        cell_t N_GetEntData(IPluginContext* ctx, const cell_t* params)
        {
            const cell_t size = params[0] >= 3 ? params[3] : 4;
            uint8_t* addr = DataAt(ctx, params[1], params[2], size);
            return addr ? ReadInt(addr, size, true) : 0;
        }

        cell_t N_SetEntData(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent;
            const cell_t size = params[0] >= 4 ? params[4] : 4;
            uint8_t* addr = DataAt(ctx, params[1], params[2], size, &ent);
            if (addr)
            {
                WriteInt(addr, size, params[3]);
                DataChanged(ent, params[2], params[0] >= 5 && params[5]);
            }
            return 0;
        }

        cell_t N_GetEntDataFloat(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* addr = DataAt(ctx, params[1], params[2], 4);
            return addr ? sp_ftoc(*reinterpret_cast<float*>(addr)) : 0;
        }

        cell_t N_SetEntDataFloat(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent;
            uint8_t* addr = DataAt(ctx, params[1], params[2], 4, &ent);
            if (addr)
            {
                *reinterpret_cast<float*>(addr) = sp_ctof(params[3]);
                DataChanged(ent, params[2], params[0] >= 4 && params[4]);
            }
            return 0;
        }

        cell_t N_GetEntDataEnt2(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* addr = DataAt(ctx, params[1], params[2], 4);
            return addr ? HandleToIndex(addr) : -1;
        }

        cell_t N_SetEntDataEnt2(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent;
            uint8_t* addr = DataAt(ctx, params[1], params[2], 4, &ent);
            if (!addr)
                return 0;
            CEntityHandle h;
            if (params[3] != -1)
            {
                CEntityInstance* other = CheckEntity(ctx, params[3]);
                if (!other)
                    return 0;
                h = other->GetRefEHandle();
            }
            *reinterpret_cast<CEntityHandle*>(addr) = h;
            DataChanged(ent, params[2], params[0] >= 4 && params[4]);
            return 0;
        }

        cell_t N_GetEntDataVector(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* addr = DataAt(ctx, params[1], params[2], 12);
            if (!addr)
                return 0;
            cell_t* out;
            ctx->LocalToPhysAddr(params[3], &out);
            for (int i = 0; i < 3; i++)
                out[i] = sp_ftoc(reinterpret_cast<float*>(addr)[i]);
            return 0;
        }

        cell_t N_SetEntDataVector(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent;
            uint8_t* addr = DataAt(ctx, params[1], params[2], 12, &ent);
            if (!addr)
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[3], &in);
            for (int i = 0; i < 3; i++)
                reinterpret_cast<float*>(addr)[i] = sp_ctof(in[i]);
            DataChanged(ent, params[2], params[0] >= 4 && params[4]);
            return 0;
        }

        cell_t N_GetEntDataString(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* addr = DataAt(ctx, params[1], params[2], 1);
            if (!addr)
                return 0;
            size_t written = 0;
            const std::string str(reinterpret_cast<const char*>(addr), strnlen(reinterpret_cast<const char*>(addr), static_cast<size_t>(std::max(params[4], 1))));
            ctx->StringToLocalUTF8(params[3], params[4], str.c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_SetEntDataString(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent;
            uint8_t* addr = DataAt(ctx, params[1], params[2], 1, &ent);
            if (!addr || params[4] < 1)
                return 0;
            const char* src = LocalString(ctx, params[3]);
            const size_t len = std::min(strlen(src), static_cast<size_t>(params[4] - 1));
            memcpy(addr, src, len);
            addr[len] = '\0';
            DataChanged(ent, params[2], params[0] >= 5 && params[5]);
            return static_cast<cell_t>(len);
        }

        cell_t N_ChangeEdictState(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            if (ent && params[0] >= 2 && params[2] > 0)
                EntityNetworkStateChanged(reinterpret_cast<uintptr_t>(ent), params[2]);
            return 0;
        }

        // ---- entities -----------------------------------------------------------

        cell_t N_GetMaxEntities(IPluginContext*, const cell_t*)
        {
            return kMaxEntities;
        }

        cell_t N_GetEntityCount(IPluginContext*, const cell_t*)
        {
            cell_t count = 0;
            for (cell_t i = 0; i < kMaxEntities; i++)
                if (InstanceAt(i))
                    count++;
            return count;
        }

        cell_t N_IsValidEntity(IPluginContext*, const cell_t* params)
        {
            return Resolve(params[1]) != nullptr;
        }

        cell_t N_IsEntNetworkable(IPluginContext*, const cell_t* params)
        {
            CEntityInstance* ent = Resolve(params[1]);
            return ent && IndexOf(ent) < 16384;
        }

        cell_t N_RemoveEntity(IPluginContext* ctx, const cell_t* params)
        {
            if (CEntityInstance* ent = CheckEntity(ctx, params[1]))
                static_cast<CBaseEntity*>(ent)->Remove();
            return 0;
        }

        cell_t N_GetEdictClassname(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = Resolve(params[1]);
            if (!ent)
                return 0;
            ctx->StringToLocalUTF8(params[2], params[3], ent->GetClassname(), nullptr);
            return 1;
        }

        // The schema class, what Source 1 called the server class.
        cell_t N_GetEntityNetClass(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = Resolve(params[1]);
            CSchemaClassInfo* cls = ent ? ent->Schema_DynamicBinding().Get() : nullptr;
            if (!cls)
                return 0;
            ctx->StringToLocalUTF8(params[2], params[3], cls->m_pszName, nullptr);
            return 1;
        }

        cell_t N_EntIndexToEntRef(IPluginContext*, const cell_t* params)
        {
            return RefOf(Resolve(params[1]));
        }

        cell_t N_EntRefToEntIndex(IPluginContext*, const cell_t* params)
        {
            if (!(static_cast<uint32_t>(params[1]) & kRefFlag))
                return InstanceAt(params[1]) ? params[1] : -1;
            return IndexOf(Resolve(params[1]));
        }

        cell_t N_MakeCompatEntRef(IPluginContext*, const cell_t* params)
        {
            return (static_cast<uint32_t>(params[1]) & kRefFlag) ? params[1] : RefOf(Resolve(params[1]));
        }

        // ---- sdktools: creating and driving entities ------------------------------

        // DispatchKeyValue before DispatchSpawn: CS2 takes key values with the spawn.
        std::unordered_map<cell_t, std::vector<std::pair<std::string, std::string>>> g_pendingKeyValues;

        cell_t N_CreateEntityByName(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = CBaseEntity::CreateEntityByName(LocalString(ctx, params[1]));
            const cell_t index = IndexOf(ent);
            if (index >= 0)
                g_pendingKeyValues.erase(index);
            return index;
        }

        cell_t DispatchKV(IPluginContext* ctx, cell_t entity, const char* key, std::string value)
        {
            CEntityInstance* ent = CheckEntity(ctx, entity);
            if (!ent)
                return 0;
            g_pendingKeyValues[IndexOf(ent)].emplace_back(key, std::move(value));
            return 1;
        }

        cell_t N_DispatchKeyValue(IPluginContext* ctx, const cell_t* params)
        {
            return DispatchKV(ctx, params[1], LocalString(ctx, params[2]), LocalString(ctx, params[3]));
        }

        cell_t N_DispatchKeyValueInt(IPluginContext* ctx, const cell_t* params)
        {
            return DispatchKV(ctx, params[1], LocalString(ctx, params[2]), std::to_string(params[3]));
        }

        cell_t N_DispatchKeyValueFloat(IPluginContext* ctx, const cell_t* params)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%f", static_cast<double>(sp_ctof(params[3])));
            return DispatchKV(ctx, params[1], LocalString(ctx, params[2]), buf);
        }

        cell_t N_DispatchKeyValueVector(IPluginContext* ctx, const cell_t* params)
        {
            cell_t* v;
            ctx->LocalToPhysAddr(params[3], &v);
            char buf[128];
            snprintf(buf, sizeof(buf), "%f %f %f", static_cast<double>(sp_ctof(v[0])), static_cast<double>(sp_ctof(v[1])), static_cast<double>(sp_ctof(v[2])));
            return DispatchKV(ctx, params[1], LocalString(ctx, params[2]), buf);
        }

        cell_t N_DispatchSpawn(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            if (!ent)
                return 0;

            CEntityKeyValues* kv = nullptr;
            auto it = g_pendingKeyValues.find(IndexOf(ent));
            if (it != g_pendingKeyValues.end())
            {
                kv = new CEntityKeyValues();
                for (const auto& [key, value] : it->second)
                    kv->SetString(key.c_str(), value.c_str());
                g_pendingKeyValues.erase(it);
            }
            static_cast<CBaseEntity*>(ent)->DispatchSpawn(kv);
            return 1;
        }

        // SetVariant* fill what the next AcceptEntityInput passes; CS2's
        // inputs take their value as text.
        std::string g_variant;

        cell_t N_SetVariantString(IPluginContext* ctx, const cell_t* params)
        {
            g_variant = LocalString(ctx, params[1]);
            return 0;
        }

        cell_t N_SetVariantInt(IPluginContext*, const cell_t* params)
        {
            g_variant = std::to_string(params[1]);
            return 0;
        }

        cell_t N_SetVariantBool(IPluginContext*, const cell_t* params)
        {
            g_variant = params[1] ? "1" : "0";
            return 0;
        }

        cell_t N_SetVariantFloat(IPluginContext*, const cell_t* params)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%f", static_cast<double>(sp_ctof(params[1])));
            g_variant = buf;
            return 0;
        }

        cell_t N_SetVariantVector3D(IPluginContext* ctx, const cell_t* params)
        {
            cell_t* v;
            ctx->LocalToPhysAddr(params[1], &v);
            char buf[128];
            snprintf(buf, sizeof(buf), "%f %f %f", static_cast<double>(sp_ctof(v[0])), static_cast<double>(sp_ctof(v[1])), static_cast<double>(sp_ctof(v[2])));
            g_variant = buf;
            return 0;
        }

        cell_t N_SetVariantColor(IPluginContext* ctx, const cell_t* params)
        {
            cell_t* c;
            ctx->LocalToPhysAddr(params[1], &c);
            char buf[64];
            snprintf(buf, sizeof(buf), "%d %d %d %d", c[0], c[1], c[2], c[3]);
            g_variant = buf;
            return 0;
        }

        cell_t N_SetVariantEntity(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            if (ent)
            {
                const char* name = ent->m_pEntity->m_name.String();
                g_variant = name && *name ? name : "!activator";
            }
            return 0;
        }

        // AcceptEntityInput(dest, const char[] input, activator=-1, caller=-1, outputid=0)
        cell_t N_AcceptEntityInput(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            if (!ent)
                return 0;
            CEntityInstance* activator = params[0] >= 3 && params[3] != -1 ? CheckEntity(ctx, params[3]) : nullptr;
            CEntityInstance* caller = params[0] >= 4 && params[4] != -1 ? CheckEntity(ctx, params[4]) : nullptr;
            const std::string value = std::move(g_variant);
            g_variant.clear();
            static_cast<CBaseEntity*>(ent)->AcceptInput(LocalString(ctx, params[2]), activator, caller, value.c_str());
            return 1;
        }

        // TeleportEntity(entity, origin=NULL_VECTOR, angles=NULL_VECTOR, velocity=NULL_VECTOR)
        cell_t N_TeleportEntity(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            if (!ent)
                return 0;
            auto read = [ctx, params](int param, float out[3]) -> bool
            {
                if (params[0] < param)
                    return false;
                cell_t* addr;
                ctx->LocalToPhysAddr(params[param], &addr);
                if (addr == ctx->GetNullRef(SP_NULL_VECTOR))
                    return false;
                for (int i = 0; i < 3; i++)
                    out[i] = sp_ctof(addr[i]);
                return true;
            };
            float o[3], a[3], v[3];
            const bool hasOrigin = read(2, o), hasAngles = read(3, a), hasVelocity = read(4, v);
            const Vector origin(o[0], o[1], o[2]), velocity(v[0], v[1], v[2]);
            const QAngle angles(a[0], a[1], a[2]);
            static_cast<CBaseEntity*>(ent)->Teleport(hasOrigin ? &origin : nullptr, hasAngles ? &angles : nullptr, hasVelocity ? &velocity : nullptr);
            return 0;
        }

        // FindEntityByClassname(int startEnt, const char[] classname): -1 starts at the beginning.
        cell_t N_FindEntityByClassname(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* start = params[1] == -1 ? nullptr : Resolve(params[1]);
            if (params[1] != -1 && !start)
                return ctx->ThrowNativeError("Entity %d (%d) is invalid", params[1], params[1]);
            const char* cls = LocalString(ctx, params[2]);
            for (cell_t i = start ? IndexOf(start) + 1 : 0; i < kMaxEntities; i++)
            {
                CEntityInstance* ent = InstanceAt(i);
                if (ent && strcmp(ent->GetClassname(), cls) == 0)
                    return i;
            }
            return -1;
        }

        // ---- clients: what lives on the controller and the pawn -------------------

        CCSPlayerController* InGameClient(IPluginContext* ctx, cell_t client)
        {
            CCSPlayerController* player = (client >= 1 && client <= kMaxClients) ? CCSPlayerController::FromSlot(client - 1) : nullptr;
            if (!player || player->m_iConnected() != PlayerConnectedState::Connected)
            {
                ctx->ThrowNativeError("Client %d is not in game", client);
                return nullptr;
            }
            return player;
        }

        CCSPlayerPawn* PawnOrError(IPluginContext* ctx, cell_t client)
        {
            CCSPlayerController* player = InGameClient(ctx, client);
            if (!player)
                return nullptr;
            CCSPlayerPawn* pawn = player->GetPlayerPawn();
            if (!pawn)
                ctx->ThrowNativeError("Client %d has no pawn", client);
            return pawn;
        }

        // A value by path on the client (falling through to the pawn).
        cell_t ClientInt(IPluginContext* ctx, cell_t client, const char* path)
        {
            CCSPlayerController* player = InGameClient(ctx, client);
            Prop p;
            if (!player || !FindProp(ctx, player, path, p))
                return 0;
            return ReadInt(Addr(p, 0), p.size, p.isSigned);
        }

        cell_t N_GetClientHealth(IPluginContext* ctx, const cell_t* params)
        {
            return ClientInt(ctx, params[1], "m_iHealth");
        }

        cell_t N_GetClientArmor(IPluginContext* ctx, const cell_t* params)
        {
            return ClientInt(ctx, params[1], "m_ArmorValue");
        }

        cell_t N_GetClientTeam(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = InGameClient(ctx, params[1]);
            return player ? player->m_iTeamNum() : 0;
        }

        cell_t N_IsPlayerAlive(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = InGameClient(ctx, params[1]);
            return player && player->m_bPawnIsAlive();
        }

        cell_t N_GetClientFrags(IPluginContext* ctx, const cell_t* params)
        {
            return ClientInt(ctx, params[1], "m_pActionTrackingServices.m_matchStats.m_iKills");
        }

        cell_t N_GetClientDeaths(IPluginContext* ctx, const cell_t* params)
        {
            return ClientInt(ctx, params[1], "m_pActionTrackingServices.m_matchStats.m_iDeaths");
        }

        void WriteVector(IPluginContext* ctx, cell_t addr, float x, float y, float z)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            out[0] = sp_ftoc(x);
            out[1] = sp_ftoc(y);
            out[2] = sp_ftoc(z);
        }

        cell_t N_GetClientAbsOrigin(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerPawn* pawn = PawnOrError(ctx, params[1]))
            {
                const Vector v = pawn->GetAbsOrigin();
                WriteVector(ctx, params[2], v.x, v.y, v.z);
            }
            return 0;
        }

        cell_t N_GetClientAbsAngles(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerPawn* pawn = PawnOrError(ctx, params[1]);
            Prop p;
            if (pawn && FindProp(ctx, pawn, "m_pGameSceneNode.m_angAbsRotation", p))
            {
                const float* a = reinterpret_cast<const float*>(Addr(p, 0));
                WriteVector(ctx, params[2], a[0], a[1], a[2]);
            }
            return 0;
        }

        cell_t N_GetClientEyePosition(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerPawn* pawn = PawnOrError(ctx, params[1]))
            {
                const Vector v = pawn->GetEyePosition();
                WriteVector(ctx, params[2], v.x, v.y, v.z);
            }
            return 0;
        }

        cell_t N_GetClientEyeAngles(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerPawn* pawn = PawnOrError(ctx, params[1]);
            if (!pawn)
                return 0;
            const QAngle a = pawn->GetEyeAngles();
            WriteVector(ctx, params[2], a.x, a.y, a.z);
            return 1;
        }

        cell_t N_ChangeClientTeam(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* player = InGameClient(ctx, params[1]))
                player->ChangeTeam(params[2]);
            return 0;
        }

        // ForcePlayerSuicide(int client, bool explode=false)
        cell_t N_ForcePlayerSuicide(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerPawn* pawn = PawnOrError(ctx, params[1]))
                pawn->CommitSuicide(params[0] >= 2 && params[2], true);
            return 0;
        }

        // GivePlayerItem(int client, const char[] item, int iSubType=0): the entity or -1.
        cell_t N_GivePlayerItem(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerPawn* pawn = PawnOrError(ctx, params[1]);
            if (!pawn || !pawn->m_pItemServices())
                return -1;
            return IndexOf(pawn->m_pItemServices()->GiveNamedItem(LocalString(ctx, params[2])));
        }

        cell_t N_GetClientModel(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerPawn* pawn = PawnOrError(ctx, params[1]);
            const char* model = pawn ? pawn->GetModelName().String() : nullptr;
            ctx->StringToLocalUTF8(params[2], params[3], model ? model : "", nullptr);
            return 0;
        }

        cell_t N_SetEntityModel(IPluginContext* ctx, const cell_t* params)
        {
            if (CEntityInstance* ent = CheckEntity(ctx, params[1]))
                static_cast<CBaseModelEntity*>(ent)->SetModel(LocalString(ctx, params[2]));
            return 0;
        }

        const sp_nativeinfo_t g_entityNatives[] = {
            // clients.inc / sdktools_functions.inc (players)
            { "GetClientHealth", N_GetClientHealth },
            { "GetClientArmor", N_GetClientArmor },
            { "GetClientTeam", N_GetClientTeam },
            { "IsPlayerAlive", N_IsPlayerAlive },
            { "GetClientFrags", N_GetClientFrags },
            { "GetClientDeaths", N_GetClientDeaths },
            { "GetClientAbsOrigin", N_GetClientAbsOrigin },
            { "GetClientAbsAngles", N_GetClientAbsAngles },
            { "GetClientEyePosition", N_GetClientEyePosition },
            { "GetClientEyeAngles", N_GetClientEyeAngles },
            { "GetClientModel", N_GetClientModel },
            { "ChangeClientTeam", N_ChangeClientTeam },
            { "ForcePlayerSuicide", N_ForcePlayerSuicide },
            { "GivePlayerItem", N_GivePlayerItem },
            { "SetEntityModel", N_SetEntityModel },
            // entity.inc
            { "GetMaxEntities", N_GetMaxEntities },
            { "GetEntityCount", N_GetEntityCount },
            { "IsValidEntity", N_IsValidEntity },
            { "IsValidEdict", N_IsValidEntity },
            { "IsEntNetworkable", N_IsEntNetworkable },
            { "RemoveEdict", N_RemoveEntity },
            { "RemoveEntity", N_RemoveEntity },
            { "GetEdictClassname", N_GetEdictClassname },
            { "GetEntityNetClass", N_GetEntityNetClass },
            { "ChangeEdictState", N_ChangeEdictState },
            { "GetEntData", N_GetEntData },
            { "SetEntData", N_SetEntData },
            { "GetEntDataFloat", N_GetEntDataFloat },
            { "SetEntDataFloat", N_SetEntDataFloat },
            { "GetEntDataEnt2", N_GetEntDataEnt2 },
            { "SetEntDataEnt2", N_SetEntDataEnt2 },
            { "GetEntDataVector", N_GetEntDataVector },
            { "SetEntDataVector", N_SetEntDataVector },
            { "GetEntDataString", N_GetEntDataString },
            { "SetEntDataString", N_SetEntDataString },
            { "FindSendPropInfo", N_FindSendPropInfo },
            { "FindDataMapInfo", N_FindDataMapInfo },
            { "GetEntProp", N_GetEntProp },
            { "SetEntProp", N_SetEntProp },
            { "GetEntPropFloat", N_GetEntPropFloat },
            { "SetEntPropFloat", N_SetEntPropFloat },
            { "GetEntPropEnt", N_GetEntPropEnt },
            { "SetEntPropEnt", N_SetEntPropEnt },
            { "GetEntPropVector", N_GetEntPropVector },
            { "SetEntPropVector", N_SetEntPropVector },
            { "GetEntPropString", N_GetEntPropString },
            { "SetEntPropString", N_SetEntPropString },
            { "GetEntPropArraySize", N_GetEntPropArraySize },
            // halflife.inc
            { "EntIndexToEntRef", N_EntIndexToEntRef },
            { "EntRefToEntIndex", N_EntRefToEntIndex },
            { "MakeCompatEntRef", N_MakeCompatEntRef },
            // sdktools_functions.inc / sdktools_entinput.inc / sdktools_variant_t.inc
            { "CreateEntityByName", N_CreateEntityByName },
            { "DispatchKeyValue", N_DispatchKeyValue },
            { "DispatchKeyValueInt", N_DispatchKeyValueInt },
            { "DispatchKeyValueFloat", N_DispatchKeyValueFloat },
            { "DispatchKeyValueVector", N_DispatchKeyValueVector },
            { "DispatchSpawn", N_DispatchSpawn },
            { "SetVariantString", N_SetVariantString },
            { "SetVariantInt", N_SetVariantInt },
            { "SetVariantBool", N_SetVariantBool },
            { "SetVariantFloat", N_SetVariantFloat },
            { "SetVariantVector3D", N_SetVariantVector3D },
            { "SetVariantPosVector3D", N_SetVariantVector3D },
            { "SetVariantColor", N_SetVariantColor },
            { "SetVariantEntity", N_SetVariantEntity },
            { "AcceptEntityInput", N_AcceptEntityInput },
            { "TeleportEntity", N_TeleportEntity },
            { "FindEntityByClassname", N_FindEntityByClassname },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetEntityNatives()
    {
        return g_entityNatives;
    }
}
