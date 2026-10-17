/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 * gives you permission to link the code of this program
 * (as well as its derivative works) to "Counter-Strike 2," "Source 2,"
 * "Steam," and any Game MODs or server software running on software by
 * Valve Corporation. You must obey the GNU General Public License in all
 * respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */

/*
 * The schema and entity part of the SourcePawn API (source2mod/schema.inc,
 * source2mod/entities.inc): Source2ModSchema's generic field access behind
 * the generated class methodmaps, the C++ helper methods of the schema
 * classes, and Source2ModEntities.
 *
 * An entity is an entity handle (index + serial, see EntityCell): a value
 * kept after its entity is removed reads as gone instead of finding whatever
 * reuses the index. Anything else the schema reaches -- services,
 * the scene node, the game rules -- is a SchemaObject: a handle the core
 * keeps, which remembers how it was reached (an entity and the fields from
 * it, or the game rules) and finds the object again on every use, so a
 * stale one reads as gone instead of reading freed memory.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/entities.h"
#include "utils/log.h"

#include <source2mod/ISource2ModEntities.h>
#include <source2mod/schema/recipientfilter.h>
#include <source2mod/schema/schema.h>
#include <source2mod/schema/entity/classes/CAttributeList.h>
#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CBaseModelEntity.h>
#include <source2mod/schema/entity/classes/CBasePlayerWeapon.h>
#include <source2mod/schema/entity/classes/CCSCustomHudLayout.h>
#include <source2mod/schema/entity/classes/CCSGameRules.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawnBase.h>
#include <source2mod/schema/entity/classes/CCSObserverPawn.h>
#include <source2mod/schema/entity/classes/CCSWeaponBaseVData.h>
#include <source2mod/schema/entity/classes/CDecoyProjectile.h>
#include <source2mod/schema/entity/classes/CFlashbangProjectile.h>
#include <source2mod/schema/entity/classes/CGameSceneNode.h>
#include <source2mod/schema/entity/classes/CHEGrenadeProjectile.h>
#include <source2mod/schema/entity/classes/CMolotovProjectile.h>
#include <source2mod/schema/entity/classes/CPlayerControllerComponent.h>
#include <source2mod/schema/entity/classes/CPlayerPawnComponent.h>
#include <source2mod/schema/entity/classes/CPlayer_ItemServices.h>
#include <source2mod/schema/entity/classes/CPlayer_WeaponServices.h>
#include <source2mod/schema/entity/classes/CSkeletonInstance.h>
#include <source2mod/schema/entity/classes/CSmokeGrenadeProjectile.h>
#include <source2mod/utils/plat.h>

#include <entity2/entitykeyvalues.h>

#include <schemalib/schemaclassinfo.h>
#include <schemalib/schematype.h>
#include <schemasystem/schemasystem.h>
#include <sp_callable.h>

#include <algorithm>
#include <cstdlib>
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
        CEntityInstance* CheckEntity(IPluginContext* ctx, cell_t entity)
        {
            return EntityArg(ctx, entity);
        }

        // An entity argument that may be NULL_ENTITY (-1).
        CEntityInstance* OptionalEntity(IPluginContext* ctx, cell_t entity, bool* ok)
        {
            return OptionalEntityArg(ctx, entity, ok);
        }

        template <typename T>
        T* As(IPluginContext* ctx, cell_t entity)
        {
            return static_cast<T*>(CheckEntity(ctx, entity));
        }

        // The optional trailing Source2ModHookChain argument at params[n]:
        // HookChain_Default (-1), or a script built before it existed, keeps
        // the C++ helper's own default.
        HookChain ChainArg(const cell_t* params, int n, HookChain def = HookChain::Run)
        {
            if (params[0] < n || params[n] < 0)
                return def;
            return params[n] ? HookChain::Bypass : HookChain::Run;
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
            Array,          // a fixed array of anything but char (elem* below)
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
            // Kind::Array: how many elements, and what one of them is.
            int count = 0;
            int elemSize = 0;
            bool elemSigned = true;
            Kind elemKind = Kind::Other;
            std::string elemTypeName;
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
                if (!arr->m_pElementType)
                    return;
                if (strcmp(arr->m_pElementType->m_sTypeName.Get(), "char") == 0)
                {
                    f.kind = Kind::CharArray;
                    return;
                }
                Field elem;
                Classify(arr->m_pElementType, elem);
                f.kind = Kind::Array;
                f.count = arr->m_nElementCount;
                f.elemSize = arr->m_nElementSize;
                f.elemSigned = elem.isSigned;
                f.elemKind = elem.kind;
                f.elemTypeName = elem.typeName;
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

        // What an `instance` argument points at: an entity (handle), or a SchemaObject.
        void* Base(IPluginContext* ctx, cell_t instance, const SchemaRef** ref = nullptr)
        {
            if (ref)
                *ref = nullptr;
            if (IsEntityCell(instance))
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
        void Changed(const Access& a, int extra = 0)
        {
            const char* cls = a.field->declClass.c_str();
            const char* name = a.field->name.c_str();
            const SchemaKey key = schema::GetOffset(cls, hash_32_fnv1a_const(cls), name, hash_32_fnv1a_const(name));
            if (!key.valid || !key.networked)
                return;
            const int16_t chain = schema::FindChainOffset(cls);
            if (chain != 0)
                ChainNetworkStateChanged(reinterpret_cast<uintptr_t>(a.base) + chain, key.offset + extra);
            else
                EntityNetworkStateChanged(reinterpret_cast<uintptr_t>(a.base), key.offset + extra);
        }

        // Element `params[index]` of a fixed array field (instance, class,
        // field, index, ...): a.addr is the element, `extra` its offset in
        // the field. Throws on a field that is no such array, a wrong element
        // type or an index outside the array.
        bool LookupElement(IPluginContext* ctx, const cell_t* params, Access& a, int& extra, Kind want, const char* what)
        {
            const char* cls = LocalString(ctx, params[2]);
            const char* name = LocalString(ctx, params[3]);
            const Field& f = FindField(cls, name);
            if (!f.found)
            {
                ctx->ThrowNativeError("No field %s::%s in the running game's schema", cls, name);
                return false;
            }
            const bool ok = f.kind == Kind::Array
                && (f.elemKind == want || (want == Kind::Int && f.elemKind == Kind::Bool));
            if (!ok)
            {
                ctx->ThrowNativeError("Field %s::%s is %s, not an array of %s", cls, name, f.typeName.c_str(), what);
                return false;
            }
            const cell_t index = params[4];
            if (index < 0 || index >= f.count)
            {
                ctx->ThrowNativeError("Index %d out of bounds for %s::%s (%d elements)", index, cls, name, f.count);
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
            extra = index * f.elemSize;
            a.base = static_cast<uint8_t*>(base);
            a.addr = a.base + f.offset + extra;
            a.field = &f;
            return true;
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
        // A 64-bit field does not fit a cell: GetInt64 / SetInt64.
        bool Not64(IPluginContext* ctx, const Access& a, const char* use)
        {
            if (a.field->size != 8)
                return true;
            ctx->ThrowNativeError("Field %s::%s is 64-bit: use %s", a.field->declClass.c_str(), a.field->name.c_str(), use);
            return false;
        }

        cell_t N_GetInt(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            return Lookup(ctx, params, a, Kind::Int, "an integer") && Not64(ctx, a, "GetInt64") ? ReadInt(a.addr, a.field->size, a.field->isSigned) : 0;
        }

        cell_t N_SetInt(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (Lookup(ctx, params, a, Kind::Int, "an integer") && Not64(ctx, a, "SetInt64"))
            {
                WriteInt(a.addr, a.field->size, a.field->kind == Kind::Bool ? (params[4] != 0) : params[4]);
                Changed(a);
            }
            return 0;
        }

        // Source2ModSchema.GetInt64 / SetInt64: a 64-bit integer field as two
        // cells, value[0] the low half (GetInt would lose the high one).
        cell_t N_GetInt64(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Int, "an integer"))
                return 0;
            if (a.field->size != 8)
                return ctx->ThrowNativeError("Field %s::%s is %d bytes, not 64-bit: use GetInt",
                                             a.field->declClass.c_str(), a.field->name.c_str(), a.field->size);
            const uint64_t v = *reinterpret_cast<const uint64_t*>(a.addr);
            cell_t* out;
            ctx->LocalToPhysAddr(params[4], &out);
            out[0] = static_cast<cell_t>(v & 0xFFFFFFFFu);
            out[1] = static_cast<cell_t>(v >> 32);
            return 0;
        }

        cell_t N_SetInt64(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            if (!Lookup(ctx, params, a, Kind::Int, "an integer"))
                return 0;
            if (a.field->size != 8)
                return ctx->ThrowNativeError("Field %s::%s is %d bytes, not 64-bit: use SetInt",
                                             a.field->declClass.c_str(), a.field->name.c_str(), a.field->size);
            cell_t* in;
            ctx->LocalToPhysAddr(params[4], &in);
            *reinterpret_cast<uint64_t*>(a.addr) = static_cast<uint32_t>(in[0]) | (static_cast<uint64_t>(static_cast<uint32_t>(in[1])) << 32);
            Changed(a);
            return 0;
        }

        // ---- fixed arrays (SCHEMA_FIELD_POINTER in C++) ------------------------------

        // Source2ModSchema.GetArraySize(className, field): elements of a fixed
        // array field, 0 for anything else.
        cell_t N_GetArraySize(IPluginContext* ctx, const cell_t* params)
        {
            const Field& f = FindField(LocalString(ctx, params[1]), LocalString(ctx, params[2]));
            return f.found && f.kind == Kind::Array ? f.count : 0;
        }

        cell_t N_GetArrayInt(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Int, "integers"))
                return 0;
            if (a.field->elemSize == 8)
                return ctx->ThrowNativeError("Elements of %s::%s are 64-bit: use GetArrayInt64", a.field->declClass.c_str(), a.field->name.c_str());
            if (a.field->elemKind == Kind::Bool)
                return *a.addr != 0;
            return ReadInt(a.addr, a.field->elemSize, a.field->elemSigned);
        }

        cell_t N_SetArrayInt(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Int, "integers"))
                return 0;
            if (a.field->elemSize == 8)
                return ctx->ThrowNativeError("Elements of %s::%s are 64-bit: use SetArrayInt64", a.field->declClass.c_str(), a.field->name.c_str());
            if (a.field->elemKind == Kind::Bool)
                *a.addr = params[5] ? 1 : 0;
            else
                WriteInt(a.addr, a.field->elemSize, params[5]);
            Changed(a, extra);
            return 0;
        }

        cell_t N_GetArrayInt64(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Int, "integers"))
                return 0;
            if (a.field->elemSize != 8)
                return ctx->ThrowNativeError("Elements of %s::%s are not 64-bit: use GetArrayInt", a.field->declClass.c_str(), a.field->name.c_str());
            const uint64_t v = *reinterpret_cast<const uint64_t*>(a.addr);
            cell_t* out;
            ctx->LocalToPhysAddr(params[5], &out);
            out[0] = static_cast<cell_t>(v & 0xFFFFFFFFu);
            out[1] = static_cast<cell_t>(v >> 32);
            return 0;
        }

        cell_t N_SetArrayInt64(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Int, "integers"))
                return 0;
            if (a.field->elemSize != 8)
                return ctx->ThrowNativeError("Elements of %s::%s are not 64-bit: use SetArrayInt", a.field->declClass.c_str(), a.field->name.c_str());
            cell_t* in;
            ctx->LocalToPhysAddr(params[5], &in);
            *reinterpret_cast<uint64_t*>(a.addr) = static_cast<uint32_t>(in[0]) | (static_cast<uint64_t>(static_cast<uint32_t>(in[1])) << 32);
            Changed(a, extra);
            return 0;
        }

        cell_t N_GetArrayFloat(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Float, "floats"))
                return 0;
            const float v = a.field->elemSize == 8 ? static_cast<float>(*reinterpret_cast<double*>(a.addr)) : *reinterpret_cast<float*>(a.addr);
            return sp_ftoc(v);
        }

        cell_t N_SetArrayFloat(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Float, "floats"))
                return 0;
            if (a.field->elemSize == 8)
                *reinterpret_cast<double*>(a.addr) = sp_ctof(params[5]);
            else
                *reinterpret_cast<float*>(a.addr) = sp_ctof(params[5]);
            Changed(a, extra);
            return 0;
        }

        cell_t N_GetArrayVector(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Vector, "vectors"))
                return 0;
            cell_t* out;
            ctx->LocalToPhysAddr(params[5], &out);
            for (int i = 0; i < 3; i++)
                out[i] = sp_ftoc(reinterpret_cast<float*>(a.addr)[i]);
            return 0;
        }

        cell_t N_SetArrayVector(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Vector, "vectors"))
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[5], &in);
            for (int i = 0; i < 3; i++)
                reinterpret_cast<float*>(a.addr)[i] = sp_ctof(in[i]);
            Changed(a, extra);
            return 0;
        }

        cell_t N_GetArrayHandle(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Handle, "entity handles"))
                return -1;
            const CEntityHandle h = *reinterpret_cast<CEntityHandle*>(a.addr);
            return EntityCell(h.IsValid() ? GetEntitySystem()->GetEntityInstance(h) : nullptr);
        }

        cell_t N_SetArrayHandle(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::Handle, "entity handles"))
                return 0;
            bool ok;
            CEntityInstance* other = OptionalEntity(ctx, params[5], &ok);
            if (!ok)
                return 0;
            *reinterpret_cast<CEntityHandle*>(a.addr) = other ? other->GetRefEHandle() : CEntityHandle();
            Changed(a, extra);
            return 0;
        }

        cell_t N_GetArrayString(IPluginContext* ctx, const cell_t* params)
        {
            Access a;
            int extra;
            if (!LookupElement(ctx, params, a, extra, Kind::StringPtr, "strings"))
                return 0;
            const char* s = *reinterpret_cast<const char* const*>(a.addr);
            size_t written = 0;
            ctx->StringToLocalUTF8(params[5], params[6], s ? s : "", &written);
            return static_cast<cell_t>(written);
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
            return EntityCell(h.IsValid() ? GetEntitySystem()->GetEntityInstance(h) : nullptr);
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
            return EntityCell(*reinterpret_cast<CEntityInstance**>(a.addr));
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

        // StateChanged(instance, className, field): NetworkStateChanged for a field
        // written behind the schema's back (a raw call, raw memory, an array element).
        cell_t N_StateChanged(IPluginContext* ctx, const cell_t* params)
        {
            const char* cls = LocalString(ctx, params[2]);
            const char* name = LocalString(ctx, params[3]);
            const Field& f = FindField(cls, name);
            if (!f.found)
                return ctx->ThrowNativeError("No field %s::%s in the running game's schema", cls, name);
            if (params[1] == 0)
                return ctx->ThrowNativeError("Null schema object");
            Access a;
            a.base = static_cast<uint8_t*>(Base(ctx, params[1]));
            if (!a.base)
                return 0;
            a.addr = a.base + f.offset;
            a.field = &f;
            Changed(a);
            return 0;
        }

        // ---- raw memory at an offset (ISource2ModMemory's GetValue / Offset / Deref) --

        bool ReadVector(IPluginContext* ctx, cell_t addr, Vector& out);
        void WriteVector(IPluginContext* ctx, cell_t addr, float x, float y, float z);

        // The address `offset` bytes into an entity or SchemaObject.
        uint8_t* At(IPluginContext* ctx, cell_t instance, cell_t offset)
        {
            if (instance == 0)
            {
                ctx->ThrowNativeError("Null schema object");
                return nullptr;
            }
            if (offset < 0)
            {
                ctx->ThrowNativeError("Negative offset %d", offset);
                return nullptr;
            }
            auto* base = static_cast<uint8_t*>(Base(ctx, instance));
            return base ? base + offset : nullptr;
        }

        int IntSize(IPluginContext* ctx, cell_t size)
        {
            if (size != 1 && size != 2 && size != 4)
            {
                ctx->ThrowNativeError("Integer size %d is not 1, 2 or 4", size);
                return 0;
            }
            return size;
        }

        // GetIntAt(instance, offset, size = 4, bool isSigned = true)
        cell_t N_GetIntAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            const int size = IntSize(ctx, params[3]);
            return p && size ? ReadInt(p, size, params[4] != 0) : 0;
        }

        // SetIntAt(instance, offset, value, size = 4)
        cell_t N_SetIntAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            const int size = IntSize(ctx, params[4]);
            if (p && size)
                WriteInt(p, size, params[3]);
            return 0;
        }

        cell_t N_GetInt64At(IPluginContext* ctx, const cell_t* params)
        {
            if (uint8_t* p = At(ctx, params[1], params[2]))
            {
                const uint64_t v = *reinterpret_cast<const uint64_t*>(p);
                cell_t* out;
                ctx->LocalToPhysAddr(params[3], &out);
                out[0] = static_cast<cell_t>(v & 0xFFFFFFFFu);
                out[1] = static_cast<cell_t>(v >> 32);
            }
            return 0;
        }

        cell_t N_SetInt64At(IPluginContext* ctx, const cell_t* params)
        {
            if (uint8_t* p = At(ctx, params[1], params[2]))
            {
                cell_t* in;
                ctx->LocalToPhysAddr(params[3], &in);
                *reinterpret_cast<uint64_t*>(p) = static_cast<uint32_t>(in[0]) | (static_cast<uint64_t>(static_cast<uint32_t>(in[1])) << 32);
            }
            return 0;
        }

        cell_t N_GetFloatAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            return p ? sp_ftoc(*reinterpret_cast<const float*>(p)) : 0;
        }

        cell_t N_SetFloatAt(IPluginContext* ctx, const cell_t* params)
        {
            if (uint8_t* p = At(ctx, params[1], params[2]))
                *reinterpret_cast<float*>(p) = sp_ctof(params[3]);
            return 0;
        }

        cell_t N_GetVectorAt(IPluginContext* ctx, const cell_t* params)
        {
            if (uint8_t* p = At(ctx, params[1], params[2]))
            {
                const auto* v = reinterpret_cast<const float*>(p);
                WriteVector(ctx, params[3], v[0], v[1], v[2]);
            }
            return 0;
        }

        cell_t N_SetVectorAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            Vector v;
            if (p && ReadVector(ctx, params[3], v))
                *reinterpret_cast<Vector*>(p) = v;
            return 0;
        }

        // A CHandle at the offset, as the entity.
        cell_t N_GetEntityAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            if (!p)
                return -1;
            const CEntityHandle h = *reinterpret_cast<const CEntityHandle*>(p);
            return EntityCell(h.IsValid() ? GetEntitySystem()->GetEntityInstance(h) : nullptr);
        }

        cell_t N_SetEntityAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            bool ok;
            CEntityInstance* other = OptionalEntity(ctx, params[3], &ok);
            if (p && ok)
                *reinterpret_cast<CEntityHandle*>(p) = other ? other->GetRefEHandle() : CEntityHandle();
            return 0;
        }

        // The pointer stored at the offset (Deref), as a SchemaObject; 0 for null.
        cell_t N_GetObjectAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            return p ? SchemaObjectForPointer(*reinterpret_cast<void**>(p)) : 0;
        }

        // The object embedded at the offset (Offset), as a SchemaObject.
        cell_t N_GetEmbeddedAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            return p ? SchemaObjectForPointer(p) : 0;
        }

        // A char* stored at the offset (CUtlSymbolLarge, CUtlString, const char*).
        cell_t N_GetStringPointerAt(IPluginContext* ctx, const cell_t* params)
        {
            uint8_t* p = At(ctx, params[1], params[2]);
            const char* s = p ? *reinterpret_cast<const char* const*>(p) : nullptr;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[3], params[4], s ? s : "", &written);
            return static_cast<cell_t>(written);
        }

        // ---- CUtlVector fields -------------------------------------------------------

        // A CUtlVector field (or a networked one, laid out the same: count, then
        // the element pointer): the vector itself.
        CUtlVector<uint8_t>* UtlVectorOf(IPluginContext* ctx, const cell_t* params)
        {
            const char* cls = LocalString(ctx, params[2]);
            const char* name = LocalString(ctx, params[3]);
            const Field& f = FindField(cls, name);
            const bool vector = f.typeName.rfind("CUtlVector", 0) == 0 || f.typeName.rfind("CNetworkUtlVectorBase", 0) == 0
                || f.typeName.rfind("C_NetworkUtlVectorBase", 0) == 0;
            if (!f.found || !vector)
            {
                ctx->ThrowNativeError("Field %s::%s is not a CUtlVector", cls, name);
                return nullptr;
            }
            if (params[1] == 0)
            {
                ctx->ThrowNativeError("Null schema object");
                return nullptr;
            }
            auto* base = static_cast<uint8_t*>(Base(ctx, params[1]));
            return base ? reinterpret_cast<CUtlVector<uint8_t>*>(base + f.offset) : nullptr;
        }

        cell_t N_GetUtlVectorCount(IPluginContext* ctx, const cell_t* params)
        {
            CUtlVector<uint8_t>* v = UtlVectorOf(ctx, params);
            return v ? v->Count() : 0;
        }

        // GetUtlVectorElement(instance, className, field, index, elementSize): element `index`
        // as a SchemaObject (read it with the *At natives, or view_as a schema class).
        cell_t N_GetUtlVectorElement(IPluginContext* ctx, const cell_t* params)
        {
            CUtlVector<uint8_t>* v = UtlVectorOf(ctx, params);
            if (!v)
                return 0;
            if (params[4] < 0 || params[4] >= v->Count())
                return ctx->ThrowNativeError("Element %d is out of range (%d elements)", params[4], v->Count());
            if (params[5] <= 0)
                return ctx->ThrowNativeError("Element size %d is invalid", params[5]);
            return SchemaObjectForPointer(v->Base() + static_cast<size_t>(params[4]) * params[5]);
        }

        // ---- CEntityInstance -----------------------------------------------------

        cell_t N_EntIsValid(IPluginContext*, const cell_t* params)
        {
            return EntityFromCell(params[1]) != nullptr;
        }

        // The index the handle carries, alive or not; -1 for NULL_ENTITY.
        cell_t N_EntGetEntityIndex(IPluginContext*, const cell_t* params)
        {
            return EntityCellIndex(params[1]);
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
            if (params[1] < 0 || params[1] >= MAX_TOTAL_ENTITIES)
                return -1;
            return EntityCell(CBaseEntity::FromIndex<CEntityInstance>(params[1]));
        }

        cell_t N_BaseCreateEntityByName(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(CBaseEntity::CreateEntityByName(LocalString(ctx, params[1])));
        }

        // AcceptInput(input, activator = NULL_ENTITY, caller = NULL_ENTITY, value = "", chain)
        cell_t N_BaseAcceptInput(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            bool ok1, ok2;
            CEntityInstance* activator = OptionalEntity(ctx, params[3], &ok1);
            CEntityInstance* caller = OptionalEntity(ctx, params[4], &ok2);
            if (ent && ok1 && ok2)
                ent->AcceptInput(LocalString(ctx, params[2]), activator, caller, LocalString(ctx, params[5]), ChainArg(params, 6));
            return 0;
        }

        // AddEntityIOEvent(input, activator, caller, value, delay, chain)
        cell_t N_BaseAddEntityIOEvent(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            bool ok1, ok2;
            CEntityInstance* activator = OptionalEntity(ctx, params[3], &ok1);
            CEntityInstance* caller = OptionalEntity(ctx, params[4], &ok2);
            if (ent && ok1 && ok2)
                ent->AddEntityIOEvent(LocalString(ctx, params[2]), activator, caller, LocalString(ctx, params[5]), sp_ctof(params[6]), ChainArg(params, 7));
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

        // DispatchSpawn(StringMap keyValues = null, chain): the map's entries become the spawn keyvalues.
        cell_t N_BaseDispatchSpawn(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            if (!ent)
                return 0;
            CEntityKeyValues* keyValues = nullptr;
            if (params[0] >= 2 && params[2] != 0)
            {
                std::vector<std::pair<std::string, std::string>> entries;
                if (!StringMapStrings(ctx, params[2], entries))
                    return 0;
                // The entity takes the keyvalues over; it releases them.
                keyValues = new CEntityKeyValues();
                for (const auto& [key, value] : entries)
                    keyValues->SetString(key.c_str(), value.c_str());
            }
            ent->DispatchSpawn(keyValues, ChainArg(params, 3));
            return 0;
        }

        // Teleport(position = NULL_VECTOR, angles = NULL_VECTOR, velocity = NULL_VECTOR, chain)
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
            ent->Teleport(hasPos ? &pos : nullptr, hasAng ? &angles : nullptr, hasVel ? &vel : nullptr, ChainArg(params, 5));
            return 0;
        }

        cell_t N_BaseRemove(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]))
                ent->Remove(ChainArg(params, 2));
            return 0;
        }

        // GetAngRotation / GetAbsRotation / GetAbsVelocity (float[3])
        template <typename V, V (CBaseEntity::*GET)()>
        cell_t N_BaseGetVector(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]))
            {
                const V v = (ent->*GET)();
                WriteVector(ctx, params[2], v.x, v.y, v.z);
            }
            return 0;
        }

        // SetAbsOrigin / SetAbsRotation / SetAngRotation / SetAbsVelocity / SetBaseVelocity (const float[3])
        template <typename V, void (CBaseEntity::*SET)(V)>
        cell_t N_BaseSetVector(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            Vector v;
            if (ent && ReadVector(ctx, params[2], v))
                (ent->*SET)(V(v.x, v.y, v.z));
            return 0;
        }

        cell_t N_BaseGetVData(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            return ent ? SchemaObjectForPointer(ent->GetVData()) : 0;
        }

        cell_t N_BaseSetMoveType(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]))
                ent->SetMoveType(static_cast<MoveType_t>(params[2]));
            return 0;
        }

        cell_t N_BaseGetCollisionGroup(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            return ent ? ent->GetCollisionGroup() : 0;
        }

        cell_t N_BaseSetCollisionGroup(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]))
                ent->SetCollisionGroup(static_cast<uint8>(params[2]));
            return 0;
        }

        cell_t N_BaseCollisionRulesChanged(IPluginContext* ctx, const cell_t* params)
        {
            if (CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]))
                ent->CollisionRulesChanged();
            return 0;
        }

        // DispatchParticleEffect(particle, attachType, attachmentPoint, resetAll, const int recipients[2], chain)
        cell_t N_BaseDispatchParticleEffect(IPluginContext* ctx, const cell_t* params)
        {
            CBaseEntity* ent = As<CBaseEntity>(ctx, params[1]);
            if (!ent)
                return 0;
            cell_t* mask;
            ctx->LocalToPhysAddr(params[6], &mask);
            const uint64_t recipients = static_cast<uint32_t>(mask[0]) | (static_cast<uint64_t>(static_cast<uint32_t>(mask[1])) << 32);
            // Everybody is C++'s default: no filter.
            CRecipientFilter filter;
            filter.SetFromBitmask(recipients);
            ent->DispatchParticleEffect(LocalString(ctx, params[2]), static_cast<ParticleAttachment_t>(params[3]), static_cast<uint8>(params[4]),
                                        params[5] != 0, recipients == ~0ull ? nullptr : &filter, ChainArg(params, 7));
            return 0;
        }

        cell_t N_BaseGetIndex(IPluginContext*, const cell_t* params)
        {
            return EntityCellIndex(params[1]);
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
                ent->SetModel(LocalString(ctx, params[2]), ChainArg(params, 3));
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
                pc->SetPawn(pawn, ChainArg(params, 3));
            return 0;
        }

        cell_t N_PCGetPawn(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[1]);
            return pc ? EntityCell(pc->GetPawn()) : -1;
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
            return EntityCell(CCSPlayerController::FromSlot(params[1]));
        }

        cell_t N_CSPrintToCenterHtml(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]))
                pc->PrintToCenterHtml(LocalString(ctx, params[2]), params[3], params[4] != 0);
            return 0;
        }

        cell_t N_CSFromPawn(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerPawn* pawn = As<CCSPlayerPawn>(ctx, params[1]);
            return pawn ? EntityCell(CCSPlayerController::FromPawn(pawn)) : -1;
        }

        cell_t N_CSFromUserId(IPluginContext*, const cell_t* params)
        {
            return EntityCell(CCSPlayerController::FromUserId(params[1]));
        }

        cell_t N_CSFromSteamId(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(CCSPlayerController::FromSteamId(static_cast<uint64>(std::strtoull(LocalString(ctx, params[1]), nullptr, 10))));
        }

        // TakeDamage(attacker, damage, damageType, chain): C++ defaults to Bypass.
        cell_t N_CSTakeDamage(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]);
            bool ok;
            auto* attacker = static_cast<CCSPlayerController*>(OptionalEntity(ctx, params[2], &ok));
            if (pc && ok)
                pc->TakeDamage(attacker, params[3], static_cast<DamageTypes_t>(params[4]), ChainArg(params, 5, HookChain::Bypass));
            return 0;
        }

        cell_t N_CSRespawn(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]))
                pc->Respawn(ChainArg(params, 2));
            return 0;
        }

        cell_t N_CSSwitchTeam(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]))
                pc->SwitchTeam(params[2], ChainArg(params, 3));
            return 0;
        }

        cell_t N_CSChangeTeam(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]))
                pc->ChangeTeam(params[2], ChainArg(params, 3));
            return 0;
        }

        cell_t N_CSGetPlayerPawn(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]);
            return pc ? EntityCell(pc->GetPlayerPawn()) : -1;
        }

        cell_t N_CSGetObserverPawn(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[1]);
            return pc ? EntityCell(pc->GetObserverPawn()) : -1;
        }

        // ---- pawns ----------------------------------------------------------------

        cell_t N_PawnCommitSuicide(IPluginContext* ctx, const cell_t* params)
        {
            if (CBasePlayerPawn* pawn = As<CBasePlayerPawn>(ctx, params[1]))
                pawn->CommitSuicide(params[2] != 0, params[3] != 0, ChainArg(params, 4));
            return 0;
        }

        cell_t N_PawnSnapViewAngles(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerPawn* pawn = As<CBasePlayerPawn>(ctx, params[1]);
            Vector v;
            if (pawn && ReadVector(ctx, params[2], v))
                pawn->SnapViewAngles(QAngle(v.x, v.y, v.z), ChainArg(params, 3));
            return 0;
        }

        cell_t N_PawnGetController(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerPawn* pawn = As<CBasePlayerPawn>(ctx, params[1]);
            return pawn ? EntityCell(pawn->GetController()) : -1;
        }

        cell_t N_PawnGetDefaultController(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerPawn* pawn = As<CBasePlayerPawn>(ctx, params[1]);
            return pawn ? EntityCell(pawn->GetDefaultController()) : -1;
        }

        cell_t N_CSPawnBaseGetOriginalController(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerPawnBase* pawn = As<CCSPlayerPawnBase>(ctx, params[1]);
            return pawn ? EntityCell(pawn->GetOriginalController()) : -1;
        }

        cell_t N_CSPawnBaseCanMove(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerPawnBase* pawn = As<CCSPlayerPawnBase>(ctx, params[1]);
            return pawn && pawn->CanMove();
        }

        cell_t N_CSPawnPostThink(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerPawn* pawn = As<CCSPlayerPawn>(ctx, params[1]))
                pawn->PostThink(ChainArg(params, 2));
            return 0;
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
                items->DropActivePlayerWeapon(weapon, ChainArg(params, 3));
            return 0;
        }

        cell_t N_ItemsRemoveWeapons(IPluginContext* ctx, const cell_t* params)
        {
            if (CPlayer_ItemServices* items = ObjectOf<CPlayer_ItemServices>(ctx, params[1]))
                items->RemoveWeapons(params[2] != 0, ChainArg(params, 3));
            return 0;
        }

        cell_t N_ItemsGiveNamedItem(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_ItemServices* items = ObjectOf<CPlayer_ItemServices>(ctx, params[1]);
            return items ? EntityCell(items->GiveNamedItem(LocalString(ctx, params[2]), ChainArg(params, 3))) : -1;
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
            ws->DropWeapon(weapon, hasTarget ? &target : nullptr, hasVelocity ? &velocity : nullptr, ChainArg(params, 5));
            return 0;
        }

        cell_t N_WeaponsBumpWeapon(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_WeaponServices* ws = ObjectOf<CPlayer_WeaponServices>(ctx, params[1]);
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[2]);
            return ws && weapon ? ws->BumpWeapon(weapon, ChainArg(params, 3)) : 0;
        }

        cell_t N_WeaponsSelectItem(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_WeaponServices* ws = ObjectOf<CPlayer_WeaponServices>(ctx, params[1]);
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[2]);
            if (ws && weapon)
                ws->SelectItem(weapon, 0, ChainArg(params, 3));
            return 0;
        }

        cell_t N_WeaponsDestroy(IPluginContext* ctx, const cell_t* params)
        {
            CPlayer_WeaponServices* ws = ObjectOf<CPlayer_WeaponServices>(ctx, params[1]);
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[2]);
            if (ws && weapon)
                ws->Destroy(weapon, ChainArg(params, 3));
            return 0;
        }

        cell_t N_WeaponGetWeaponVData(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[1]);
            return weapon ? SchemaObjectForPointer(weapon->GetWeaponVData()) : 0;
        }

        cell_t N_WeaponGetWeaponClassname(IPluginContext* ctx, const cell_t* params)
        {
            CBasePlayerWeapon* weapon = As<CBasePlayerWeapon>(ctx, params[1]);
            const char* name = weapon ? weapon->GetWeaponClassname() : nullptr;
            size_t written = 0;
            ctx->StringToLocalUTF8(params[2], params[3], name ? name : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_WeaponVDataFromKey(IPluginContext* ctx, const cell_t* params)
        {
            return SchemaObjectForPointer(CCSWeaponBaseVData::FromKey(LocalString(ctx, params[1]), params[2]));
        }

        cell_t N_PawnComponentGetPlayerPawn(IPluginContext* ctx, const cell_t* params)
        {
            CPlayerPawnComponent* c = ObjectOf<CPlayerPawnComponent>(ctx, params[1]);
            return c ? EntityCell(c->GetPlayerPawn()) : -1;
        }

        cell_t N_ControllerComponentGetPlayerController(IPluginContext* ctx, const cell_t* params)
        {
            CPlayerControllerComponent* c = ObjectOf<CPlayerControllerComponent>(ctx, params[1]);
            return c ? EntityCell(c->GetPlayerController()) : -1;
        }

        template <void (CAttributeList::*SET)(const char*, float, HookChain)>
        cell_t N_AttributesSet(IPluginContext* ctx, const cell_t* params)
        {
            if (CAttributeList* list = ObjectOf<CAttributeList>(ctx, params[1]))
                (list->*SET)(LocalString(ctx, params[2]), sp_ctof(params[3]), ChainArg(params, 4));
            return 0;
        }

        cell_t N_SceneNodeGetSkeletonInstance(IPluginContext* ctx, const cell_t* params)
        {
            CGameSceneNode* node = ObjectOf<CGameSceneNode>(ctx, params[1]);
            return node ? SchemaObjectForPointer(node->GetSkeletonInstance()) : 0;
        }

        // EmitGrenade(position, angles, velocity, owner, itemDefIndex, chain), static on each projectile class.
        template <typename T>
        cell_t N_EmitGrenade(IPluginContext* ctx, const cell_t* params)
        {
            Vector pos, ang, vel;
            ReadVector(ctx, params[1], pos);
            ReadVector(ctx, params[2], ang);
            ReadVector(ctx, params[3], vel);
            bool ok;
            auto* owner = static_cast<CBaseEntity*>(OptionalEntity(ctx, params[4], &ok));
            if (!ok)
                return -1;
            return EntityCell(T::EmitGrenade(pos, QAngle(ang.x, ang.y, ang.z), vel, owner, static_cast<uint32_t>(params[5]), ChainArg(params, 6)));
        }

        // CSmokeGrenadeProjectile.EmitGrenade(position, angles, velocity, team, owner, itemDefIndex, chain)
        cell_t N_EmitSmokeGrenade(IPluginContext* ctx, const cell_t* params)
        {
            Vector pos, ang, vel;
            ReadVector(ctx, params[1], pos);
            ReadVector(ctx, params[2], ang);
            ReadVector(ctx, params[3], vel);
            bool ok;
            auto* owner = static_cast<CBaseEntity*>(OptionalEntity(ctx, params[5], &ok));
            if (!ok)
                return -1;
            return EntityCell(CSmokeGrenadeProjectile::EmitGrenade(pos, QAngle(ang.x, ang.y, ang.z), vel, params[4], owner,
                                                                static_cast<uint32_t>(params[6]), ChainArg(params, 7)));
        }

        cell_t N_RulesTerminateRound(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSGameRules* rules = ObjectOf<CCSGameRules>(ctx, params[1]))
                rules->TerminateRound(sp_ctof(params[2]), params[3], static_cast<uint32>(params[4]), ChainArg(params, 5, HookChain::Bypass));
            return 0;
        }

        cell_t N_RulesGoToIntermission(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSGameRules* rules = ObjectOf<CCSGameRules>(ctx, params[1]))
                rules->GoToIntermission(params[2] != 0, ChainArg(params, 3));
            return 0;
        }

        cell_t N_RulesFindPickerEntity(IPluginContext* ctx, const cell_t* params)
        {
            CCSGameRules* rules = ObjectOf<CCSGameRules>(ctx, params[1]);
            CBasePlayerController* pc = As<CBasePlayerController>(ctx, params[2]);
            return rules && pc ? EntityCell(rules->FindPickerEntity(pc)) : -1;
        }

        cell_t N_RulesGetClientAimTarget(IPluginContext* ctx, const cell_t* params)
        {
            CCSGameRules* rules = ObjectOf<CCSGameRules>(ctx, params[1]);
            CCSPlayerController* pc = As<CCSPlayerController>(ctx, params[2]);
            return rules && pc ? EntityCell(rules->GetClientAimTarget(pc)) : -1;
        }

        // ---- CCSCustomHudLayout ---------------------------------------------------

        // The optional controller argument: NULL_ENTITY for every player.
        bool HudController(IPluginContext* ctx, cell_t index, CCSPlayerController** out)
        {
            bool ok;
            *out = static_cast<CCSPlayerController*>(OptionalEntity(ctx, index, &ok));
            return ok;
        }

        cell_t N_HudCreate(IPluginContext* ctx, const cell_t* params)
        {
            const char* target = LocalString(ctx, params[2]);
            return EntityCell(CCSCustomHudLayout::Create(LocalString(ctx, params[1]), *target ? target : nullptr));
        }

        cell_t N_HudGetLayoutState(IPluginContext* ctx, const cell_t* params)
        {
            CCSCustomHudLayout* hud = As<CCSCustomHudLayout>(ctx, params[1]);
            CCSPlayerController* pc;
            if (!hud || !HudController(ctx, params[2], &pc))
                return 0;
            return SchemaObjectForPointer(&hud->GetLayoutState(pc));
        }

        cell_t N_HudSetHasClass(IPluginContext* ctx, const cell_t* params)
        {
            CCSCustomHudLayout* hud = As<CCSCustomHudLayout>(ctx, params[1]);
            CCSPlayerController* pc;
            if (hud && HudController(ctx, params[5], &pc))
                hud->SetHasClass(LocalString(ctx, params[2]), LocalString(ctx, params[3]), params[4] != 0, pc);
            return 0;
        }

        cell_t N_HudSetDialogVariableString(IPluginContext* ctx, const cell_t* params)
        {
            CCSCustomHudLayout* hud = As<CCSCustomHudLayout>(ctx, params[1]);
            CCSPlayerController* pc;
            if (hud && HudController(ctx, params[5], &pc))
                hud->SetDialogVariableString(LocalString(ctx, params[2]), LocalString(ctx, params[3]), LocalString(ctx, params[4]), pc);
            return 0;
        }

        cell_t N_HudSetInputCaptureEnabled(IPluginContext* ctx, const cell_t* params)
        {
            CCSCustomHudLayout* hud = As<CCSCustomHudLayout>(ctx, params[1]);
            CCSPlayerController* pc;
            if (hud && HudController(ctx, params[3], &pc))
                hud->SetInputCaptureEnabled(params[2] != 0, pc);
            return 0;
        }

        cell_t N_HudIsInputCaptureEnabled(IPluginContext* ctx, const cell_t* params)
        {
            CCSCustomHudLayout* hud = As<CCSCustomHudLayout>(ctx, params[1]);
            CCSPlayerController* pc;
            return hud && HudController(ctx, params[2], &pc) && hud->IsInputCaptureEnabled(pc);
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
            return pc ? EntityCell(entities::entitiesManager.FindPickerEntity(pc)) : -1;
        }

        cell_t N_EntsFindEntityByClassname(IPluginContext* ctx, const cell_t* params)
        {
            bool ok;
            CEntityInstance* start = OptionalEntity(ctx, params[1], &ok);
            return ok ? EntityCell(entities::entitiesManager.FindEntityByClassname(start, LocalString(ctx, params[2]))) : -1;
        }

        // FindEntityByName(start, name, searching = NULL_ENTITY, activator = NULL_ENTITY, caller = NULL_ENTITY):
        // the last three resolve "!self", "!activator", "!caller", "!player".
        cell_t N_EntsFindEntityByName(IPluginContext* ctx, const cell_t* params)
        {
            bool ok[4] = { true, true, true, true };
            CEntityInstance* args[4] = {};
            for (int i = 0; i < 4; i++)
            {
                const int p = i == 0 ? 1 : i + 2;
                if (p <= params[0])
                    args[i] = OptionalEntity(ctx, params[p], &ok[i]);
            }
            if (!ok[0] || !ok[1] || !ok[2] || !ok[3])
                return -1;
            return EntityCell(entities::entitiesManager.FindEntityByName(args[0], LocalString(ctx, params[2]), args[1], args[2], args[3]));
        }

        cell_t N_EntsCreateEntityByName(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(entities::entitiesManager.CreateEntityByName(LocalString(ctx, params[1])));
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

        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        // A script's entity listener: four optional functions.
        class ScriptEntityListener final : public IEntityListener
        {
        public:
            PluginId owner = 0;
            IPluginFunction* created = nullptr;
            IPluginFunction* spawned = nullptr;
            IPluginFunction* deleted = nullptr;
            IPluginFunction* parentChanged = nullptr;
            CEntityHandle only;     // for one entity; invalid for every one

            void OnEntityCreated(CEntityInstance* e) override { Fire(created, e); }
            void OnEntitySpawned(CEntityInstance* e) override { Fire(spawned, e); }
            void OnEntityDeleted(CEntityInstance* e) override { Fire(deleted, e); }
            void OnEntityParentChanged(CEntityInstance* e, CEntityInstance* parent) override
            {
                if (!parentChanged)
                    return;
                sp::CallArgs args;
                args.PushCell(EntityCell(e));
                args.PushCell(EntityCell(parent));
                Invoke(parentChanged, args);
            }

        private:
            static void Fire(IPluginFunction* fn, CEntityInstance* e)
            {
                if (!fn)
                    return;
                sp::CallArgs args;
                args.PushCell(EntityCell(e));
                Invoke(fn, args);
            }
        };

        class ScriptOutputListener final : public IEntityOutputListener
        {
        public:
            PluginId owner = 0;
            IPluginFunction* fn = nullptr;
            CEntityHandle only;

            Action OnEntityOutput(CEntityInstance* caller, const char* output, CEntityInstance* activator, float delay, bool post) override
            {
                sp::CallArgs args;
                args.PushCell(EntityCell(caller));
                args.PushString(output ? output : "");
                args.PushCell(EntityCell(activator));
                args.PushFloat(delay);
                args.PushCell(post ? 1 : 0);
                cell_t res = 0;
                Invoke(fn, args, &res);
                return static_cast<Action>(std::clamp<cell_t>(res, 0, 2));
            }
        };

        cell_t g_nextListenerId = 1;
        std::map<cell_t, std::unique_ptr<ScriptEntityListener>> g_entityListeners;
        std::map<cell_t, std::unique_ptr<ScriptOutputListener>> g_outputListeners;

        IPluginFunction* OptionalFunction(IPluginContext* ctx, cell_t id)
        {
            return id == 0 || id == -1 ? nullptr : ctx->GetFunctionById(static_cast<funcid_t>(id));
        }

        // A listener for one entity is dropped by the core with the entity;
        // the script's object goes on the next add, once the handle no longer
        // resolves. Never during a dispatch of its own: the core skips a
        // dropped entry without touching the listener.
        template <typename MAP>
        void SweepGone(MAP& map)
        {
            std::erase_if(map, [](const auto& e) { return e.second->only.IsValid() && !e.second->only.Get(); });
        }

        // AddEntityListener(created, spawned, deleted, parentChanged, designerName, entity)
        cell_t N_EntsAddEntityListener(IPluginContext* ctx, const cell_t* params)
        {
            bool ok;
            CEntityInstance* entity = OptionalEntity(ctx, params[6], &ok);
            if (!ok)
                return 0;
            SweepGone(g_entityListeners);

            auto listener = std::make_unique<ScriptEntityListener>();
            listener->owner = CallerId(ctx);
            listener->created = OptionalFunction(ctx, params[1]);
            listener->spawned = OptionalFunction(ctx, params[2]);
            listener->deleted = OptionalFunction(ctx, params[3]);
            listener->parentChanged = OptionalFunction(ctx, params[4]);
            if (entity)
                listener->only = entity->GetRefEHandle();
            const char* designer = LocalString(ctx, params[5]);
            entities::entitiesManager.AddEntityListener(listener->owner, listener.get(), *designer ? designer : nullptr, entity);
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

        // AddEntityOutputListener(callback, designerName, output, entity)
        cell_t N_EntsAddEntityOutputListener(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[1]));
            if (!fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[1]);
            bool ok;
            CEntityInstance* entity = OptionalEntity(ctx, params[4], &ok);
            if (!ok)
                return 0;
            SweepGone(g_outputListeners);

            auto listener = std::make_unique<ScriptOutputListener>();
            listener->owner = CallerId(ctx);
            listener->fn = fn;
            if (entity)
                listener->only = entity->GetRefEHandle();
            const char* designer = LocalString(ctx, params[2]);
            const char* output = LocalString(ctx, params[3]);
            entities::entitiesManager.AddEntityOutputListener(listener->owner, listener.get(), *designer ? designer : nullptr,
                                                              *output ? output : nullptr, entity);
            const cell_t id = g_nextListenerId++;
            g_outputListeners[id] = std::move(listener);
            return id;
        }

        cell_t N_EntsRemoveEntityOutputListener(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_outputListeners.find(params[1]);
            if (it == g_outputListeners.end() || it->second->owner != CallerId(ctx))
                return 0;
            entities::entitiesManager.RemoveEntityOutputListener(it->second.get());
            g_outputListeners.erase(it);
            return 1;
        }

        // FindAllEntitiesByDesignerName(name) -> ArrayList of entities (UTIL_FindAllEntitiesByDesignerName).
        cell_t N_EntsFindAllEntitiesByDesignerName(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<cell_t> found;
            for (CEntityInstance* e : UTIL_FindAllEntitiesByDesignerName<CEntityInstance>(LocalString(ctx, params[1])))
                found.push_back(EntityCell(e));
            return NewCellList(found, CallerId(ctx));
        }

        const sp_nativeinfo_t g_entityNatives[] = {
            // source2mod/schema.inc
            { "Source2ModSchema.GetInt", N_GetInt },
            { "Source2ModSchema.SetInt", N_SetInt },
            { "Source2ModSchema.GetInt64", N_GetInt64 },
            { "Source2ModSchema.SetInt64", N_SetInt64 },
            { "Source2ModSchema.GetArraySize", N_GetArraySize },
            { "Source2ModSchema.GetArrayInt", N_GetArrayInt },
            { "Source2ModSchema.SetArrayInt", N_SetArrayInt },
            { "Source2ModSchema.GetArrayInt64", N_GetArrayInt64 },
            { "Source2ModSchema.SetArrayInt64", N_SetArrayInt64 },
            { "Source2ModSchema.GetArrayFloat", N_GetArrayFloat },
            { "Source2ModSchema.SetArrayFloat", N_SetArrayFloat },
            { "Source2ModSchema.GetArrayVector", N_GetArrayVector },
            { "Source2ModSchema.SetArrayVector", N_SetArrayVector },
            { "Source2ModSchema.GetArrayHandle", N_GetArrayHandle },
            { "Source2ModSchema.SetArrayHandle", N_SetArrayHandle },
            { "Source2ModSchema.GetArrayString", N_GetArrayString },
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
            { "Source2ModSchema.StateChanged", N_StateChanged },
            { "Source2ModSchema.GetIntAt", N_GetIntAt },
            { "Source2ModSchema.SetIntAt", N_SetIntAt },
            { "Source2ModSchema.GetInt64At", N_GetInt64At },
            { "Source2ModSchema.SetInt64At", N_SetInt64At },
            { "Source2ModSchema.GetFloatAt", N_GetFloatAt },
            { "Source2ModSchema.SetFloatAt", N_SetFloatAt },
            { "Source2ModSchema.GetVectorAt", N_GetVectorAt },
            { "Source2ModSchema.SetVectorAt", N_SetVectorAt },
            { "Source2ModSchema.GetEntityAt", N_GetEntityAt },
            { "Source2ModSchema.SetEntityAt", N_SetEntityAt },
            { "Source2ModSchema.GetObjectAt", N_GetObjectAt },
            { "Source2ModSchema.GetEmbeddedAt", N_GetEmbeddedAt },
            { "Source2ModSchema.GetStringPointerAt", N_GetStringPointerAt },
            { "Source2ModSchema.GetUtlVectorCount", N_GetUtlVectorCount },
            { "Source2ModSchema.GetUtlVectorElement", N_GetUtlVectorElement },
            { "CEntityInstance.IsValid", N_EntIsValid },
            { "CEntityInstance.GetClassname", N_EntGetClassname },
            { "CEntityInstance.GetName", N_EntGetName },
            { "CEntityInstance.GetEntityIndex", N_EntGetEntityIndex },
            // schema/classes.inc, from tools/sp_schemagen/methods.inc
            { "CBaseEntity.FromIndex", N_BaseFromIndex },
            { "CBaseEntity.CreateEntityByName", N_BaseCreateEntityByName },
            { "CBaseEntity.AcceptInput", N_BaseAcceptInput },
            { "CBaseEntity.GetAbsOrigin", N_BaseGetAbsOrigin },
            { "CBaseEntity.DispatchSpawn", N_BaseDispatchSpawn },
            { "CBaseEntity.Teleport", N_BaseTeleport },
            { "CBaseEntity.Remove", N_BaseRemove },
            { "CBaseEntity.GetIndex", N_BaseGetIndex },
            { "CBaseEntity.AddEntityIOEvent", N_BaseAddEntityIOEvent },
            { "CBaseEntity.GetAngRotation", N_BaseGetVector<QAngle, &CBaseEntity::GetAngRotation> },
            { "CBaseEntity.GetAbsRotation", N_BaseGetVector<QAngle, &CBaseEntity::GetAbsRotation> },
            { "CBaseEntity.GetAbsVelocity", N_BaseGetVector<Vector, &CBaseEntity::GetAbsVelocity> },
            { "CBaseEntity.SetAbsOrigin", N_BaseSetVector<Vector, &CBaseEntity::SetAbsOrigin> },
            { "CBaseEntity.SetAbsRotation", N_BaseSetVector<QAngle, &CBaseEntity::SetAbsRotation> },
            { "CBaseEntity.SetAngRotation", N_BaseSetVector<QAngle, &CBaseEntity::SetAngRotation> },
            { "CBaseEntity.SetAbsVelocity", N_BaseSetVector<Vector, &CBaseEntity::SetAbsVelocity> },
            { "CBaseEntity.SetBaseVelocity", N_BaseSetVector<Vector, &CBaseEntity::SetBaseVelocity> },
            { "CBaseEntity.GetVData", N_BaseGetVData },
            { "CBaseEntity.SetMoveType", N_BaseSetMoveType },
            { "CBaseEntity.GetCollisionGroup", N_BaseGetCollisionGroup },
            { "CBaseEntity.SetCollisionGroup", N_BaseSetCollisionGroup },
            { "CBaseEntity.CollisionRulesChanged", N_BaseCollisionRulesChanged },
            { "CBaseEntity.DispatchParticleEffect", N_BaseDispatchParticleEffect },
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
            { "CCSPlayerController.FromPawn", N_CSFromPawn },
            { "CCSPlayerController.FromUserId", N_CSFromUserId },
            { "CCSPlayerController.FromSteamId", N_CSFromSteamId },
            { "CCSPlayerController.TakeDamage", N_CSTakeDamage },
            { "CCSPlayerController.PrintToCenterHtml", N_CSPrintToCenterHtml },
            { "CCSPlayerController.Respawn", N_CSRespawn },
            { "CCSPlayerController.SwitchTeam", N_CSSwitchTeam },
            { "CCSPlayerController.ChangeTeam", N_CSChangeTeam },
            { "CCSPlayerController.GetPlayerPawn", N_CSGetPlayerPawn },
            { "CCSPlayerController.GetObserverPawn", N_CSGetObserverPawn },
            { "CBasePlayerPawn.CommitSuicide", N_PawnCommitSuicide },
            { "CBasePlayerPawn.SnapViewAngles", N_PawnSnapViewAngles },
            { "CBasePlayerPawn.GetController", N_PawnGetController },
            { "CBasePlayerPawn.GetDefaultController", N_PawnGetDefaultController },
            { "CCSPlayerPawnBase.GetOriginalController", N_CSPawnBaseGetOriginalController },
            { "CCSPlayerPawnBase.CanMove", N_CSPawnBaseCanMove },
            { "CCSPlayerPawn.GetEyeAngles", N_CSPawnGetEyeAngles },
            { "CCSPlayerPawn.PostThink", N_CSPawnPostThink },
            { "CBasePlayerWeapon.GetWeaponVData", N_WeaponGetWeaponVData },
            { "CBasePlayerWeapon.GetWeaponClassname", N_WeaponGetWeaponClassname },
            { "CCSWeaponBaseVData.FromKey", N_WeaponVDataFromKey },
            { "CPlayerPawnComponent.GetPlayerPawn", N_PawnComponentGetPlayerPawn },
            { "CPlayerControllerComponent.GetPlayerController", N_ControllerComponentGetPlayerController },
            { "CPlayer_WeaponServices.Destroy", N_WeaponsDestroy },
            { "CAttributeList.SetOrAddAttribute", N_AttributesSet<&CAttributeList::SetOrAddAttribute> },
            { "CAttributeList.SetOrAddAttributeValueByName", N_AttributesSet<&CAttributeList::SetOrAddAttributeValueByName> },
            { "CGameSceneNode.GetSkeletonInstance", N_SceneNodeGetSkeletonInstance },
            { "CDecoyProjectile.EmitGrenade", N_EmitGrenade<CDecoyProjectile> },
            { "CFlashbangProjectile.EmitGrenade", N_EmitGrenade<CFlashbangProjectile> },
            { "CHEGrenadeProjectile.EmitGrenade", N_EmitGrenade<CHEGrenadeProjectile> },
            { "CMolotovProjectile.EmitGrenade", N_EmitGrenade<CMolotovProjectile> },
            { "CSmokeGrenadeProjectile.EmitGrenade", N_EmitSmokeGrenade },
            { "CCSGameRules.GoToIntermission", N_RulesGoToIntermission },
            { "CCSGameRules.FindPickerEntity", N_RulesFindPickerEntity },
            { "CCSGameRules.GetClientAimTarget", N_RulesGetClientAimTarget },
            { "CCSCustomHudLayout.Create", N_HudCreate },
            { "CCSCustomHudLayout.GetLayoutState", N_HudGetLayoutState },
            { "CCSCustomHudLayout.SetHasClass", N_HudSetHasClass },
            { "CCSCustomHudLayout.SetDialogVariableString", N_HudSetDialogVariableString },
            { "CCSCustomHudLayout.SetInputCaptureEnabled", N_HudSetInputCaptureEnabled },
            { "CCSCustomHudLayout.IsInputCaptureEnabled", N_HudIsInputCaptureEnabled },
            { "Source2ModEntities.FindAllEntitiesByDesignerName", N_EntsFindAllEntitiesByDesignerName },
            { "Source2ModEntities.AddEntityListener", N_EntsAddEntityListener },
            { "Source2ModEntities.RemoveEntityListener", N_EntsRemoveEntityListener },
            { "Source2ModEntities.AddEntityOutputListener", N_EntsAddEntityOutputListener },
            { "Source2ModEntities.RemoveEntityOutputListener", N_EntsRemoveEntityOutputListener },
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
            { "Source2ModEntities.AcceptInput", N_EntsAcceptInput },
            { "Source2ModEntities.AddEntityIOEvent", N_EntsAddEntityIOEvent },
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

    // An entity cell: the top bit (so it is never a plain index nor a Handle,
    // which stay positive), 16 bits of the serial, 15 of the index. -1 is
    // CEntityHandle's own invalid value and NULL_ENTITY.
    namespace
    {
        constexpr uint32_t kEntityFlag = 1u << 31;
        constexpr uint32_t kCellIndexMask = 0x7FFF;
        constexpr uint32_t kCellSerialMask = 0xFFFF;
    }

    cell_t EntityCell(CEntityHandle handle)
    {
        if (!handle.IsValid())
            return -1;
        return static_cast<cell_t>(kEntityFlag | ((static_cast<uint32_t>(handle.GetSerialNumber()) & kCellSerialMask) << 15)
                                   | (static_cast<uint32_t>(handle.GetEntryIndex()) & kCellIndexMask));
    }

    cell_t EntityCell(CEntityInstance* inst)
    {
        return inst ? EntityCell(inst->GetRefEHandle()) : -1;
    }

    bool IsEntityCell(cell_t value)
    {
        return value != -1 && (static_cast<uint32_t>(value) & kEntityFlag) != 0;
    }

    cell_t EntityCellIndex(cell_t value)
    {
        return IsEntityCell(value) ? static_cast<cell_t>(static_cast<uint32_t>(value) & kCellIndexMask) : -1;
    }

    CEntityInstance* EntityFromCell(cell_t value)
    {
        if (!IsEntityCell(value))
            return nullptr;
        const uint32_t raw = static_cast<uint32_t>(value);
        CEntityInstance* inst = CBaseEntity::FromIndex<CEntityInstance>(static_cast<int>(raw & kCellIndexMask));
        if (!inst || (static_cast<uint32_t>(inst->GetRefEHandle().GetSerialNumber()) & kCellSerialMask) != ((raw >> 15) & kCellSerialMask))
            return nullptr;
        return inst;
    }

    CEntityInstance* EntityArg(IPluginContext* ctx, cell_t value)
    {
        if (value == -1)
        {
            ctx->ThrowNativeError("Null entity");
            return nullptr;
        }
        if (!IsEntityCell(value))
        {
            ctx->ThrowNativeError("%d is not an entity (entities are handles: CBaseEntity.FromIndex makes one from an index)", value);
            return nullptr;
        }
        CEntityInstance* inst = EntityFromCell(value);
        if (!inst)
            ctx->ThrowNativeError("Entity %d (handle %x) no longer exists", EntityCellIndex(value), value);
        return inst;
    }

    CEntityInstance* OptionalEntityArg(IPluginContext* ctx, cell_t value, bool* ok)
    {
        *ok = true;
        if (value == -1)
            return nullptr;
        CEntityInstance* inst = EntityArg(ctx, value);
        *ok = inst != nullptr;
        return inst;
    }

    CEntityHandle HandleFromCell(cell_t value)
    {
        // A cell keeps 16 bits of the serial: the live entity gives the whole handle.
        CEntityInstance* inst = EntityFromCell(value);
        return inst ? inst->GetRefEHandle() : CEntityHandle();
    }

    void ForgetEntityListeners(PluginId id)
    {
        // Off the manager first, whichever way the script went.
        std::erase_if(g_entityListeners, [id](const auto& e)
        {
            if (e.second->owner != id)
                return false;
            entities::entitiesManager.RemoveEntityListener(e.second.get());
            return true;
        });
        std::erase_if(g_outputListeners, [id](const auto& e)
        {
            if (e.second->owner != id)
                return false;
            entities::entitiesManager.RemoveEntityOutputListener(e.second.get());
            return true;
        });
    }

    const sp_nativeinfo_t* GetEntityNatives()
    {
        return g_entityNatives;
    }
}
