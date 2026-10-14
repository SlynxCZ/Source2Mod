/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Handles: the opaque numbers a script holds for anything that lives on the
 * host side -- timers, ADT containers, ConVars, forwards, plugins. SourceMod's
 * HandleSys (AlliedModders LLC) is the model: typed, owned by a plugin, freed
 * with it, cloneable to another owner (a clone shares the object, which goes
 * when its last handle does), and a stale number is caught by a serial.
 *
 * Project: Source2Mod
 */
#pragma once

#include <source2mod/ISource2ModTypes.h>

#include <sp_vm_api.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sourcepawn
{
    using HandleType = uint32_t;

    /// SourceMod's HandleError, by number: the "(error %d)" a script sees.
    enum class HandleError : int
    {
        None = 0,
        Changed,
        Type,
        Freed,
        Index,
        Access,
        Limit,
        Identity,
        Owner,
        Version,
        Parameter,
        NoInherit,
    };

    constexpr cell_t kInvalidHandle = 0;

    struct HandleTypeInfo
    {
        std::string name;
        HandleType parent = 0;
        /// Takes the object when its last handle goes; may be empty.
        std::function<void(void*)> destroy;
        /// Whether a script may CloseHandle() it (plugins, events and ConVars may not).
        bool closable = true;
        /// The plugin whose code the destructor is; its handles go when it unloads.
        PluginId owner = 0;
        bool alive = false;
    };

    class HandleSystem
    {
    public:
        HandleType CreateType(const char* name, std::function<void(void*)> destroy, HandleType parent = 0,
                              bool closable = true, PluginId owner = 0);
        HandleType FindType(const char* name) const;
        const HandleTypeInfo* TypeInfo(HandleType type) const;

        /// A new handle to a new object; 0 when the table is full.
        cell_t Create(HandleType type, void* object, PluginId owner);

        /// The object behind a handle of this type (or one derived from it;
        /// 0 takes any type), nullptr and the reason otherwise.
        void* Read(cell_t handle, HandleType type, HandleError* error = nullptr) const;

        HandleType TypeOf(cell_t handle) const;
        bool OwnerOf(cell_t handle, PluginId* owner) const;

        /// Drops one handle; the object goes with its last one. `by` must own
        /// it unless `force`, and the type must be closable unless `force`.
        HandleError Free(cell_t handle, PluginId by, bool force = false);

        /// A second handle to the same object, owned by `newOwner`.
        cell_t Clone(cell_t handle, PluginId newOwner, HandleError* error = nullptr);

        /// Everything the plugin owns, newest first, then whatever types its
        /// code destroys (with every handle of them, whoever owns it).
        void RemoveAllForPlugin(PluginId owner);

        size_t CountOwned(PluginId owner) const;

    private:
        struct Object
        {
            HandleType type;
            void* ptr;
            int refs;
        };

        struct Entry
        {
            uint32_t serial = 0;
            Object* object = nullptr;
            PluginId owner = 0;
            uint64_t order = 0;
        };

        Entry* Lookup(cell_t handle, HandleError* error) const;
        cell_t Encode(uint32_t index) const;
        bool TypeMatches(HandleType given, HandleType wanted) const;

        std::vector<HandleTypeInfo> m_types{ HandleTypeInfo{} };   // 0 = no type
        mutable std::vector<Entry> m_entries{ Entry{} };            // 0 = invalid handle
        std::vector<uint32_t> m_free;
        uint64_t m_order = 0;
    };

    /// The one table; never destroyed, so a timer callback dropped at shutdown
    /// can still find it.
    HandleSystem& Handles();

    /// Reads a handle for a native, raising SourceMod's error in the script on failure.
    void* ReadHandle(SourcePawn::IPluginContext* ctx, cell_t handle, HandleType type, HandleError* error = nullptr);

    /// Reports a handle error the way SourceMod words it.
    cell_t ReportHandleError(SourcePawn::IPluginContext* ctx, cell_t handle, HandleError error);

    /// What the core's own handle types hold: deleted with the last handle.
    struct HandleObject
    {
        virtual ~HandleObject() = default;
    };

    /// The handle types the core's natives use; registered on first use.
    /// Plugin, event and ConVar handles point at things the core keeps
    /// anyway and are not closable; the rest hold a HandleObject.
    struct CoreHandleTypes
    {
        HandleType plugin;
        HandleType timer;
        HandleType event;
        HandleType arrayList;
        HandleType arrayStack;
        HandleType stringMap;
        HandleType snapshot;
        HandleType dataPack;
        HandleType conVar;
        HandleType forward;
        HandleType schemaObject;
    };
    const CoreHandleTypes& Types();
}
