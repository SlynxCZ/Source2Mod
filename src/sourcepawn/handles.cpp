/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * The handle table. Numbers are SourceMod's shape: the slot in the low 16
 * bits, a serial above it, so a handle that was freed and whose slot was
 * reused reads as "changed" instead of reaching the new object.
 *
 * Project: Source2Mod
 */
#include "handles.h"

#include <algorithm>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        constexpr uint32_t kIndexBits = 16;
        constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1;
        constexpr uint32_t kSerialMask = 0x7FFF;    // keeps the handle positive
    }

    HandleSystem& Handles()
    {
        static HandleSystem* s_handles = new HandleSystem();
        return *s_handles;
    }

    HandleType HandleSystem::CreateType(const char* name, std::function<void(void*)> destroy, HandleType parent,
                                        bool closable, PluginId owner)
    {
        HandleTypeInfo info;
        info.name = name ? name : "";
        info.parent = parent;
        info.destroy = std::move(destroy);
        info.closable = closable;
        info.owner = owner;
        info.alive = true;
        m_types.push_back(std::move(info));
        return static_cast<HandleType>(m_types.size() - 1);
    }

    HandleType HandleSystem::FindType(const char* name) const
    {
        for (size_t i = 1; i < m_types.size(); i++)
            if (m_types[i].alive && m_types[i].name == name)
                return static_cast<HandleType>(i);
        return 0;
    }

    const HandleTypeInfo* HandleSystem::TypeInfo(HandleType type) const
    {
        if (type == 0 || type >= m_types.size() || !m_types[type].alive)
            return nullptr;
        return &m_types[type];
    }

    cell_t HandleSystem::Encode(uint32_t index) const
    {
        return static_cast<cell_t>(((m_entries[index].serial & kSerialMask) << kIndexBits) | index);
    }

    cell_t HandleSystem::Create(HandleType type, void* object, PluginId owner)
    {
        if (!TypeInfo(type))
            return kInvalidHandle;

        uint32_t index;
        if (!m_free.empty())
        {
            index = m_free.back();
            m_free.pop_back();
        }
        else
        {
            if (m_entries.size() > kIndexMask)
                return kInvalidHandle;
            m_entries.emplace_back();
            index = static_cast<uint32_t>(m_entries.size() - 1);
        }

        Entry& e = m_entries[index];
        e.serial = (e.serial + 1) & kSerialMask;
        if (e.serial == 0)
            e.serial = 1;
        e.object = new Object{ type, object, 1 };
        e.owner = owner;
        e.order = ++m_order;
        return Encode(index);
    }

    HandleSystem::Entry* HandleSystem::Lookup(cell_t handle, HandleError* error) const
    {
        auto fail = [error](HandleError e) -> Entry*
        {
            if (error)
                *error = e;
            return nullptr;
        };

        if (handle <= 0)
            return fail(HandleError::Index);

        const uint32_t index = static_cast<uint32_t>(handle) & kIndexMask;
        const uint32_t serial = (static_cast<uint32_t>(handle) >> kIndexBits) & kSerialMask;
        if (index == 0 || index >= m_entries.size())
            return fail(HandleError::Index);

        Entry& e = m_entries[index];
        if (!e.object)
            return fail(HandleError::Freed);
        if (e.serial != serial)
            return fail(HandleError::Changed);

        if (error)
            *error = HandleError::None;
        return &e;
    }

    bool HandleSystem::TypeMatches(HandleType given, HandleType wanted) const
    {
        if (wanted == 0)
            return true;
        for (HandleType t = given; t != 0 && t < m_types.size(); t = m_types[t].parent)
            if (t == wanted)
                return true;
        return false;
    }

    void* HandleSystem::Read(cell_t handle, HandleType type, HandleError* error) const
    {
        Entry* e = Lookup(handle, error);
        if (!e)
            return nullptr;
        if (!TypeMatches(e->object->type, type))
        {
            if (error)
                *error = HandleError::Type;
            return nullptr;
        }
        return e->object->ptr;
    }

    HandleType HandleSystem::TypeOf(cell_t handle) const
    {
        Entry* e = Lookup(handle, nullptr);
        return e ? e->object->type : 0;
    }

    bool HandleSystem::OwnerOf(cell_t handle, PluginId* owner) const
    {
        Entry* e = Lookup(handle, nullptr);
        if (!e)
            return false;
        *owner = e->owner;
        return true;
    }

    HandleError HandleSystem::Free(cell_t handle, PluginId by, bool force)
    {
        HandleError error;
        Entry* e = Lookup(handle, &error);
        if (!e)
            return error;

        if (!force)
        {
            const HandleTypeInfo& info = m_types[e->object->type];
            if (!info.closable || e->owner != by)
                return HandleError::Access;
        }

        // The slot is free before the destructor runs: it may free other
        // handles (a timer's data pack) or this one again (a timer that is
        // told to stop while it closes), and must find this one gone.
        Object* object = e->object;
        const uint32_t index = static_cast<uint32_t>(handle) & kIndexMask;
        e->object = nullptr;
        e->owner = 0;
        m_free.push_back(index);

        if (--object->refs == 0)
        {
            // A copy: the destructor may register a type and move m_types.
            const auto destroy = m_types[object->type].destroy;
            if (destroy && object->ptr)
                destroy(object->ptr);
            delete object;
        }
        return HandleError::None;
    }

    cell_t HandleSystem::Clone(cell_t handle, PluginId newOwner, HandleError* error)
    {
        Entry* e = Lookup(handle, error);
        if (!e)
            return kInvalidHandle;

        if (!m_types[e->object->type].closable)
        {
            if (error)
                *error = HandleError::Access;
            return kInvalidHandle;
        }

        Object* object = e->object;
        const cell_t clone = Create(object->type, nullptr, newOwner);
        if (clone == kInvalidHandle)
        {
            if (error)
                *error = HandleError::Limit;
            return kInvalidHandle;
        }

        // Create() made a fresh object; point the slot at the shared one.
        Entry& c = m_entries[static_cast<uint32_t>(clone) & kIndexMask];
        delete c.object;
        c.object = object;
        object->refs++;
        return clone;
    }

    void HandleSystem::RemoveAllForPlugin(PluginId owner)
    {
        // Types whose destructor lives in this plugin's code take every handle
        // of theirs along, whoever holds them.
        std::vector<HandleType> dying;
        for (size_t t = 1; t < m_types.size(); t++)
            if (m_types[t].alive && owner != 0 && m_types[t].owner == owner)
                dying.push_back(static_cast<HandleType>(t));

        // Newest first, as SourceMod does: what was made later may hold what
        // was made earlier (a timer with a data pack), not the other way round.
        std::vector<std::pair<uint64_t, cell_t>> doomed;
        for (uint32_t i = 1; i < m_entries.size(); i++)
        {
            const Entry& e = m_entries[i];
            if (!e.object)
                continue;
            const bool ofDyingType = std::find(dying.begin(), dying.end(), e.object->type) != dying.end();
            if (e.owner == owner || ofDyingType)
                doomed.emplace_back(e.order, Encode(i));
        }
        std::sort(doomed.begin(), doomed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

        for (const auto& [order, handle] : doomed)
            Free(handle, owner, true);

        for (HandleType t : dying)
        {
            m_types[t].alive = false;
            m_types[t].destroy = nullptr;
        }
    }

    size_t HandleSystem::CountOwned(PluginId owner) const
    {
        size_t n = 0;
        for (size_t i = 1; i < m_entries.size(); i++)
            if (m_entries[i].object && m_entries[i].owner == owner)
                n++;
        return n;
    }

    cell_t ReportHandleError(IPluginContext* ctx, cell_t handle, HandleError error)
    {
        if (error == HandleError::Access)
            return ctx->ThrowNativeError("Handle %x cannot be closed or cloned by this plugin (error %d)", handle, static_cast<int>(error));
        return ctx->ThrowNativeError("Invalid Handle %x (error %d)", handle, static_cast<int>(error));
    }

    void* ReadHandle(IPluginContext* ctx, cell_t handle, HandleType type, HandleError* error)
    {
        HandleError err;
        void* object = Handles().Read(handle, type, &err);
        if (error)
            *error = err;
        if (!object)
            ReportHandleError(ctx, handle, err == HandleError::None ? HandleError::Parameter : err);
        return object;
    }

    const CoreHandleTypes& Types()
    {
        static CoreHandleTypes s_types = []
        {
            HandleSystem& hs = Handles();
            auto owned = [](void* p) { delete static_cast<HandleObject*>(p); };

            CoreHandleTypes t{};
            t.plugin = hs.CreateType("Plugin", nullptr, 0, false);
            t.timer = hs.CreateType("Timer", owned);
            t.event = hs.CreateType("Event", nullptr, 0, false);
            t.arrayList = hs.CreateType("ArrayList", owned);
            t.arrayStack = hs.CreateType("ArrayStack", owned);
            t.stringMap = hs.CreateType("StringMap", owned);
            t.snapshot = hs.CreateType("StringMapSnapshot", owned);
            t.dataPack = hs.CreateType("DataPack", owned);
            t.conVar = hs.CreateType("ConVar", nullptr, 0, false);
            t.forward = hs.CreateType("Forward", owned);
            // Kept by the core and handed out again for the same object.
            t.schemaObject = hs.CreateType("SchemaObject", owned, 0, false);
            return t;
        }();
        return s_types;
    }
}
