/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * The schema and entity part of the SourcePawn API (source2mod/schema.inc,
 * source2mod/entities.inc): Source2ModSchema's generic field access behind
 * the generated class methodmaps, the C++ helper methods of the schema
 * classes, and Source2ModEntities.
 *
 * An entity is its index. Anything else the schema reaches -- services,
 * the scene node, the game rules -- is a SchemaObject: a handle the core
 * keeps, which remembers how it was reached (an entity and the fields from
 * it, or the game rules) and finds the object again on every use, so a
 * stale one reads as gone instead of reading freed memory.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/entities.h"
#include "utils/log.h"

#include <source2mod/schema/schema.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CBaseModelEntity.h>
#include <source2mod/schema/entity/classes/CBasePlayerWeapon.h>
#include <source2mod/schema/entity/classes/CCSGameRules.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>
#include <source2mod/schema/entity/classes/CCSObserverPawn.h>
#include <source2mod/schema/entity/classes/CPlayer_ItemServices.h>
#include <source2mod/schema/entity/classes/CPlayer_WeaponServices.h>
#include <source2mod/utils/plat.h>

#include <schemalib/schemaclassinfo.h>
#include <schemalib/schematype.h>
#include <schemasystem/schemasystem.h>
#include <sp_callable.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        constexpr cell_t kMaxEntities = 32768;
        constexpr uint32_t kRefFlag = 1u << 31;

        CEntityInstance* InstanceAt(cell_t index)
        {
            if (index < 0 || index >= kMaxEntities)
                return nullptr;
            return CBaseEntity::FromIndex<CEntityInstance>(index);
        }

        cell_t IndexOf(CEntityInstance* inst)
        {
            return inst ? static_cast<cell_t>(inst->GetEntityIndex().Get()) : -1;
        }

        // GetRefEHandle(): index, 16 bits of serial, and the top bit so a
        // reference is never mistaken for an index.
        cell_t RefOf(CEntityInstance* inst)
        {
            if (!inst)
                return -1;
            const CEntityHandle h = inst->GetRefEHandle();
            return static_cast<cell_t>(kRefFlag | ((static_cast<uint32_t>(h.GetSerialNumber()) & 0xFFFF) << 15)
                                       | (static_cast<uint32_t>(h.GetEntryIndex()) & 0x7FFF));
        }

        CEntityInstance* FromRef(cell_t ref)
        {
            const uint32_t raw = static_cast<uint32_t>(ref);
            if (!(raw & kRefFlag))
                return nullptr;
            CEntityInstance* inst = InstanceAt(static_cast<cell_t>(raw & 0x7FFF));
            if (!inst || (static_cast<uint32_t>(inst->GetRefEHandle().GetSerialNumber()) & 0xFFFF) != ((raw >> 15) & 0xFFFF))
                return nullptr;
            return inst;
        }

        CEntityInstance* CheckEntity(IPluginContext* ctx, cell_t entity)
        {
            CEntityInstance* inst = InstanceAt(entity);
            if (!inst)
                ctx->ThrowNativeError("Entity %d is invalid", entity);
            return inst;
        }

        // An entity argument that may be NULL_ENTITY (-1).
        CEntityInstance* OptionalEntity(IPluginContext* ctx, cell_t entity, bool* ok)
        {
            *ok = true;
            if (entity == -1)
                return nullptr;
            CEntityInstance* inst = CheckEntity(ctx, entity);
            *ok = inst != nullptr;
            return inst;
        }

        template <typename T>
        T* As(IPluginContext* ctx, cell_t entity)
        {
            return static_cast<T*>(CheckEntity(ctx, entity));
        }

        // ---- schema fields --------------------------------------------------------

        enum class Kind
        {
            Int,
            Float,
            Bool,
            Handle,
            Vector,
            Color,
            CharArray,
            StringPtr,      // CUtlSymbolLarge, CUtlString, CGlobalSymbol: a char* at the field
            Pointer,        // to a schema class
            Embedded,       // a schema class in place
            Other,
        };

        struct Field
        {
            bool found = false;
            std::string declClass;
            std::string name;
            int offset = 0;
            int size = 0;
            bool isSigned = true;
            Kind kind = Kind::Other;
            std::string typeName;
        };

        CSchemaClassInfo* FindClass(const char* name)
        {
            CSchemaSystem* system = GetSchemaSystem();
            CSchemaSystemTypeScope* scope = system ? system->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT) : nullptr;
            return scope ? scope->FindDeclaredClass(name).Get() : nullptr;
        }

        void Classify(CSchemaType* type, Field& f)
        {
            f.typeName = type->m_sTypeName.Get();
            int size = 0;
            uint8 align = 0;
            type->GetSizeAndAlignment(size, align);
            f.size = size;

            switch (type->m_eTypeCategory)
            {
            case SCHEMA_TYPE_BUILTIN:
                switch (static_cast<CSchemaType_Builtin*>(type)->m_eBuiltinType)
                {
                case SCHEMA_BUILTIN_TYPE_FLOAT32:
                case SCHEMA_BUILTIN_TYPE_FLOAT64:
                    f.kind = Kind::Float;
                    return;
                case SCHEMA_BUILTIN_TYPE_BOOL:
                    f.kind = Kind::Bool;
                    return;
                case SCHEMA_BUILTIN_TYPE_UINT8:
                case SCHEMA_BUILTIN_TYPE_UINT16:
                case SCHEMA_BUILTIN_TYPE_UINT32:
                case SCHEMA_BUILTIN_TYPE_UINT64:
                    f.isSigned = false;
                    f.kind = Kind::Int;
                    return;
                default:
                    f.kind = Kind::Int;
                    return;
                }
            case SCHEMA_TYPE_DECLARED_ENUM:
                f.kind = Kind::Int;
                return;
            case SCHEMA_TYPE_FIXED_ARRAY:
            {
                auto* arr = static_cast<CSchemaType_FixedArray*>(type);
                if (arr->m_pElementType && strcmp(arr->m_pElementType->m_sTypeName.Get(), "char") == 0)
                    f.kind = Kind::CharArray;
                return;
            }
            case SCHEMA_TYPE_POINTER:
                f.kind = Kind::Pointer;
                return;
            case SCHEMA_TYPE_DECLARED_CLASS:
                f.kind = Kind::Embedded;
                break;
            default:
                break;
            }

            const std::string& n = f.typeName;
            if (n.rfind("CHandle<", 0) == 0 || n == "CEntityHandle")
                f.kind = Kind::Handle;
            else if (n == "Vector" || n == "QAngle" || n == "VectorWS")
                f.kind = Kind::Vector;
            else if (n == "Color")
                f.kind = Kind::Color;
            else if (n == "CUtlSymbolLarge" || n == "CUtlString" || n == "CGlobalSymbol")
                f.kind = Kind::StringPtr;
            else if (n == "GameTime_t")
            {
                f.kind = Kind::Float;
                f.size = 4;
            }
            else if (n == "GameTick_t" || n == "CEntityIndex" || n == "CPlayerSlot" || n == "CUtlStringToken")
            {
                f.kind = Kind::Int;
                f.size = 4;
            }
        }

        // By class and field name, through the class's bases; cached.
        const Field& FindField(const char* className, const char* fieldName)
        {
            static std::unordered_map<std::string, Field> s_cache;
            std::string key = std::string(className) + "::" + fieldName;
            auto it = s_cache.find(key);
            if (it != s_cache.end())
                return it->second;

            Field f;
            f.name = fieldName;
            for (CSchemaClassInfo* c = FindClass(className); c && !f.found; c = c->m_nBaseClassCount ? c->m_pBaseClasses[0].m_pClass : nullptr)
            {
                for (uint16 i = 0; i < c->m_nFieldCount; i++)
                {
                    if (strcmp(c->m_pFields[i].m_pszName, fieldName) == 0)
                    {
                        f.found = true;
                        f.declClass = c->m_pszName;
                        f.offset = c->m_pFields[i].m_nSingleInheritanceOffset;
                        Classify(c->m_pFields[i].m_pType, f);
                        break;
                    }
                }
            }
            return s_cache.emplace(std::move(key), std::move(f)).first->second;
        }

        // ---- SchemaObject ---------------------------------------------------------

        enum class Root
        {
            Entity,
            GameRules,
            Raw,
        };

        struct SchemaRef : HandleObject
        {
            Root root = Root::Entity;
            CEntityHandle entity;
            void* raw = nullptr;
            std::vector<std::pair<std::string, std::string>> chain;   // (class, field) from the root
        };

        std::unordered_map<std::string, cell_t> g_refsByKey;

        void* FollowStep(void* base, const std::string& cls, const std::string& field)
        {
            if (!base)
                return nullptr;
            const Field& f = FindField(cls.c_str(), field.c_str());
            if (!f.found)
                return nullptr;
            uint8_t* addr = static_cast<uint8_t*>(base) + f.offset;
            if (f.kind == Kind::Pointer)
                return *reinterpret_cast<void**>(addr);
            if (f.kind == Kind::Embedded)
                return addr;
            return nullptr;
        }

        void* ResolveRef(const SchemaRef& ref)
        {
            void* base = nullptr;
            switch (ref.root)
            {
            case Root::Entity:
                base = ref.entity.IsValid() ? GetEntitySystem()->GetEntityInstance(ref.entity) : nullptr;
                break;
            case Root::GameRules:
                base = entities::entitiesManager.GetGameRules();
                break;
            case Root::Raw:
                base = ref.raw;
                break;
            }
            for (const auto& [cls, field] : ref.chain)
                base = FollowStep(base, cls, field);
            return base;
        }

        // The handle for this way of reaching an object, made once.
        cell_t RefFor(Root root, CEntityHandle entity, void* raw, std::vector<std::pair<std::string, std::string>> chain)
        {
            std::string key = std::to_string(static_cast<int>(root)) + ":" + std::to_string(entity.ToInt()) + ":"
                + std::to_string(reinterpret_cast<uintptr_t>(raw));
            for (const auto& [cls, field] : chain)
                key += "/" + cls + "::" + field;

            auto it = g_refsByKey.find(key);
            if (it != g_refsByKey.end() && Handles().Read(it->second, Types().schemaObject))
                return it->second;

            // Entities come and go; drop the references whose entity is gone
            // before the table grows on.
            if (g_refsByKey.size() > 4096)
            {
                for (auto i = g_refsByKey.begin(); i != g_refsByKey.end();)
                {
                    auto* r = static_cast<SchemaRef*>(Handles().Read(i->second, Types().schemaObject));
                    if (!r || (r->root == Root::Entity && !GetEntitySystem()->GetEntityInstance(r->entity)))
                    {
                        if (r)
                            Handles().Free(i->second, 0, true);
                        i = g_refsByKey.erase(i);
                    }
                    else
                        ++i;
                }
            }

            auto* ref = new SchemaRef();
            ref->root = root;
            ref->entity = entity;
            ref->raw = raw;
            ref->chain = std::move(chain);
            const cell_t handle = Handles().Create(Types().schemaObject, ref, 0);
            if (handle)
                g_refsByKey[key] = handle;
            else
                delete ref;
            return handle;
        }

        // What an `instance` argument points at: an entity by index, or a SchemaObject.
        void* Base(IPluginContext* ctx, cell_t instance, const SchemaRef** ref = nullptr)
        {
            if (ref)
                *ref = nullptr;
            if (instance >= 0 && instance < kMaxEntities)
                return CheckEntity(ctx, instance);
            if (instance == -1)
            {
                ctx->ThrowNativeError("Null schema object");
                return nullptr;
            }
            auto* r = static_cast<SchemaRef*>(ReadHandle(ctx, instance, Types().schemaObject));
            if (!r)
                return nullptr;
            if (ref)
                *ref = r;
            void* base = ResolveRef(*r);
            if (!base)
                ctx->ThrowNativeError("Schema object %x no longer exists", instance);
            return base;
        }

        // The object behind a SchemaObject `this` (services, game rules).
        template <typename T>
        T* ObjectOf(IPluginContext* ctx, cell_t instance)
        {
            if (instance == 0)
            {
                ctx->ThrowNativeError("Null schema object");
                return nullptr;
            }
            return static_cast<T*>(Base(ctx, instance));
        }

        struct Access
        {
            uint8_t* base = nullptr;
            uint8_t* addr = nullptr;
            const Field* field = nullptr;
        };

        bool Lookup(IPluginContext* ctx, const cell_t* params, Access& a, Kind want, const char* what)
        {
            const char* cls = LocalString(ctx, params[2]);
            const char* name = LocalString(ctx, params[3]);
            const Field& f = FindField(cls, name);
            if (!f.found)
            {
                ctx->ThrowNativeError("No field %s::%s in the running game's schema", cls, name);
                return false;
            }
            const bool ok = f.kind == want || (want == Kind::Int && f.kind == Kind::Bool) || (want == Kind::StringPtr && f.kind == Kind::CharArray);
            if (!ok)
            {
                ctx->ThrowNativeError("Field %s::%s is %s, not %s", cls, name, f.typeName.c_str(), what);
                return false;
            }
            if (params[1] == 0)
            {
                ctx->ThrowNativeError("Null schema object");
                return false;
            }
            void* base = Base(ctx, params[1]);
            if (!base)
                return false;
            a.base = static_cast<uint8_t*>(base);
            a.addr = a.base + f.offset;
            a.field = &f;
            return true;
        }

        // A networked field changed: tell the owner, through the class's
        // chainer when it has one -- what SCHEMA_FIELD's setter does.
        void Changed(const Access& a)
        {
            const char* cls = a.field->declClass.c_str();
            const char* name = a.field->name.c_str();
            const SchemaKey key = schema::GetOffset(cls, hash_32_fnv1a_const(cls), name, hash_32_fnv1a_const(name));
            if (!key.valid || !key.networked)
                return;
            const int16_t chain = schema::FindChainOffset(cls);
            if (chain != 0)
                ChainNetworkStateChanged(reinterpret_cast<uintptr_t>(a.base) + chain, key.offset);
            else
                EntityNetworkStateChanged(reinterpret_cast<uintptr_t>(a.base), key.offset);
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

        // Source2ModSchema.GetInt(any instance, const char[] className, const char[] field)
        cell_t N_GetInt(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            return Lookup(ctx, params, a, Kind::Int, "an integer") ? ReadInt(a.addr, a.field->size, a.field->isSigned) : 0;
        }

        cell_t N_SetInt(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (Lookup(ctx, params, a, Kind::Int, "an integer"))
            {
                WriteInt(a.addr, a.field->size, a.field->kind == Kind::Bool ? (params[4] != 0) : params[4]);
                Changed(a);
            }
            return 0;
        }

        cell_t N_GetBool(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            return Lookup(ctx, params, a, Kind::Bool, "a bool") && *a.addr != 0;
        }

        cell_t N_SetBool(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (Lookup(ctx, params, a, Kind::Bool, "a bool"))
            {
                *a.addr = params[4] ? 1 : 0;
                Changed(a);
            }
            return 0;
        }

        cell_t N_GetFloat(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Float, "a float"))
                return 0;
            const float v = a.field->size == 8 ? static_cast<float>(*reinterpret_cast<double*>(a.addr)) : *reinterpret_cast<float*>(a.addr);
            return sp_ftoc(v);
        }

        cell_t N_SetFloat(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Float, "a float"))
                return 0;
            if (a.field->size == 8)
                *reinterpret_cast<double*>(a.addr) = sp_ctof(params[4]);
            else
                *reinterpret_cast<float*>(a.addr) = sp_ctof(params[4]);
            Changed(a);
            return 0;
        }

        cell_t N_GetHandle(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Handle, "an entity handle"))
                return -1;
            const CEntityHandle h = *reinterpret_cast<CEntityHandle*>(a.addr);
            return IndexOf(h.IsValid() ? GetEntitySystem()->GetEntityInstance(h) : nullptr);
        }

        cell_t N_SetHandle(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Handle, "an entity handle"))
                return 0;
            bool ok;
            CEntityInstance* other = OptionalEntity(ctx, params[4], &ok);
            if (!ok)
                return 0;
            *reinterpret_cast<CEntityHandle*>(a.addr) = other ? other->GetRefEHandle() : CEntityHandle();
            Changed(a);
            return 0;
        }

        cell_t N_GetEntityPointer(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Pointer, "a pointer"))
                return -1;
            return IndexOf(*reinterpret_cast<CEntityInstance**>(a.addr));
        }

        // Source2ModSchema.GetObject: a pointer to, or an embedded, schema object.
        cell_t N_GetObject(IPluginContext* ctx, const cell_t* params)
        {
            const char* cls = LocalString(ctx, params[2]);
            const char* name = LocalString(ctx, params[3]);
            const Field& f = FindField(cls, name);
            if (!f.found || (f.kind != Kind::Pointer && f.kind != Kind::Embedded))
                return ctx->ThrowNativeError("Field %s::%s is not a schema object", cls, name);
            if (params[1] == 0)
                return ctx->ThrowNativeError("Null schema object");

            const SchemaRef* parent = nullptr;
            void* base = Base(ctx, params[1], &parent);
            if (!base || !FollowStep(base, cls, name))
                return 0;

            std::vector<std::pair<std::string, std::string>> chain;
            if (parent)
                chain = parent->chain;
            chain.emplace_back(cls, name);

            if (parent)
                return RefFor(parent->root, parent->entity, parent->raw, std::move(chain));
            CEntityInstance* ent = static_cast<CEntityInstance*>(base);
            return RefFor(Root::Entity, ent->GetRefEHandle(), nullptr, std::move(chain));
        }

        cell_t N_GetVector(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Vector, "a vector"))
                return 0;
            cell_t* out;
            ctx->LocalToPhysAddr(params[4], &out);
            for (int i = 0; i < 3; i++)
                out[i] = sp_ftoc(reinterpret_cast<float*>(a.addr)[i]);
            return 0;
        }

        cell_t N_SetVector(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Vector, "a vector"))
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[4], &in);
            for (int i = 0; i < 3; i++)
                reinterpret_cast<float*>(a.addr)[i] = sp_ctof(in[i]);
            Changed(a);
            return 0;
        }

        cell_t N_GetColor(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Color, "a color"))
                return 0;
            cell_t* out;
            ctx->LocalToPhysAddr(params[4], &out);
            for (int i = 0; i < 4; i++)
                out[i] = a.addr[i];
            return 0;
        }

        cell_t N_SetColor(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Color, "a color"))
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[4], &in);
            for (int i = 0; i < 4; i++)
                a.addr[i] = static_cast<uint8_t>(in[i]);
            Changed(a);
            return 0;
        }

        cell_t N_GetString(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::StringPtr, "a string"))
                return 0;
            std::string value;
            if (a.field->kind == Kind::CharArray)
                value.assign(reinterpret_cast<const char*>(a.addr), strnlen(reinterpret_cast<const char*>(a.addr), static_cast<size_t>(a.field->size)));
            else if (const char* s = *reinterpret_cast<const char* const*>(a.addr))
                value = s;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[4], params[5], value.c_str(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_SetString(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::StringPtr, "a string"))
                return 0;
            if (a.field->kind != Kind::CharArray)
                return ctx->ThrowNativeError("Field %s::%s (%s) cannot be set from a script; only char arrays can",
                                             a.field->declClass.c_str(), a.field->name.c_str(), a.field->typeName.c_str());
            const char* src = LocalString(ctx, params[4]);
            const size_t len = std::min(strlen(src), static_cast<size_t>(a.field->size) - 1);
            memcpy(a.addr, src, len);
            a.addr[len] = '\0';
            Changed(a);
            return 0;
        }

        cell_t N_GetOffset(IPluginContext* ctx, const cell_t* params)
        {
            const Field& f = FindField(LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return f.found ? f.offset : -1;
        }

        // ---- CEntityInstance -----------------------------------------------------

        cell_t N_EntIsValid(IPluginContext*, const cell_t* params)
        {
            return InstanceAt(params[1]) != nullptr;
        }

        cell_t N_EntGetClassname(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            size_t written = 0;
            if (ent)
                ctx->StringToLocalUTF8(params[2], params[3], ent->GetClassname(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_EntGetName(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* ent = CheckEntity(ctx, params[1]);
            size_t written = 0;
            if (ent)
            {
                const char* name = ent->m_pEntity->m_name.String();
                ctx->StringToLocalUTF8(params[2], params[3], name ? name : "", &written);
            }
            return static_cast<cell_t>(written);
        }

        cell_t N_EntGetRefEHandle(IPluginContext* ctx, const cell_t* params)
        {
            return RefOf(CheckEntity(ctx, params[1]));
        }

        cell_t N_EntFromRefEHandle(IPluginContext*, const cell_t* params)
        {
            return IndexOf(FromRef(params[1]));
        }

        // ---- CBaseEntity and below ----------------------------------------------

        bool ReadVector(IPluginContext* ctx, cell_t addr, Vector& out)
        {
            cell_t* v;
            ctx->LocalToPhysAddr(addr, &v);
            if (v == ctx->GetNullRef(SP_NULL_VECTOR))
                return false;
            out = Vector(sp_ctof(v[0]), sp_ctof(v[1]), sp_ctof(v[2]));
            return true;
        }

        void WriteVector(IPluginContext* ctx, cell_t addr, float x, float y, float z)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            out[0] = sp_ftoc(x);
            out[1] = sp_ftoc(y);
            out[2] = sp_ftoc(z);
        }

        cell_t N_BaseFromIndex(IPluginContext*, const cell_t* params)
        {
            return InstanceAt(params[1]) ? params[1] : -1;
        }

        cell_t N_BaseCreateEntityByName(IPluginContext* ctx, const cell_t* params)
        {
            return IndexOf(CBaseEntity::CreateEntityByName(LocalString(ctx, params[1])));
        }

        // AcceptInput(input, activator = NULL_ENTITY, caller = NULL_ENTITY, value = "")
        cell_t N_BaseAcceptInput(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            bool ok1, ok2;
            CEntityInstance* activator = OptionalEntity(ctx, params[3], &ok1);
            CEntityInstance* caller = OptionalEntity(ctx, params[4], &ok2);
            if (ent && ok1 && ok2)
                ent->AcceptInput(LocalString(ctx, params[2]), activator, caller, LocalString(ctx, params[5]));
            return 0;
        }

        cell_t N_BaseGetAbsOrigin(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]))
            {
                const Vector v = ent->GetAbsOrigin();
                WriteVector(ctx, params[2], v.x, v.y, v.z);
            }
            return 0;
        }

        cell_t N_BaseDispatchSpawn(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]))
                ent->DispatchSpawn();
            return 0;
        }

        // Teleport(position = NULL_VECTOR, angles = NULL_VECTOR, velocity = NULL_VECTOR)
        cell_t N_BaseTeleport(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            if (!ent)
                return 0;
            Vector pos, ang, vel;
            const bool hasPos = ReadVector(ctx, params[2], pos);
            const bool hasAng = ReadVector(ctx, params[3], ang);
            const bool hasVel = ReadVector(ctx, params[4], vel);
            const QAngle angles(ang.x, ang.y, ang.z);
            ent->Teleport(hasPos ? &pos : nullptr, hasAng ? &angles : nullptr, hasVel ? &vel : nullptr);
            return 0;
        }

        cell_t N_BaseRemove(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]))
                ent->Remove();
            return 0;
        }

        cell_t N_BaseGetIndex(IPluginContext*, const cell_t* params)
        {
            return params[1];
        }

        cell_t N_ModelGetModelName(IPluginContext* ctx, const cell_t* params)
        {
            CBaseModelEntity* ent = As<CBaseModelEntity>(ctx, params[1]);
            size_t written = 0;
            if (ent)
            {
                const char* model = ent->GetModelName().String();
                ctx->StringToLocalUTF8(params[2], params[3], model ? model : "", &written);
            }
            return static_cast<cell_t>(written);
        }

        cell_t N_ModelGetEyePosition(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseModelEntity* ent = As<CBaseModelEntity>(ctx, params[1]))
            {
                const Vector v = ent->GetEyePosition();
                WriteVector(ctx, params[2], v.x, v.y, v.z);
            }
            return 0;
        }

        cell_t N_ModelSetModel(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseModelEntity* ent = As<CBaseModelEntity>(ctx, params[1]))
                ent->SetModel(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_ModelSetBodyGroup(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseModelEntity* ent = As<CBaseModelEntity>(ctx, params[1]))
                ent->SetBodyGroup(LocalString(ctx, params[2]), params[3]);
            return 0;
        }

        // ---- controllers ----------------------------------------------------------

        cell_t N_PCSetPawn(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]);
            CBasePlayerPawn* pawn = As<CBasePlayerPawn>(ctx, params[2]);
            if (pc && pawn)
                pc->SetPawn(pawn);
            return 0;
        }

        cell_t N_PCGetPawn(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]);
            return pc ? IndexOf(pc->GetPawn()) : -1;
        }

        template <void (CBasePlayerController::*Print)(const char*)>
        cell_t N_PCPrint(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]))
                (pc->*Print)(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PCIsBot(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]);
            return pc && pc->IsBot();
        }

        cell_t N_PCDisconnect(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]))
            {
                const char* reason = LocalString(ctx, params[3]);
                pc->Disconnect(static_cast<ENetworkDisconnectionReason>(params[2]), *reason ? reason : nullptr);
            }
            return 0;
        }

        cell_t N_PCExecuteClientCommand(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]))
                pc->ExecuteClientCommand(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PCExecuteClientCommandFromServer(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]))
                pc->ExecuteClientCommandFromServer(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_PCGetPlayerSlot(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]);
            return pc ? pc->GetPlayerSlot().Get() : -1;
        }

        cell_t N_PCGetUserID(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]);
            return pc ? pc->GetUserID() : -1;
        }

        cell_t N_PCGetSteamID(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]))
                ctx->StringToLocalUTF8(params[2], params[3], std::to_string(pc->GetSteamID()).c_str(), nullptr);
            return 0;
        }

        cell_t N_PCGetPlayerName(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]);
            size_t written = 0;
            if (pc)
                ctx->StringToLocalUTF8(params[2], params[3], pc->GetPlayerName(), &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_PCGetIpAddress(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]))
                ctx->StringToLocalUTF8(params[2], params[3], pc->GetIpAddress().Get(), nullptr);
            return 0;
        }

        cell_t N_PCReplicateConVar(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]))
                pc->ReplicateConVar(LocalString(ctx, params[2]), LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_CSFromSlot(IPluginContext*, const cell_t* params)
        {
            if (params[1] < 0 || params[1] >= 64)
                return -1;
            return IndexOf(CCSPlayerController::FromSlot(params[1]));
        }

        cell_t N_CSPrintToCenterHtml(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]))
                pc->PrintToCenterHtml(LocalString(ctx, params[2]), params[3], params[4] != 0);
            return 0;
        }

        cell_t N_CSRespawn(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]))
                pc->Respawn();
            return 0;
        }

        cell_t N_CSSwitchTeam(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]))
                pc->SwitchTeam(params[2]);
            return 0;
        }

        cell_t N_CSChangeTeam(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]))
                pc->ChangeTeam(params[2]);
            return 0;
        }

        cell_t N_CSGetPlayerPawn(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]);
            return pc ? IndexOf(pc->GetPlayerPawn()) : -1;
        }

        cell_t N_CSGetObserverPawn(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]);
            return pc ? IndexOf(pc->GetObserverPawn()) : -1;
        }

        // ---- pawns ----------------------------------------------------------------

        cell_t N_PawnCommitSuicide(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerPawn* pawn = As<CBasePlayerPawn>(ctx, params[1]))
                pawn->CommitSuicide(params[2] != 0, params[3] != 0);
            return 0;
        }

        cell_t N_PawnSnapViewAngles(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerPawn* pawn = As<CBasePlayerPawn>(ctx, params[1]);
            Vector v;
            if (pawn && ReadVector(ctx, params[2], v))
                pawn->SnapViewAngles(QAngle(v.x, v.y, v.z));
            return 0;
        }

        cell_t N_PawnGetController(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerPawn* pawn = As<CBasePlayerPawn>(ctx, params[1]);
            return pawn ? IndexOf(pawn->GetController()) : -1;
        }

        cell_t N_CSPawnGetEyeAngles(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerPawn* pawn = As<CCSPlayerPawn>(ctx, params[1]))
            {
                const QAngle a = pawn->GetEyeAngles();
                WriteVector(ctx, params[2], a.x, a.y, a.z);
            }
            return 0;
        }

        // ---- services and game rules (SchemaObjects) --------------------------------

        cell_t N_ItemsDropActive(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_ItemServices* items = ObjectOf<CPlayer_ItemServices>(ctx, params[1]);
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[2]);
            if (items && weapon)
                items->DropActivePlayerWeapon(weapon);
            return 0;
        }

        cell_t N_ItemsRemoveWeapons(IPluginContext* ctx, const cell_t* params)
        {
            if (CPlayer_ItemServices* items = ObjectOf<CPlayer_ItemServices>(ctx, params[1]))
                items->RemoveWeapons(params[2] != 0);
            return 0;
        }

        cell_t N_ItemsGiveNamedItem(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_ItemServices* items = ObjectOf<CPlayer_ItemServices>(ctx, params[1]);
            return items ? IndexOf(items->GiveNamedItem(LocalString(ctx, params[2]))) : -1;
        }

        cell_t N_WeaponsCanUse(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_WeaponServices* ws = ObjectOf<CPlayer_WeaponServices>(ctx, params[1]);
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[2]);
            return ws && weapon && ws->CanUse(weapon);
        }

        cell_t N_WeaponsDropWeapon(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_WeaponServices* ws = ObjectOf<CPlayer_WeaponServices>(ctx, params[1]);
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[2]);
            if (!ws || !weapon)
                return 0;
            Vector target, velocity;
            const bool hasTarget = ReadVector(ctx, params[3], target);
            const bool hasVelocity = ReadVector(ctx, params[4], velocity);
            ws->DropWeapon(weapon, hasTarget ? &target : nullptr, hasVelocity ? &velocity : nullptr);
            return 0;
        }

        cell_t N_WeaponsBumpWeapon(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_WeaponServices* ws = ObjectOf<CPlayer_WeaponServices>(ctx, params[1]);
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[2]);
            return ws && weapon ? ws->BumpWeapon(weapon) : 0;
        }

        cell_t N_WeaponsSelectItem(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_WeaponServices* ws = ObjectOf<CPlayer_WeaponServices>(ctx, params[1]);
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[2]);
            if (ws && weapon)
                ws->SelectItem(weapon);
            return 0;
        }

        cell_t N_RulesTerminateRound(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSGameRules* rules = ObjectOf<CCSGameRules>(ctx, params[1]))
                rules->TerminateRound(sp_ctof(params[2]), params[3], static_cast<uint32>(params[4]));
            return 0;
        }

        // ---- Source2ModEntities ---------------------------------------------------

        cell_t N_EntsGetGameRules(IPluginContext*, const cell_t*)
        {
            if (!entities::entitiesManager.GetGameRules())
                return 0;
            return RefFor(Root::GameRules, CEntityHandle(), nullptr, {});
        }

        cell_t N_EntsFindPickerEntity(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]);
            return pc ? IndexOf(entities::entitiesManager.FindPickerEntity(pc)) : -1;
        }

        cell_t N_EntsFindEntityByClassname(IPluginContext* ctx, const cell_t* params)
        {
            bool ok;
            CEntityInstance* start = OptionalEntity(ctx, params[1], &ok);
            return ok ? IndexOf(entities::entitiesManager.FindEntityByClassname(start, LocalString(ctx, params[2]))) : -1;
        }

        cell_t N_EntsFindEntityByName(IPluginContext* ctx, const cell_t* params)
        {
            bool ok;
            CEntityInstance* start = OptionalEntity(ctx, params[1], &ok);
            return ok ? IndexOf(entities::entitiesManager.FindEntityByName(start, LocalString(ctx, params[2]))) : -1;
        }

        cell_t N_EntsCreateEntityByName(IPluginContext* ctx, const cell_t* params)
        {
            return IndexOf(entities::entitiesManager.CreateEntityByName(LocalString(ctx, params[1])));
        }

        // AcceptInput(target, input, activator, caller, value)
        cell_t N_EntsAcceptInput(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* target = CheckEntity(ctx, params[1]);
            bool ok1, ok2;
            CEntityInstance* activator = OptionalEntity(ctx, params[3], &ok1);
            CEntityInstance* caller = OptionalEntity(ctx, params[4], &ok2);
            if (target && ok1 && ok2)
                entities::entitiesManager.AcceptInput(target, LocalString(ctx, params[2]), activator, caller, LocalString(ctx, params[5]));
            return 0;
        }

        cell_t N_EntsAddEntityIOEvent(IPluginContext* ctx, const cell_t* params)
        {
            CEntityInstance* target = CheckEntity(ctx, params[1]);
            bool ok1, ok2;
            CEntityInstance* activator = OptionalEntity(ctx, params[3], &ok1);
            CEntityInstance* caller = OptionalEntity(ctx, params[4], &ok2);
            if (target && ok1 && ok2)
                entities::entitiesManager.AddEntityIOEvent(target, LocalString(ctx, params[2]), activator, caller,
                                                           LocalString(ctx, params[5]), sp_ctof(params[6]));
            return 0;
        }

        // A script's IEntityListener: four optional functions.
        class ScriptEntityListener final : public IEntityListener
        {
        public:
            PluginId owner = 0;
            IPluginFunction* created = nullptr;
            IPluginFunction* spawned = nullptr;
            IPluginFunction* deleted = nullptr;
            IPluginFunction* parentChanged = nullptr;

            void OnEntityCreated(CEntityInstance* e) override { Fire(created, e); }
            void OnEntitySpawned(CEntityInstance* e) override { Fire(spawned, e); }
            void OnEntityDeleted(CEntityInstance* e) override { Fire(deleted, e); }
            void OnEntityParentChanged(CEntityInstance* e, CEntityInstance* parent) override
            {
                if (!parentChanged)
                    return;
                sp::CallArgs args;
                args.PushCell(IndexOf(e));
                args.PushCell(IndexOf(parent));
                Invoke(parentChanged, args);
            }

        private:
            static void Fire(IPluginFunction* fn, CEntityInstance* e)
            {
                if (!fn)
                    return;
                sp::CallArgs args;
                args.PushCell(IndexOf(e));
                Invoke(fn, args);
            }
        };

        class ScriptIOListener final : public IEntityIOListener
        {
        public:
            PluginId owner = 0;
            IPluginFunction* fn = nullptr;
            std::string className, outputName;
            bool post = false;

            Action OnEntityOutput(const char* output, CEntityInstance* activator, CEntityInstance* caller, float delay, bool isPost) override
            {
                sp::CallArgs args;
                args.PushString(output ? output : "");
                args.PushCell(IndexOf(activator));
                args.PushCell(IndexOf(caller));
                args.PushFloat(delay);
                args.PushCell(isPost ? 1 : 0);
                cell_t res = 0;
                Invoke(fn, args, &res);
                return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
            }
        };

        cell_t g_nextListenerId = 1;
        std::map<cell_t, std::unique_ptr<ScriptEntityListener>> g_entityListeners;
        std::map<cell_t, std::unique_ptr<ScriptIOListener>> g_ioListeners;

        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        IPluginFunction* OptionalFunction(IPluginContext* ctx, cell_t id)
        {
            return id == 0 || id == -1 ? nullptr : ctx->GetFunctionById(static_cast<funcid_t>(id));
        }

        cell_t N_EntsAddEntityListener(IPluginContext* ctx, const cell_t* params)
        {
            auto listener = std::make_unique<ScriptEntityListener>();
            listener->owner = CallerId(ctx);
            listener->created = OptionalFunction(ctx, params[1]);
            listener->spawned = OptionalFunction(ctx, params[2]);
            listener->deleted = OptionalFunction(ctx, params[3]);
            listener->parentChanged = OptionalFunction(ctx, params[4]);
            entities::entitiesManager.AddEntityListener(listener->owner, listener.get());
            const cell_t id = g_nextListenerId++;
            g_entityListeners[id] = std::move(listener);
            return id;
        }

        cell_t N_EntsRemoveEntityListener(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_entityListeners.find(params[1]);
            if (it == g_entityListeners.end() || it->second->owner != CallerId(ctx))
                return 0;
            entities::entitiesManager.RemoveEntityListener(it->second.get());
            g_entityListeners.erase(it);
            return 1;
        }

        // AddEntityIOListener(handler, className, outputName, post)
        cell_t N_EntsAddEntityIOListener(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[1]));
            if (!fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[1]);
            auto listener = std::make_unique<ScriptIOListener>();
            listener->owner = CallerId(ctx);
            listener->fn = fn;
            listener->className = LocalString(ctx, params[2]);
            listener->outputName = LocalString(ctx, params[3]);
            listener->post = params[4] != 0;
            entities::entitiesManager.AddEntityIOListener(listener->owner, listener.get(),
                                                          listener->className.empty() ? nullptr : listener->className.c_str(),
                                                          listener->outputName.empty() ? nullptr : listener->outputName.c_str(), listener->post);
            const cell_t id = g_nextListenerId++;
            g_ioListeners[id] = std::move(listener);
            return id;
        }

        cell_t N_EntsRemoveEntityIOListener(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_ioListeners.find(params[1]);
            if (it == g_ioListeners.end() || it->second->owner != CallerId(ctx))
                return 0;
            ScriptIOListener* l = it->second.get();
            entities::entitiesManager.RemoveEntityIOListener(l, l->className.empty() ? nullptr : l->className.c_str(),
                                                             l->outputName.empty() ? nullptr : l->outputName.c_str(), l->post);
            g_ioListeners.erase(it);
            return 1;
        }

        const sp_nativeinfo_t g_entityNatives[] = {
            // source2mod/schema.inc
            { "Source2ModSchema.GetInt", N_GetInt },
            { "Source2ModSchema.SetInt", N_SetInt },
            { "Source2ModSchema.GetFloat", N_GetFloat },
            { "Source2ModSchema.SetFloat", N_SetFloat },
            { "Source2ModSchema.GetBool", N_GetBool },
            { "Source2ModSchema.SetBool", N_SetBool },
            { "Source2ModSchema.GetHandle", N_GetHandle },
            { "Source2ModSchema.SetHandle", N_SetHandle },
            { "Source2ModSchema.GetEntityPointer", N_GetEntityPointer },
            { "Source2ModSchema.GetObject", N_GetObject },
            { "Source2ModSchema.GetVector", N_GetVector },
            { "Source2ModSchema.SetVector", N_SetVector },
            { "Source2ModSchema.GetColor", N_GetColor },
            { "Source2ModSchema.SetColor", N_SetColor },
            { "Source2ModSchema.GetString", N_GetString },
            { "Source2ModSchema.SetString", N_SetString },
            { "Source2ModSchema.GetOffset", N_GetOffset },
            { "CEntityInstance.IsValid", N_EntIsValid },
            { "CEntityInstance.GetClassname", N_EntGetClassname },
            { "CEntityInstance.GetName", N_EntGetName },
            { "CEntityInstance.GetRefEHandle", N_EntGetRefEHandle },
            { "CEntityInstance.FromRefEHandle", N_EntFromRefEHandle },
            // schema/classes.inc, from tools/sp_schemagen/methods.inc
            { "CBaseEntity.FromIndex", N_BaseFromIndex },
            { "CBaseEntity.CreateEntityByName", N_BaseCreateEntityByName },
            { "CBaseEntity.AcceptInput", N_BaseAcceptInput },
            { "CBaseEntity.GetAbsOrigin", N_BaseGetAbsOrigin },
            { "CBaseEntity.DispatchSpawn", N_BaseDispatchSpawn },
            { "CBaseEntity.Teleport", N_BaseTeleport },
            { "CBaseEntity.Remove", N_BaseRemove },
            { "CBaseEntity.GetIndex", N_BaseGetIndex },
            { "CBaseModelEntity.GetModelName", N_ModelGetModelName },
            { "CBaseModelEntity.GetEyePosition", N_ModelGetEyePosition },
            { "CBaseModelEntity.SetModel", N_ModelSetModel },
            { "CBaseModelEntity.SetBodyGroup", N_ModelSetBodyGroup },
            { "CBasePlayerController.SetPawn", N_PCSetPawn },
            { "CBasePlayerController.GetPawn", N_PCGetPawn },
            { "CBasePlayerController.PrintToConsole", N_PCPrint<&CBasePlayerController::PrintToConsole> },
            { "CBasePlayerController.PrintToChat", N_PCPrint<&CBasePlayerController::PrintToChat> },
            { "CBasePlayerController.PrintToCenter", N_PCPrint<&CBasePlayerController::PrintToCenter> },
            { "CBasePlayerController.PrintToCenterAlert", N_PCPrint<&CBasePlayerController::PrintToCenterAlert> },
            { "CBasePlayerController.IsBot", N_PCIsBot },
            { "CBasePlayerController.Disconnect", N_PCDisconnect },
            { "CBasePlayerController.ExecuteClientCommand", N_PCExecuteClientCommand },
            { "CBasePlayerController.ExecuteClientCommandFromServer", N_PCExecuteClientCommandFromServer },
            { "CBasePlayerController.GetPlayerSlot", N_PCGetPlayerSlot },
            { "CBasePlayerController.GetSlot", N_PCGetPlayerSlot },
            { "CBasePlayerController.GetUserID", N_PCGetUserID },
            { "CBasePlayerController.GetSteamID", N_PCGetSteamID },
            { "CBasePlayerController.GetPlayerName", N_PCGetPlayerName },
            { "CBasePlayerController.GetIpAddress", N_PCGetIpAddress },
            { "CBasePlayerController.ReplicateConVar", N_PCReplicateConVar },
            { "CCSPlayerController.FromSlot", N_CSFromSlot },
            { "CCSPlayerController.PrintToCenterHtml", N_CSPrintToCenterHtml },
            { "CCSPlayerController.Respawn", N_CSRespawn },
            { "CCSPlayerController.SwitchTeam", N_CSSwitchTeam },
            { "CCSPlayerController.ChangeTeam", N_CSChangeTeam },
            { "CCSPlayerController.GetPlayerPawn", N_CSGetPlayerPawn },
            { "CCSPlayerController.GetObserverPawn", N_CSGetObserverPawn },
            { "CBasePlayerPawn.CommitSuicide", N_PawnCommitSuicide },
            { "CBasePlayerPawn.SnapViewAngles", N_PawnSnapViewAngles },
            { "CBasePlayerPawn.GetController", N_PawnGetController },
            { "CCSPlayerPawn.GetEyeAngles", N_CSPawnGetEyeAngles },
            { "CPlayer_ItemServices.DropActivePlayerWeapon", N_ItemsDropActive },
            { "CPlayer_ItemServices.RemoveWeapons", N_ItemsRemoveWeapons },
            { "CPlayer_ItemServices.GiveNamedItem", N_ItemsGiveNamedItem },
            { "CPlayer_WeaponServices.CanUse", N_WeaponsCanUse },
            { "CPlayer_WeaponServices.DropWeapon", N_WeaponsDropWeapon },
            { "CPlayer_WeaponServices.BumpWeapon", N_WeaponsBumpWeapon },
            { "CPlayer_WeaponServices.SelectItem", N_WeaponsSelectItem },
            { "CCSGameRules.TerminateRound", N_RulesTerminateRound },
            // source2mod/entities.inc
            { "Source2ModEntities.GetGameRules", N_EntsGetGameRules },
            { "Source2ModEntities.FindPickerEntity", N_EntsFindPickerEntity },
            { "Source2ModEntities.FindEntityByClassname", N_EntsFindEntityByClassname },
            { "Source2ModEntities.FindEntityByName", N_EntsFindEntityByName },
            { "Source2ModEntities.CreateEntityByName", N_EntsCreateEntityByName },
            { "Source2ModEntities.AddEntityListener", N_EntsAddEntityListener },
            { "Source2ModEntities.RemoveEntityListener", N_EntsRemoveEntityListener },
            { "Source2ModEntities.AcceptInput", N_EntsAcceptInput },
            { "Source2ModEntities.AddEntityIOEvent", N_EntsAddEntityIOEvent },
            { "Source2ModEntities.AddEntityIOListener", N_EntsAddEntityIOListener },
            { "Source2ModEntities.RemoveEntityIOListener", N_EntsRemoveEntityIOListener },
            { nullptr, nullptr },
        };
    }

    void* SchemaObjectPointer(IPluginContext* ctx, cell_t handle)
    {
        return Base(ctx, handle);
    }

    cell_t SchemaObjectForPointer(void* ptr)
    {
        // A pointer the game handed back, with nothing to find it again by:
        // good for as long as the object it points at.
        return ptr ? RefFor(Root::Raw, CEntityHandle(), ptr, {}) : 0;
    }

    void ForgetEntityListeners(PluginId id)
    {
        // The manager took them off the engine already (RemoveAllForPlugin).
        std::erase_if(g_entityListeners, [id](const auto& e) { return e.second->owner == id; });
        std::erase_if(g_ioListeners, [id](const auto& e) { return e.second->owner == id; });
    }

    const sp_nativeinfo_t* GetEntityNatives()
    {
        return g_entityNatives;
    }
}
