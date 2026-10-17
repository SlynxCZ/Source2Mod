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

#pragma once

#include <source2mod/ISource2ModHooks.h>

#include "slowguard.h"

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

class CEntityInstance;

/*
 * ISource2ModHooks: the registry every hook point shares (PointList: the
 * handlers of one point with their target and filter, and the dispatch over
 * them) and the manager that hands Hook() to the right kind:
 *   - game functions: gamehooks.cpp (one detour per function);
 *   - engine callbacks and entity events: lists here, dispatched from the
 *     core's own hooks (virtualhooks.cpp, inlinehooks.cpp);
 *   - functions: dynamichooks.cpp.
 */
namespace hooks
{
    /// A HookTarget the core keeps (the class name copied).
    struct StoredTarget
    {
        HookTarget::Kind kind = HookTarget::Kind::All;
        const void* instance = nullptr;
        std::string className;

        static StoredTarget From(const HookTarget& t);
    };

    /// A HookFilter the core keeps.
    struct StoredFilter
    {
        std::string designerName;
        std::string name;

        static StoredFilter From(const HookFilter& f);
    };

    /// What a call is about, for the target and filter checks.
    struct Subject
    {
        const void* object = nullptr;           // compared with Instance / Class targets
        CEntityInstance* entity = nullptr;      // its designer name, for HookFilter::designerName
        const char* name = nullptr;             // the output / input, for HookFilter::name
    };

    Source2ModHookId NextId();

    /// Whether @p object is @p className or derives from it (RTTI; cached per vtable).
    bool IsA(const void* object, const std::string& className);

    bool Matches(const StoredTarget& target, const StoredFilter& filter, const Subject& subject);

    // "s2m hookdebug": part of the point names whose calls are logged, "all", or off.
    void SetHookDebug(const char* name);
    const char* GetHookDebug();
    bool DebugOn(const char* pointName);
    std::string OwnerName(PluginId owner);
    const char* ActionName(Action a);

    template <typename CONTEXT>
    struct Entry
    {
        Source2ModHookId id;
        PluginId owner;
        HookHandler<CONTEXT> handler;
        StoredTarget target;
        StoredFilter filter;
    };

    /// What every list of handlers can be asked without its context type.
    class IPointList
    {
    public:
        virtual ~IPointList() = default;
        virtual bool Remove(Source2ModHookId id) = 0;
        virtual void RemoveOwner(PluginId owner) = 0;
        /// Drops the hooks on HookTarget::Instance(instance).
        virtual void RemoveInstance(const void* instance) = 0;
        virtual bool Empty() const = 0;
    };

    /// The handlers of one point, pre and post, and the dispatch over them:
    /// Supersede stops the chain, otherwise the strongest answer wins.
    template <typename CONTEXT>
    class PointList final : public IPointList
    {
    public:
        Source2ModHookId Add(PluginId owner, HookHandler<CONTEXT> handler, bool post, const HookTarget& target, const HookFilter& filter)
        {
            const Source2ModHookId id = NextId();
            (post ? m_post : m_pre).push_back({ id, owner, std::move(handler), StoredTarget::From(target), StoredFilter::From(filter) });
            return id;
        }

        bool Remove(Source2ModHookId id) override
        {
            const auto match = [id](const Entry<CONTEXT>& e) { return e.id == id; };
            return std::erase_if(m_pre, match) + std::erase_if(m_post, match) > 0;
        }

        void RemoveOwner(PluginId owner) override
        {
            const auto match = [owner](const Entry<CONTEXT>& e) { return e.owner == owner; };
            std::erase_if(m_pre, match);
            std::erase_if(m_post, match);
        }

        void RemoveInstance(const void* instance) override
        {
            const auto match = [instance](const Entry<CONTEXT>& e)
            {
                return e.target.kind == HookTarget::Kind::Instance && e.target.instance == instance;
            };
            std::erase_if(m_pre, match);
            std::erase_if(m_post, match);
        }

        bool Empty() const override { return m_pre.empty() && m_post.empty(); }
        bool Has(bool post) const { return !(post ? m_post : m_pre).empty(); }

        /// APPLY(ctx, value) takes a value a handler returned with its
        /// action into ctx.result, before the next handler runs.
        template <typename APPLY>
        Action Dispatch(CONTEXT& ctx, bool post, const Subject& subject, const char* pointName, APPLY&& apply) const
        {
            // A copy: a handler may unhook from inside itself.
            const auto list = post ? m_post : m_pre;
            Action result = Action::Ignore;

            for (const auto& e : list)
            {
                if (!Matches(e.target, e.filter, subject))
                    continue;

                Action a;
                {
                    ::slow::Guard slowGuard("hook handler", pointName, e.owner);
                    const auto answer = e.handler(ctx, post);
                    a = answer.action;
                    if constexpr (!std::is_void_v<typename CONTEXT::Return>)
                    {
                        if (answer.hasValue && a != Action::Ignore)
                            apply(ctx, answer.value);
                    }
                }

                if (DebugOn(pointName))
                    LogAnswer(pointName, post, e.owner, a);

                if (a == Action::Supersede)
                    return a;
                if (static_cast<int>(a) > static_cast<int>(result))
                    result = a;
            }
            return result;
        }

        /// A point without a return value, or one whose result field the
        /// handler sets itself.
        Action Dispatch(CONTEXT& ctx, bool post, const Subject& subject, const char* pointName) const
        {
            return Dispatch(ctx, post, subject, pointName, [](CONTEXT& c, const auto& value)
            {
                if constexpr (!std::is_void_v<typename CONTEXT::Return>)
                    c.result = value;
            });
        }

    private:
        static void LogAnswer(const char* pointName, bool post, PluginId owner, Action a);

        std::vector<Entry<CONTEXT>> m_pre;
        std::vector<Entry<CONTEXT>> m_post;
    };

    void LogHookAnswer(const char* pointName, bool post, PluginId owner, Action a);

    template <typename CONTEXT>
    void PointList<CONTEXT>::LogAnswer(const char* pointName, bool post, PluginId owner, Action a)
    {
        LogHookAnswer(pointName, post, owner, a);
    }

    class HooksManager final : public ISource2ModHooks
    {
    public:
        HooksManager();

        Source2ModHookId AddHook(HookPointId point, ISource2ModFunction* function, const HookTarget& target,
                                 const HookFilter& filter, const void* handler, bool post) override;
        bool Unhook(Source2ModHookId id) override;
        bool IsAvailable(HookPointId point) override;

        /// Hook() with the owner given: the SourcePawn natives, whose
        /// handlers are the core's code.
        Source2ModHookId AddHookFor(PluginId owner, HookPointId point, ISource2ModFunction* function, const HookTarget& target,
                                    const HookFilter& filter, const void* handler, bool post);

        void RemoveAllForPlugin(PluginId id);
        void RemoveInstance(const void* instance);

        /// The list of an engine or entity point.
        template <typename CONTEXT>
        PointList<CONTEXT>& List(HookPointId point)
        {
            return *static_cast<PointList<CONTEXT>*>(m_lists.at(point).get());
        }

        /// Runs an engine or entity point's handlers; Ignore when it has none.
        template <typename CONTEXT>
        Action Dispatch(HookPointId point, CONTEXT& ctx, bool post, const Subject& subject = {})
        {
            auto& list = List<CONTEXT>(point);
            if (!list.Has(post))
                return Action::Ignore;
            return list.Dispatch(ctx, post, subject, PointName(point));
        }

        static const char* PointName(HookPointId point);

    private:
        std::unordered_map<HookPointId, std::unique_ptr<IPointList>> m_lists;
    };

    extern HooksManager hooksManager;

    /// An object is going away (an entity deleted): every hook on
    /// HookTarget::Instance(it) goes with it.
    void ForgetInstance(const void* instance);
}
