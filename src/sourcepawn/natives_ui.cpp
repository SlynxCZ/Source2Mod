/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for menus, custom HUD clicks and cs_script
 * (source2mod/menus.inc, customhud.inc, scripts.inc).
 *
 * A CenterHtmlMenu handle owns its menu. Deleting it closes the menu for
 * everyone who has it open, on the next frame: the delete may come from the
 * menu's own option handler, and the core still reads the menu after that
 * handler returns. Until then its options do nothing.
 *
 * Project: Source2Mod
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

        cell_t IndexOf(CEntityInstance* inst)
        {
            return inst ? static_cast<cell_t>(inst->GetEntityIndex().Get()) : -1;
        }

        cell_t IndexOf(const CEntityHandle& handle)
        {
            return handle.IsValid() ? handle.GetEntryIndex() : -1;
        }

        template <typename T>
        T* EntityArg(IPluginContext* ctx, cell_t index, const char* what)
        {
            T* ent = (index >= 0 && index < 32768) ? CBaseEntity::FromIndex<T>(index) : nullptr;
            if (!ent)
                ctx->ThrowNativeError("%s %d is invalid", what, index);
            return ent;
        }

        CCSPlayerController* PlayerArg(IPluginContext* ctx, cell_t index)
        {
            return EntityArg<CCSPlayerController>(ctx, index, "Player");
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

        void CloseEverywhere(const IMenu* menu)
        {
            for (int slot = 0; slot < kMaxClients; slot++)
            {
                auto* player = CBaseEntity::FromIndex<CCSPlayerController>(slot + 1);
                if (!player)
                    continue;
                IMenuInstance* open = menus::menuManager.GetActiveMenu(player);
                if (open && open->Menu() == menu)
                    menus::menuManager.CloseActiveMenu(player);
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
            args.PushCell(IndexOf(player));
            args.PushCell(m->handle);
            args.PushCell(option);
            args.PushCell(data);
            Invoke(fn, args);
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

        // AddMenuOptionWithCooldown(text, handler, data, disabled, close): IBaseMenu's
        // version with the default cooldown handlers (no cooldown).
        cell_t N_MenuAddMenuOptionWithCooldown(IPluginContext* ctx, const cell_t* params)
        {
            ScriptMenu* m = MenuOf(ctx, params[1]);
            IPluginFunction* fn = m ? Function(ctx, params[3]) : nullptr;
            if (!fn)
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
            IMenuInstance* open = player ? menus::menuManager.GetActiveMenu(player) : nullptr;
            if (!open)
                return 0;
            auto it = g_menus.find(open->Menu());
            return it != g_menus.end() && it->second->owner == CallerId(ctx) ? it->second->handle : 0;
        }

        cell_t N_MenusCloseActiveMenu(IPluginContext* ctx, const cell_t* params)
        {
            if (CCSPlayerController* player = PlayerArg(ctx, params[1]))
                menus::menuManager.CloseActiveMenu(player);
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
            auto* layout = EntityArg<CCSCustomHudLayout>(ctx, params[1], "Layout");
            IPluginFunction* fn = layout ? Function(ctx, params[2]) : nullptr;
            if (!fn)
                return 0;
            const PluginId owner = CallerId(ctx);
            CustomHudClickHandler handler([fn](CCSPlayerController* player, CCSCustomHudLayout* l, const char* buttonId)
            {
                sp::CallArgs args;
                args.PushCell(IndexOf(player));
                args.PushCell(IndexOf(l));
                args.PushString(buttonId ? buttonId : "");
                Invoke(fn, args);
            });
            const Source2ModHookId id = customhud::customHudManager.HookCustomHudClick(owner, layout, std::move(handler));
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
            auto* layout = EntityArg<CCSCustomHudLayout>(ctx, params[1], "Layout");
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
            return IndexOf(scripts::scriptsManager.LoadAsset(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]), params[3] != 0));
        }

        // RunSource(name, virtualPath, source, persistent)
        cell_t N_ScriptsRunSource(IPluginContext* ctx, const cell_t* params)
        {
            return IndexOf(scripts::scriptsManager.RunSource(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]),
                                                             LocalString(ctx, params[3]), params[4] != 0));
        }

        // RunFile(name, filePath, persistent)
        cell_t N_ScriptsRunFile(IPluginContext* ctx, const cell_t* params)
        {
            return IndexOf(scripts::scriptsManager.RunFile(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]), params[3] != 0));
        }

        cell_t N_ScriptsReload(IPluginContext* ctx, const cell_t* params)
        {
            return IndexOf(scripts::scriptsManager.Reload(LocalString(ctx, params[1])));
        }

        cell_t N_ScriptsRemove(IPluginContext* ctx, const cell_t* params)
        {
            scripts::scriptsManager.Remove(LocalString(ctx, params[1]));
            return 0;
        }

        cell_t N_ScriptsFind(IPluginContext* ctx, const cell_t* params)
        {
            return IndexOf(scripts::scriptsManager.Find(LocalString(ctx, params[1])));
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
            const Source2ModHookId id = scripts::scriptsManager.HookScriptMessage(owner, LocalString(ctx, params[1]), std::move(handler));
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
