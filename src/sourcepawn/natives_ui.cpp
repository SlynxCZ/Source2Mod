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
 * SourcePawn natives for menus, custom HUD clicks and cs_script
 * (source2mod/menus.inc, customhud.inc, scripts.inc).
 *
 * A CenterHtmlMenu handle owns its menu. Deleting it closes the menu for
 * everyone who has it open, on the next frame: the delete may come from the
 * menu's own option handler, and the core still reads the menu after that
 * handler returns. Until then its options do nothing.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/customhud.h"
#include "core/menus.h"
#include "core/scheduler.h"
#include "core/scripts.h"

#include <source2mod/schema/entity/classes/CBaseEntity.h>
#include <source2mod/schema/entity/classes/CCSCustomHudLayout.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <sp_callable.h>

#include <functional>
#include <map>
#include <string>
#include <unordered_map>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        template <typename T>
        T* TypedEntityArg(IPluginContext* ctx, cell_t entity)
        {
            return static_cast<T*>(EntityArg(ctx, entity));
        }

        CCSPlayerController* PlayerArg(IPluginContext* ctx, cell_t entity)
        {
            return TypedEntityArg<CCSPlayerController>(ctx, entity);
        }

        // ---- CenterHtmlMenu ------------------------------------------------------------

        struct ScriptMenu : HandleObject
        {
            explicit ScriptMenu(const char* title) : menu(title) {}

            CenterHtmlMenu menu;
            PluginId owner = 0;
            cell_t handle = 0;
            bool dead = false;
        };

        /// The script menus by their CenterHtmlMenu, for GetActiveMenu.
        std::unordered_map<const IMenu*, ScriptMenu*> g_menus;

        /// Closes a script menu wherever it is open (also as a submenu) and
        /// unlinks it from the script menus that open it as a submenu.
        void CloseEverywhere(CenterHtmlMenu* menu)
        {
            menus::menuManager.CloseMenu(menu);
            for (auto& [key, other] : g_menus)
            {
                for (ChatMenuOption& opt : other->menu.Options())
                {
                    if (opt.Submenu == menu)
                        opt.Submenu = nullptr;
                }
            }
        }

        HandleType MenuType()
        {
            static const HandleType t = Handles().CreateType("CenterHtmlMenu", [](void* p)
            {
                auto* m = static_cast<ScriptMenu*>(p);
                m->dead = true;
                g_menus.erase(&m->menu);
                scheduler::schedulerManager.NextFrame(0, [m]()
                {
                    CloseEverywhere(&m->menu);
                    delete m;
                });
            });
            return t;
        }

        ScriptMenu* MenuOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ScriptMenu*>(ReadHandle(ctx, handle, MenuType()));
        }

        bool OptionOk(IPluginContext* ctx, ScriptMenu* m, cell_t option)
        {
            if (option < 0 || option >= static_cast<cell_t>(m->menu.Options().size()))
            {
                ctx->ThrowNativeError("Invalid option %d (count: %d)", option, static_cast<int>(m->menu.Options().size()));
                return false;
            }
            return true;
        }

        /// Calls the script's option handler: (player, menu, option, data).
        void SelectOption(ScriptMenu* m, IPluginFunction* fn, cell_t option, cell_t data, CCSPlayerController* player)
        {
            if (m->dead)
                return;
            sp::CallArgs args;
            args.PushCell(EntityCell(player));
            args.PushCell(m->handle);
            args.PushCell(option);
            args.PushCell(data);
            Invoke(fn, args);
        }

        constexpr cell_t kInvalidFunction = -1;     // INVALID_FUNCTION

        /// An unset function argument: INVALID_FUNCTION compiles to 0 with this spcomp, -1 with others.
        bool IsUnset(cell_t id)
        {
            return id == kInvalidFunction || id == 0;
        }

        /// An evaluator calling the script's (player, menu, option, data) -> bool;
        /// run each time the menu is drawn. @p whenDead answers once the menu is deleted.
        std::function<bool(CCSPlayerController*)> Evaluator(ScriptMenu* m, IPluginFunction* fn, cell_t option, cell_t data, bool whenDead)
        {
            return [m, fn, option, data, whenDead](CCSPlayerController* player) -> bool
            {
                if (m->dead)
                    return whenDead;
                sp::CallArgs args;
                args.PushCell(EntityCell(player));
                args.PushCell(m->handle);
                args.PushCell(option);
                args.PushCell(data);
                cell_t res = 0;
                Invoke(fn, args, &res);
                return res != 0;
            };
        }

        /// Text from the script's (player, menu, option, buffer, maxlen, data); option -1 is the title.
        std::function<std::string(CCSPlayerController*)> TextFrom(ScriptMenu* m, IPluginFunction* fn, cell_t option, cell_t data)
        {
            return [m, fn, option, data](CCSPlayerController* player) -> std::string
            {
                if (m->dead)
                    return {};
                char buffer[256] = {};
                sp::CallArgs args;
                args.PushCell(EntityCell(player));
                args.PushCell(m->handle);
                args.PushCell(option);
                args.PushString(buffer, sizeof(buffer), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY | SM_PARAM_COPYBACK);
                args.PushCell(sizeof(buffer));
                args.PushCell(data);
                Invoke(fn, args);
                buffer[sizeof(buffer) - 1] = '\0';
                return buffer;
            };
        }

        /// The option handler for AddMenuOption-style options.
        std::function<void(CCSPlayerController*, ChatMenuOption&)> Handler(ScriptMenu* m, IPluginFunction* fn, cell_t option, cell_t data)
        {
            if (!fn)
                return nullptr;
            return [m, fn, option, data](CCSPlayerController* player, ChatMenuOption&)
            {
                SelectOption(m, fn, option, data, player);
            };
        }

        cell_t N_MenuNew(IPluginContext* ctx, const cell_t* params)
        {
            auto* m = new ScriptMenu(LocalString(ctx, params[1]));
            m->owner = CallerId(ctx);
            m->handle = Handles().Create(MenuType(), m, m->owner);
            if (!m->handle)
            {
                delete m;
                return 0;
            }
            g_menus[&m->menu] = m;
            return m->handle;
        }

        cell_t N_MenuSetTitle(IPluginContext* ctx, const cell_t* params)
        {
            if (ScriptMenu* m = MenuOf(ctx, params[1]))
                m->menu.SetTitle(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_MenuGetTitle(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            return m ? ToBuffer(ctx, params[2], params[3], m->menu.Title().c_str()) : 0;
        }

        // AddMenuOption(text, handler, data, disabled)
        cell_t N_MenuAddMenuOption(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            IPluginFunction* fn = m ? Function(ctx, params[3]) : nullptr;
            if (!fn)
                return -1;
            const cell_t option = static_cast<cell_t>(m->menu.Options().size());
            const cell_t data = params[4];
            m->menu.AddMenuOption(LocalString(ctx, params[2]), [m, fn, option, data](CCSPlayerController* player, ChatMenuOption&)
            {
                SelectOption(m, fn, option, data, player);
            }, params[5] != 0);
            return option;
        }

        // AddMenuOptionWithCooldown(text, handler, data, disabled, close, evaluator): IBaseMenu's
        // version with the default cooldown handlers (no cooldown). evaluator came later:
        // scripts built before it pass none.
        cell_t N_MenuAddMenuOptionWithCooldown(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            IPluginFunction* fn = m ? Function(ctx, params[3]) : nullptr;
            if (!fn)
                return -1;
            IPluginFunction* eval = nullptr;
            // INVALID_FUNCTION compiles to 0 with this spcomp, -1 with others: both are "none".
            if (params[0] >= 7 && !IsUnset(params[7]) && !(eval = Function(ctx, params[7])))
                return -1;
            const cell_t option = static_cast<cell_t>(m->menu.Options().size());
            const cell_t data = params[4];
            const bool close = params[6] != 0;
            m->menu.AddMenuOption(LocalString(ctx, params[2]), [m, fn, option, data, close](CCSPlayerController* player, ChatMenuOption& opt)
            {
                const std::string text = opt.Text;
                SelectOption(m, fn, option, data, player);
                if (close)
                {
                    menus::menuManager.CloseActiveMenu(player);
                    if (player)
                        player->PrintToCenterHtml(text.c_str(), 5, true);
                }
            }, params[5] != 0);
            if (eval)
                m->menu.Options().back().DisabledEvaluator = Evaluator(m, eval, option, data, true);
            return option;
        }

        cell_t N_MenuClearOptions(IPluginContext* ctx, const cell_t* params)
        {
            if (ScriptMenu* m = MenuOf(ctx, params[1]))
                m->menu.ClearOptions();
            return 0;
        }

        cell_t N_MenuOptionCount(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            return m ? static_cast<cell_t>(m->menu.Options().size()) : 0;
        }

        cell_t N_MenuSetOptionText(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (m && OptionOk(ctx, m, params[2]))
                m->menu.Options()[params[2]].Text = LocalString(ctx, params[3]);
            return 0;
        }

        cell_t N_MenuGetOptionText(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            return m && OptionOk(ctx, m, params[2]) ? ToBuffer(ctx, params[3], params[4], m->menu.Options()[params[2]].Text.c_str()) : 0;
        }

        cell_t N_MenuSetOptionDisabled(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (m && OptionOk(ctx, m, params[2]))
                m->menu.Options()[params[2]].Disabled = params[3] != 0;
            return 0;
        }

        cell_t N_MenuIsOptionDisabled(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            return m && OptionOk(ctx, m, params[2]) && m->menu.Options()[params[2]].Disabled;
        }

        // SetOptionDisabledEvaluator(option, evaluator, data): INVALID_FUNCTION removes it.
        cell_t N_MenuSetOptionDisabledEvaluator(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (!m || !OptionOk(ctx, m, params[2]))
                return 0;
            ChatMenuOption& opt = m->menu.Options()[params[2]];
            if (IsUnset(params[3]))
            {
                opt.DisabledEvaluator = nullptr;
                return 0;
            }
            if (IPluginFunction* fn = Function(ctx, params[3]))
                opt.DisabledEvaluator = Evaluator(m, fn, params[2], params[4], true);
            return 0;
        }

        // AddSubmenu(text, submenu, disabled)
        cell_t N_MenuAddSubmenu(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            ScriptMenu* sub = m ? MenuOf(ctx, params[3]) : nullptr;
            if (!sub)
                return -1;
            if (sub == m)
                return ctx->ThrowNativeError("A menu cannot be its own submenu");
            const cell_t option = static_cast<cell_t>(m->menu.Options().size());
            m->menu.AddSubmenu(LocalString(ctx, params[2]), &sub->menu, params[0] >= 4 && params[4]);
            return option;
        }

        cell_t N_MenuAddText(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (!m)
                return -1;
            const cell_t option = static_cast<cell_t>(m->menu.Options().size());
            m->menu.AddText(LocalString(ctx, params[2]));
            return option;
        }

        // AddDynamicOption(text, handler, data, disabled, visible)
        cell_t N_MenuAddDynamicOption(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            IPluginFunction* text = m ? Function(ctx, params[2]) : nullptr;
            if (!text)
                return -1;
            IPluginFunction *handler = nullptr, *disabled = nullptr, *visible = nullptr;
            if ((params[0] >= 3 && !IsUnset(params[3]) && !(handler = Function(ctx, params[3])))
                || (params[0] >= 5 && !IsUnset(params[5]) && !(disabled = Function(ctx, params[5])))
                || (params[0] >= 6 && !IsUnset(params[6]) && !(visible = Function(ctx, params[6]))))
                return -1;
            const cell_t option = static_cast<cell_t>(m->menu.Options().size());
            const cell_t data = params[0] >= 4 ? params[4] : 0;
            m->menu.AddDynamicOption(TextFrom(m, text, option, data), Handler(m, handler, option, data),
                                     disabled ? Evaluator(m, disabled, option, data, true) : nullptr,
                                     visible ? Evaluator(m, visible, option, data, false) : nullptr);
            return option;
        }

        // Option flags (menus.inc MenuOption_*).
        constexpr cell_t kOptDisabled = 1 << 0;
        constexpr cell_t kOptHidden = 1 << 1;
        constexpr cell_t kOptClose = 1 << 2;
        constexpr cell_t kOptStay = 1 << 3;

        cell_t N_MenuSetOptionFlags(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (!m || !OptionOk(ctx, m, params[2]))
                return 0;
            ChatMenuOption& opt = m->menu.Options()[params[2]];
            const cell_t flags = params[3];
            opt.Disabled = (flags & kOptDisabled) != 0;
            opt.Hidden = (flags & kOptHidden) != 0;
            opt.CloseOnSelect = (flags & kOptClose) != 0;
            opt.StayOnPage = (flags & kOptStay) != 0;
            return 0;
        }

        cell_t N_MenuGetOptionFlags(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (!m || !OptionOk(ctx, m, params[2]))
                return 0;
            const ChatMenuOption& opt = m->menu.Options()[params[2]];
            return (opt.Disabled ? kOptDisabled : 0) | (opt.Hidden ? kOptHidden : 0) | (opt.CloseOnSelect ? kOptClose : 0)
                 | (opt.StayOnPage ? kOptStay : 0);
        }

        // SetDynamicTitle(text, data): INVALID_FUNCTION goes back to the fixed title.
        cell_t N_MenuSetDynamicTitle(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (!m)
                return 0;
            if (IsUnset(params[2]))
            {
                m->menu.SetDynamicTitle(nullptr);
                return 0;
            }
            if (IPluginFunction* fn = Function(ctx, params[2]))
                m->menu.SetDynamicTitle(TextFrom(m, fn, -1, params[0] >= 3 ? params[3] : 0));
            return 0;
        }

        // OnEnd(callback): (player, menu, reason); INVALID_FUNCTION removes it.
        cell_t N_MenuOnEnd(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (!m)
                return 0;
            if (IsUnset(params[2]))
            {
                m->menu.SetEndHandler(nullptr);
                return 0;
            }
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            m->menu.SetEndHandler([m, fn](CCSPlayerController* player, MenuEndReason reason)
            {
                if (m->dead)
                    return;
                sp::CallArgs args;
                args.PushCell(EntityCell(player));
                args.PushCell(m->handle);
                args.PushCell(static_cast<cell_t>(reason));
                Invoke(fn, args);
            });
            return 0;
        }

        cell_t N_MenuItemsPerPageGet(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            return m ? m->menu.GetItemsPerPage() : 0;
        }

        cell_t N_MenuItemsPerPageSet(IPluginContext* ctx, const cell_t* params)
        {
            if (ScriptMenu* m = MenuOf(ctx, params[1]))
                m->menu.SetItemsPerPage(params[2]);
            return 0;
        }

        cell_t N_MenuTimeoutGet(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            return m ? m->menu.GetTimeout() : 0;
        }

        cell_t N_MenuTimeoutSet(IPluginContext* ctx, const cell_t* params)
        {
            if (ScriptMenu* m = MenuOf(ctx, params[1]))
                m->menu.SetTimeout(params[2]);
            return 0;
        }

        // Close(player = NULL_ENTITY): one player, or everyone.
        cell_t N_MenuClose(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (!m)
                return 0;
            const cell_t index = params[0] >= 2 ? params[2] : -1;
            if (index == -1)
            {
                menus::menuManager.CloseMenu(&m->menu);
                return 0;
            }
            CCSPlayerController* player = PlayerArg(ctx, index);
            if (player && menus::menuManager.IsMenuOpen(player, &m->menu))
            {
                menus::menuManager.CloseActiveMenu(player);
                player->PrintToCenterHtml(" ", 3, true);
            }
            return 0;
        }

        cell_t N_MenuIsOpenFor(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            CCSPlayerController* player = m ? PlayerArg(ctx, params[2]) : nullptr;
            return player && menus::menuManager.IsMenuOpen(player, &m->menu);
        }

        cell_t N_MenuPostSelectGet(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            return m ? static_cast<cell_t>(m->menu.GetPostSelectAction()) : 0;
        }

        cell_t N_MenuPostSelectSet(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (!m)
                return 0;
            if (params[2] < static_cast<cell_t>(PostSelectAction::Close) || params[2] > static_cast<cell_t>(PostSelectAction::Nothing))
                return ctx->ThrowNativeError("Invalid PostSelectAction %d", params[2]);
            m->menu.SetPostSelectAction(static_cast<PostSelectAction>(params[2]));
            return 0;
        }

        cell_t N_MenuExitButtonGet(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            return m && m->menu.GetExitButton();
        }

        cell_t N_MenuExitButtonSet(IPluginContext* ctx, const cell_t* params)
        {
            if (ScriptMenu* m = MenuOf(ctx, params[1]))
                m->menu.SetExitButton(params[2] != 0);
            return 0;
        }

        std::string* ColorOf(IPluginContext* ctx, ScriptMenu* m, cell_t which)
        {
            switch (which)
            {
            case 0: return &m->menu.TitleColor;
            case 1: return &m->menu.EnabledColor;
            case 2: return &m->menu.DisabledColor;
            case 3: return &m->menu.PrevPageColor;
            case 4: return &m->menu.NextPageColor;
            case 5: return &m->menu.CloseColor;
            }
            ctx->ThrowNativeError("Invalid menu color %d", which);
            return nullptr;
        }

        cell_t N_MenuSetColor(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            if (std::string* color = m ? ColorOf(ctx, m, params[2]) : nullptr)
                *color = LocalString(ctx, params[3]);
            return 0;
        }

        cell_t N_MenuGetColor(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            std::string* color = m ? ColorOf(ctx, m, params[2]) : nullptr;
            return color ? ToBuffer(ctx, params[3], params[4], color->c_str()) : 0;
        }

        // ---- Source2ModMenus -----------------------------------------------------------

        cell_t N_MenusOpenCenterHtmlMenu(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = PlayerArg(ctx, params[1]);
            ScriptMenu* m = player ? MenuOf(ctx, params[2]) : nullptr;
            if (m)
                menus::menuManager.OpenCenterHtmlMenu(CallerId(ctx), player, &m->menu);
            return 0;
        }

        cell_t N_MenusHasActiveMenu(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = PlayerArg(ctx, params[1]);
            return player && menus::menuManager.GetActiveMenu(player) != nullptr;
        }

        cell_t N_MenusGetActiveMenu(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = PlayerArg(ctx, params[1]);
            IMenu* open = player ? menus::menuManager.GetOpenMenu(player) : nullptr;
            if (!open)
                return 0;
            auto it = g_menus.find(open);
            return it != g_menus.end() && it->second->owner == CallerId(ctx) ? it->second->handle : 0;
        }

        cell_t N_MenusCloseActiveMenu(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* player = PlayerArg(ctx, params[1]))
                menus::menuManager.CloseActiveMenu(player);
            return 0;
        }

        // IMenuInstance::Display: draws the open menu now (it is redrawn every frame anyway).
        cell_t N_MenusRedrawActiveMenu(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = PlayerArg(ctx, params[1]);
            if (IMenuInstance* open = player ? menus::menuManager.GetActiveMenu(player) : nullptr)
                open->Display();
            return 0;
        }

        // IMenuInstance::Reset: back to the first page, then drawn.
        cell_t N_MenusResetActiveMenu(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = PlayerArg(ctx, params[1]);
            if (IMenuInstance* open = player ? menus::menuManager.GetActiveMenu(player) : nullptr)
            {
                open->Reset();
                open->Display();
            }
            return 0;
        }

        cell_t N_MenusOnKeyPress(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* player = PlayerArg(ctx, params[1]))
                menus::menuManager.OnKeyPress(player, params[2]);
            return 0;
        }

        // ---- Source2ModCustomHud -------------------------------------------------------

        struct HudHook
        {
            PluginId owner;
            CEntityHandle layout;
        };
        std::map<Source2ModHookId, HudHook> g_hudHooks;

        // HookCustomHudClick(layout, handler)
        cell_t N_HudHookCustomHudClick(IPluginContext* ctx, const cell_t* params)
        {
            auto* layout = TypedEntityArg<CCSCustomHudLayout>(ctx, params[1]);
            IPluginFunction* fn = layout ? Function(ctx, params[2]) : nullptr;
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            CustomHudClickHandler handler([fn](CCSPlayerController* player, CCSCustomHudLayout* l, const char* buttonId)
            {
                sp::CallArgs args;
                args.PushCell(EntityCell(player));
                args.PushCell(EntityCell(l));
                args.PushString(buttonId ? buttonId : "");
                Invoke(fn, args);
            });
            const Source2ModCustomHudHookId id = customhud::customHudManager.HookCustomHudClick(owner, layout, std::move(handler));
            if (id)
                g_hudHooks[id] = { owner, layout->GetRefEHandle() };
            return static_cast<cell_t>(id);
        }

        cell_t N_HudUnhookCustomHudClick(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_hudHooks.find(params[1]);
            if (it == g_hudHooks.end() || it->second.owner != CallerId(ctx))
                return 0;
            g_hudHooks.erase(it);
            return customhud::customHudManager.UnhookCustomHudClickId(params[1]);
        }

        cell_t N_HudUnhookCustomHudClickLayout(IPluginContext* ctx, const cell_t* params)
        {
            auto* layout = TypedEntityArg<CCSCustomHudLayout>(ctx, params[1]);
            if (!layout)
                return 0;
            const PluginId owner = CallerId(ctx);
            const CEntityHandle handle = layout->GetRefEHandle();
            for (auto it = g_hudHooks.begin(); it != g_hudHooks.end();)
            {
                if (it->second.owner == owner && it->second.layout == handle)
                {
                    customhud::customHudManager.UnhookCustomHudClickId(it->first);
                    it = g_hudHooks.erase(it);
                }
                else
                    ++it;
            }
            return 0;
        }

        // ---- Source2ModScripts ---------------------------------------------------------

        std::map<Source2ModHookId, PluginId> g_scriptHooks;

        cell_t N_ScriptsCanRunSource(IPluginContext*, const cell_t*)
        {
            return scripts::scriptsManager.CanRunSource();
        }

        // LoadAsset(name, asset, persistent)
        cell_t N_ScriptsLoadAsset(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.LoadAsset(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]), params[3] != 0));
        }

        // RunSource(name, virtualPath, source, persistent)
        cell_t N_ScriptsRunSource(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.RunSource(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]),
                                                             LocalString(ctx, params[3]), params[4] != 0));
        }

        // RunFile(name, filePath, persistent)
        cell_t N_ScriptsRunFile(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.RunFile(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]), params[3] != 0));
        }

        cell_t N_ScriptsReload(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.Reload(LocalString(ctx, params[1])));
        }

        cell_t N_ScriptsRemove(IPluginContext* ctx, const cell_t* params)
        {
            scripts::scriptsManager.Remove(LocalString(ctx, params[1]));
            return 0;
        }

        cell_t N_ScriptsFind(IPluginContext* ctx, const cell_t* params)
        {
            return EntityCell(scripts::scriptsManager.Find(LocalString(ctx, params[1])));
        }

        cell_t N_ScriptsFireInput(IPluginContext* ctx, const cell_t* params)
        {
            return scripts::scriptsManager.FireInput(LocalString(ctx, params[1]), LocalString(ctx, params[2]));
        }

        // HookScriptMessage(channel, handler)
        cell_t N_ScriptsHookScriptMessage(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            ScriptMessageHandler handler([fn](const char* channel, const char* payload)
            {
                sp::CallArgs args;
                args.PushString(channel ? channel : "");
                args.PushString(payload ? payload : "");
                Invoke(fn, args);
            });
            const Source2ModScriptMessageHookId id = scripts::scriptsManager.HookScriptMessage(owner, LocalString(ctx, params[1]), std::move(handler));
            if (id)
                g_scriptHooks[id] = owner;
            return static_cast<cell_t>(id);
        }

        cell_t N_ScriptsUnhookScriptMessage(IPluginContext* ctx, const cell_t* params)
        {
            auto it = g_scriptHooks.find(params[1]);
            if (it == g_scriptHooks.end() || it->second != CallerId(ctx))
                return 0;
            g_scriptHooks.erase(it);
            return scripts::scriptsManager.UnhookScriptMessageId(params[1]);
        }

        const sp_nativeinfo_t g_uiNatives[] = {
            { "CenterHtmlMenu.CenterHtmlMenu", N_MenuNew },
            { "CenterHtmlMenu.SetTitle", N_MenuSetTitle },
            { "CenterHtmlMenu.GetTitle", N_MenuGetTitle },
            { "CenterHtmlMenu.AddMenuOption", N_MenuAddMenuOption },
            { "CenterHtmlMenu.AddMenuOptionWithCooldown", N_MenuAddMenuOptionWithCooldown },
            { "CenterHtmlMenu.ClearOptions", N_MenuClearOptions },
            { "CenterHtmlMenu.OptionCount", N_MenuOptionCount },
            { "CenterHtmlMenu.SetOptionText", N_MenuSetOptionText },
            { "CenterHtmlMenu.GetOptionText", N_MenuGetOptionText },
            { "CenterHtmlMenu.SetOptionDisabled", N_MenuSetOptionDisabled },
            { "CenterHtmlMenu.IsOptionDisabled", N_MenuIsOptionDisabled },
            { "CenterHtmlMenu.SetOptionDisabledEvaluator", N_MenuSetOptionDisabledEvaluator },
            { "CenterHtmlMenu.AddSubmenu", N_MenuAddSubmenu },
            { "CenterHtmlMenu.AddText", N_MenuAddText },
            { "CenterHtmlMenu.AddDynamicOption", N_MenuAddDynamicOption },
            { "CenterHtmlMenu.SetOptionFlags", N_MenuSetOptionFlags },
            { "CenterHtmlMenu.GetOptionFlags", N_MenuGetOptionFlags },
            { "CenterHtmlMenu.SetDynamicTitle", N_MenuSetDynamicTitle },
            { "CenterHtmlMenu.OnEnd", N_MenuOnEnd },
            { "CenterHtmlMenu.ItemsPerPage.get", N_MenuItemsPerPageGet },
            { "CenterHtmlMenu.ItemsPerPage.set", N_MenuItemsPerPageSet },
            { "CenterHtmlMenu.Timeout.get", N_MenuTimeoutGet },
            { "CenterHtmlMenu.Timeout.set", N_MenuTimeoutSet },
            { "CenterHtmlMenu.Close", N_MenuClose },
            { "CenterHtmlMenu.IsOpenFor", N_MenuIsOpenFor },
            { "CenterHtmlMenu.PostSelect.get", N_MenuPostSelectGet },
            { "CenterHtmlMenu.PostSelect.set", N_MenuPostSelectSet },
            { "CenterHtmlMenu.ExitButton.get", N_MenuExitButtonGet },
            { "CenterHtmlMenu.ExitButton.set", N_MenuExitButtonSet },
            { "CenterHtmlMenu.SetColor", N_MenuSetColor },
            { "CenterHtmlMenu.GetColor", N_MenuGetColor },
            { "Source2ModMenus.OpenCenterHtmlMenu", N_MenusOpenCenterHtmlMenu },
            { "Source2ModMenus.HasActiveMenu", N_MenusHasActiveMenu },
            { "Source2ModMenus.GetActiveMenu", N_MenusGetActiveMenu },
            { "Source2ModMenus.CloseActiveMenu", N_MenusCloseActiveMenu },
            { "Source2ModMenus.RedrawActiveMenu", N_MenusRedrawActiveMenu },
            { "Source2ModMenus.ResetActiveMenu", N_MenusResetActiveMenu },
            { "Source2ModMenus.OnKeyPress", N_MenusOnKeyPress },

            { "Source2ModCustomHud.HookCustomHudClick", N_HudHookCustomHudClick },
            { "Source2ModCustomHud.UnhookCustomHudClick", N_HudUnhookCustomHudClick },
            { "Source2ModCustomHud.UnhookCustomHudClickLayout", N_HudUnhookCustomHudClickLayout },

            { "Source2ModScripts.CanRunSource", N_ScriptsCanRunSource },
            { "Source2ModScripts.LoadAsset", N_ScriptsLoadAsset },
            { "Source2ModScripts.RunSource", N_ScriptsRunSource },
            { "Source2ModScripts.RunFile", N_ScriptsRunFile },
            { "Source2ModScripts.Reload", N_ScriptsReload },
            { "Source2ModScripts.Remove", N_ScriptsRemove },
            { "Source2ModScripts.Find", N_ScriptsFind },
            { "Source2ModScripts.FireInput", N_ScriptsFireInput },
            { "Source2ModScripts.HookScriptMessage", N_ScriptsHookScriptMessage },
            { "Source2ModScripts.UnhookScriptMessage", N_ScriptsUnhookScriptMessage },
            { nullptr, nullptr },
        };
    }

    void ForgetUiHooks(PluginId id)
    {
        // The core has taken these down already (it unhooks by owner); only the
        // bookkeeping is left.
        for (auto it = g_hudHooks.begin(); it != g_hudHooks.end();)
            it = it->second.owner == id ? g_hudHooks.erase(it) : std::next(it);
        for (auto it = g_scriptHooks.begin(); it != g_scriptHooks.end();)
            it = it->second == id ? g_scriptHooks.erase(it) : std::next(it);
    }

    const sp_nativeinfo_t* GetUiNatives()
    {
        return g_uiNatives;
    }
}
