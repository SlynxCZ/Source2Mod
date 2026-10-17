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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl gives
 * you permission to link the code of this program (as well as its derivative
 * works) to "Counter-Strike 2," "Source 2," "Steam," and any Game MODs or
 * server software running on software by Valve Corporation. You must obey the
 * GNU General Public License in all respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */

#include "hooks.h"

#include "dynamichooks.h"
#include "gamehooks.h"
#include "hookid.h"
#include "pluginmanager.h"
#include "virtualpoints.h"
#include "utils/log.h"

#include <entity2/entityinstance.h>

#include <cstring>
#include <unordered_map>

namespace hooks
{
    HooksManager hooksManager;

    namespace
    {
        std::string s_hookDebug;

        // ---- RTTI: the class names an object is, its own and its bases' -----

#if defined(_WIN32)
        struct RttiTypeDescriptor
        {
            const void* vftable;
            void* spare;
            char name[1];
        };

        struct RttiBaseClassDescriptor
        {
            uint32_t typeDescriptor;
            uint32_t numContainedBases;
            int32_t mdisp, pdisp, vdisp;
            uint32_t attributes;
            uint32_t classDescriptor;
        };

        struct RttiClassHierarchyDescriptor
        {
            uint32_t signature;
            uint32_t attributes;
            uint32_t numBaseClasses;
            uint32_t baseClassArray;
        };

        struct RttiCompleteObjectLocator
        {
            uint32_t signature;
            uint32_t offset;
            uint32_t cdOffset;
            uint32_t typeDescriptor;
            uint32_t classDescriptor;
            uint32_t self;
        };

        // ".?AVCCSPlayerPawn@@" -> "CCSPlayerPawn"
        std::string Plain(const char* decorated)
        {
            std::string s = decorated;
            if (s.rfind(".?AV", 0) == 0 || s.rfind(".?AU", 0) == 0)
                s = s.substr(4);
            const size_t at = s.find('@');
            return at == std::string::npos ? s : s.substr(0, at);
        }

        std::vector<std::string> ClassNames(const void* vtable)
        {
            std::vector<std::string> names;
            const auto* locator = static_cast<const RttiCompleteObjectLocator* const*>(vtable)[-1];
            if (!locator || locator->signature != 1)
                return names;
            const auto base = reinterpret_cast<uintptr_t>(locator) - locator->self;
            const auto* hierarchy = reinterpret_cast<const RttiClassHierarchyDescriptor*>(base + locator->classDescriptor);
            const auto* array = reinterpret_cast<const uint32_t*>(base + hierarchy->baseClassArray);
            for (uint32_t i = 0; i < hierarchy->numBaseClasses; i++)
            {
                const auto* bcd = reinterpret_cast<const RttiBaseClassDescriptor*>(base + array[i]);
                names.push_back(Plain(reinterpret_cast<const RttiTypeDescriptor*>(base + bcd->typeDescriptor)->name));
            }
            return names;
        }
#else
        struct ItaniumTypeInfo
        {
            const void* vtable;
            const char* name;
        };

        struct ItaniumSiTypeInfo : ItaniumTypeInfo
        {
            const ItaniumTypeInfo* base;
        };

        struct ItaniumBaseInfo
        {
            const ItaniumTypeInfo* type;
            long offsetFlags;
        };

        struct ItaniumVmiTypeInfo : ItaniumTypeInfo
        {
            uint32_t flags;
            uint32_t baseCount;
            ItaniumBaseInfo bases[1];
        };

        // "13CCSPlayerPawn" -> "CCSPlayerPawn"; a nested name stays as it is.
        std::string Plain(const char* mangled)
        {
            const char* p = mangled;
            if (*p == '*')
                p++;
            size_t len = 0;
            while (*p >= '0' && *p <= '9')
                len = len * 10 + static_cast<size_t>(*p++ - '0');
            return len ? std::string(p, len) : std::string(mangled);
        }

        // Which record a type_info is, from the name of its own type_info's type.
        int KindOf(const ItaniumTypeInfo* type)
        {
            const auto* meta = static_cast<const ItaniumTypeInfo* const*>(type->vtable)[-1];
            const char* name = meta ? meta->name : "";
            if (std::strcmp(name, "N10__cxxabiv120__si_class_type_infoE") == 0)
                return 1;
            if (std::strcmp(name, "N10__cxxabiv121__vmi_class_type_infoE") == 0)
                return 2;
            return 0;
        }

        void Walk(const ItaniumTypeInfo* type, std::vector<std::string>& names, int depth)
        {
            if (!type || depth > 64)
                return;
            names.push_back(Plain(type->name));
            switch (KindOf(type))
            {
            case 1:
                Walk(static_cast<const ItaniumSiTypeInfo*>(type)->base, names, depth + 1);
                break;
            case 2:
            {
                const auto* vmi = static_cast<const ItaniumVmiTypeInfo*>(type);
                for (uint32_t i = 0; i < vmi->baseCount; i++)
                    Walk(vmi->bases[i].type, names, depth + 1);
                break;
            }
            default:
                break;
            }
        }

        std::vector<std::string> ClassNames(const void* vtable)
        {
            std::vector<std::string> names;
            Walk(static_cast<const ItaniumTypeInfo* const*>(vtable)[-1], names, 0);
            return names;
        }
#endif

        // Per vtable: every class name the object's class is (its own first).
        std::unordered_map<const void*, std::vector<std::string>> s_classNames;
    }

    StoredTarget StoredTarget::From(const HookTarget& t)
    {
        StoredTarget s;
        s.kind = t.kind;
        s.instance = t.instance;
        if (t.className)
            s.className = t.className;
        return s;
    }

    StoredFilter StoredFilter::From(const HookFilter& f)
    {
        StoredFilter s;
        if (f.designerName)
            s.designerName = f.designerName;
        if (f.name)
            s.name = f.name;
        return s;
    }

    Source2ModHookId NextId()
    {
        return hookid::Next();
    }

    bool IsA(const void* object, const std::string& className)
    {
        if (!object)
            return false;
        const void* vtable = *static_cast<const void* const*>(object);
        if (!vtable)
            return false;
        auto it = s_classNames.find(vtable);
        if (it == s_classNames.end())
            it = s_classNames.emplace(vtable, ClassNames(vtable)).first;
        for (const auto& name : it->second)
            if (name == className)
                return true;
        return false;
    }

    bool Matches(const StoredTarget& target, const StoredFilter& filter, const Subject& subject)
    {
        switch (target.kind)
        {
        case HookTarget::Kind::Instance:
            if (subject.object != target.instance)
                return false;
            break;
        case HookTarget::Kind::Class:
            if (!IsA(subject.object, target.className))
                return false;
            break;
        case HookTarget::Kind::All:
            break;
        }

        if (!filter.designerName.empty())
        {
            const char* designer = subject.entity ? subject.entity->GetClassname() : nullptr;
            if (!designer || filter.designerName != designer)
                return false;
        }
        if (!filter.name.empty() && (!subject.name || filter.name != subject.name))
            return false;
        return true;
    }

    void SetHookDebug(const char* name)
    {
        s_hookDebug = (!name || !strcmp(name, "off")) ? "" : name;
    }

    const char* GetHookDebug()
    {
        return s_hookDebug.empty() ? "off" : s_hookDebug.c_str();
    }

    bool DebugOn(const char* pointName)
    {
        return !s_hookDebug.empty() && pointName && (s_hookDebug == "all" || strstr(pointName, s_hookDebug.c_str()));
    }

    std::string OwnerName(PluginId owner)
    {
        for (const auto& p : pluginManager.m_plugins)
        {
            if (p && p->id == owner)
            {
                const size_t slash = p->path.find_last_of("/\\");
                return slash == std::string::npos ? p->path : p->path.substr(slash + 1);
            }
        }
        return owner == 0 ? std::string("core") : "plugin " + std::to_string(owner);
    }

    const char* ActionName(Action a)
    {
        switch (a)
        {
        case Action::Ignore: return "Ignore";
        case Action::Override: return "Override";
        case Action::Supersede: return "Supersede";
        default: return "?";
        }
    }

    void LogHookAnswer(const char* pointName, bool post, PluginId owner, Action a)
    {
        FP_INFO("hookdebug {} {}: {} -> {}", pointName, post ? "post" : "pre", OwnerName(owner), ActionName(a));
    }

    // ---- HooksManager ---------------------------------------------------------------

    HooksManager::HooksManager()
    {
        auto add = [this](HookPointId id, IPointList* list) { m_lists[id].reset(list); };
        add(HookPointId::GameFrame, new PointList<GameFrameContext>());
        add(HookPointId::StartupServer, new PointList<StartupServerContext>());
        add(HookPointId::ClientConnect, new PointList<ClientConnectContext>());
        add(HookPointId::ClientConnected, new PointList<ClientConnectedContext>());
        add(HookPointId::ClientPutInServer, new PointList<ClientPutInServerContext>());
        add(HookPointId::ClientDisconnect, new PointList<ClientDisconnectContext>());
        add(HookPointId::ClientVoice, new PointList<ClientSlotContext>());
        add(HookPointId::ClientSettingsChanged, new PointList<ClientSlotContext>());
        add(HookPointId::GameServerSteamAPIActivated, new PointList<EmptyContext>());
        add(HookPointId::GameServerSteamAPIDeactivated, new PointList<EmptyContext>());
        add(HookPointId::LoadEventsFromFile, new PointList<LoadEventsFromFileContext>());
    }

    const char* HooksManager::PointName(HookPointId point)
    {
        switch (point)
        {
        case HookPointId::GameFrame: return "GameFrame";
        case HookPointId::StartupServer: return "StartupServer";
        case HookPointId::ClientConnect: return "ClientConnect";
        case HookPointId::ClientConnected: return "ClientConnected";
        case HookPointId::ClientPutInServer: return "ClientPutInServer";
        case HookPointId::ClientDisconnect: return "ClientDisconnect";
        case HookPointId::ClientVoice: return "ClientVoice";
        case HookPointId::ClientSettingsChanged: return "ClientSettingsChanged";
        case HookPointId::GameServerSteamAPIActivated: return "GameServerSteamAPIActivated";
        case HookPointId::GameServerSteamAPIDeactivated: return "GameServerSteamAPIDeactivated";
        case HookPointId::LoadEventsFromFile: return "LoadEventsFromFile";
        default: return virtualpoints::Is(point) ? virtualpoints::Name(point) : "?";
        }
    }

    namespace
    {
        bool IsGameFunction(HookPointId p)
        {
            return static_cast<int>(p) >= static_cast<int>(HookPointId::TakeDamage)
                && static_cast<int>(p) <= static_cast<int>(HookPointId::CheckJumpButtonModern);
        }

        bool IsEngine(HookPointId p)
        {
            return static_cast<int>(p) >= static_cast<int>(HookPointId::GameFrame)
                && static_cast<int>(p) <= static_cast<int>(HookPointId::LoadEventsFromFile);
        }

        // The owner of a handler from the point's context type.
        template <typename CONTEXT>
        PluginId OwnerOf(const void* handler)
        {
            return hookid::OwnerOfHandler(*static_cast<const HookHandler<CONTEXT>*>(handler));
        }

        PluginId OwnerOfEnginePoint(HookPointId point, const void* handler)
        {
            switch (point)
            {
            case HookPointId::GameFrame: return OwnerOf<GameFrameContext>(handler);
            case HookPointId::StartupServer: return OwnerOf<StartupServerContext>(handler);
            case HookPointId::ClientConnect: return OwnerOf<ClientConnectContext>(handler);
            case HookPointId::ClientConnected: return OwnerOf<ClientConnectedContext>(handler);
            case HookPointId::ClientPutInServer: return OwnerOf<ClientPutInServerContext>(handler);
            case HookPointId::ClientDisconnect: return OwnerOf<ClientDisconnectContext>(handler);
            case HookPointId::ClientVoice:
            case HookPointId::ClientSettingsChanged: return OwnerOf<ClientSlotContext>(handler);
            case HookPointId::GameServerSteamAPIActivated:
            case HookPointId::GameServerSteamAPIDeactivated: return OwnerOf<EmptyContext>(handler);
            case HookPointId::LoadEventsFromFile: return OwnerOf<LoadEventsFromFileContext>(handler);
            default: return 0;
            }
        }

        template <typename CONTEXT>
        Source2ModHookId AddTo(IPointList* list, PluginId owner, const void* handler, bool post, const HookTarget& target, const HookFilter& filter)
        {
            return static_cast<PointList<CONTEXT>*>(list)->Add(owner, *static_cast<const HookHandler<CONTEXT>*>(handler), post, target, filter);
        }
    }

    Source2ModHookId HooksManager::AddHook(HookPointId point, ISource2ModFunction* function, const HookTarget& target,
                                           const HookFilter& filter, const void* handler, bool post)
    {
        if (!handler)
            return 0;

        PluginId owner;
        if (IsGameFunction(point))
            owner = gamehooks::gameHooksManager.OwnerOfHandler(point, handler);
        else if (virtualpoints::Is(point))
            owner = virtualpoints::OwnerOf(point, handler);
        else if (point == HookPointId::Function)
            owner = OwnerOf<FunctionContext>(handler);
        else
            owner = OwnerOfEnginePoint(point, handler);
        return AddHookFor(owner, point, function, target, filter, handler, post);
    }

    Source2ModHookId HooksManager::AddHookFor(PluginId owner, HookPointId point, ISource2ModFunction* function, const HookTarget& target,
                                              const HookFilter& filter, const void* handler, bool post)
    {
        if (!handler)
            return 0;

        if (IsGameFunction(point))
            return gamehooks::gameHooksManager.Add(point, owner, handler, post, target, filter);

        if (virtualpoints::Is(point))
            return virtualpoints::Add(point, owner, handler, post, target, filter);

        if (point == HookPointId::Function)
        {
            if (!function)
                return 0;
            return dynamichooks::dynamicHooksManager.Add(function, owner, *static_cast<const HookHandler<FunctionContext>*>(handler), post, target, filter);
        }

        auto it = m_lists.find(point);
        if (it == m_lists.end())
            return 0;

        // An engine callback has no object to compare.
        if (IsEngine(point) && target.kind != HookTarget::Kind::All)
        {
            FP_WARN("Hook on {}: an engine callback takes HookTarget::All() only", PointName(point));
            return 0;
        }

        IPointList* list = it->second.get();
        switch (point)
        {
        case HookPointId::GameFrame: return AddTo<GameFrameContext>(list, owner, handler, post, target, filter);
        case HookPointId::StartupServer: return AddTo<StartupServerContext>(list, owner, handler, post, target, filter);
        case HookPointId::ClientConnect: return AddTo<ClientConnectContext>(list, owner, handler, post, target, filter);
        case HookPointId::ClientConnected: return AddTo<ClientConnectedContext>(list, owner, handler, post, target, filter);
        case HookPointId::ClientPutInServer: return AddTo<ClientPutInServerContext>(list, owner, handler, post, target, filter);
        case HookPointId::ClientDisconnect: return AddTo<ClientDisconnectContext>(list, owner, handler, post, target, filter);
        case HookPointId::ClientVoice:
        case HookPointId::ClientSettingsChanged: return AddTo<ClientSlotContext>(list, owner, handler, post, target, filter);
        case HookPointId::GameServerSteamAPIActivated:
        case HookPointId::GameServerSteamAPIDeactivated: return AddTo<EmptyContext>(list, owner, handler, post, target, filter);
        case HookPointId::LoadEventsFromFile: return AddTo<LoadEventsFromFileContext>(list, owner, handler, post, target, filter);
        default: return 0;
        }
    }

    bool HooksManager::Unhook(Source2ModHookId id)
    {
        if (id <= 0)
            return false;
        for (auto& [point, list] : m_lists)
            if (list->Remove(id))
                return true;
        return gamehooks::gameHooksManager.Remove(id) || dynamichooks::dynamicHooksManager.Remove(id);
    }

    bool HooksManager::IsAvailable(HookPointId point)
    {
        if (IsGameFunction(point))
            return gamehooks::gameHooksManager.IsAvailable(point);
        if (virtualpoints::Is(point))
            return virtualpoints::IsAvailable(point);
        return point == HookPointId::Function || m_lists.count(point) > 0;
    }

    void ForgetInstance(const void* instance)
    {
        hooksManager.RemoveInstance(instance);
    }

    void HooksManager::RemoveInstance(const void* instance)
    {
        for (auto& [point, list] : m_lists)
            list->RemoveInstance(instance);
        gamehooks::gameHooksManager.RemoveInstance(instance);
        dynamichooks::dynamicHooksManager.RemoveInstance(instance);
    }

    void HooksManager::RemoveAllForPlugin(PluginId id)
    {
        for (auto& [point, list] : m_lists)
            list->RemoveOwner(id);
        gamehooks::gameHooksManager.RemoveAllForPlugin(id);
        dynamichooks::dynamicHooksManager.RemoveAllForPlugin(id);
    }
}
