/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for the menu builder (source2mod/menus.inc:
 * Source2ModMenu) on ISource2ModMenuBuilder. A script's menus are the
 * builder's under the script's plugin id, so they go when it unloads.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/menubuilder.h"
#include "core/scheduler.h"

#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <sp_callable.h>

#include <string>
#include <unordered_map>
#include <vector>

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

        /// An unset function argument (INVALID_FUNCTION) is nullptr; a bad id is a script error.
        bool OptionalFunction(IPluginContext* ctx, cell_t id, IPluginFunction*& fn)
        {
            fn = nullptr;
            if (id == -1)
                return true;
            fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
            {
                ctx->ThrowNativeError("Invalid function id (%X)", id);
                return false;
            }
            return true;
        }

        cell_t IndexOf(CCSPlayerController* player)
        {
            return player ? static_cast<cell_t>(player->GetEntityIndex().Get()) : -1;
        }

        struct ScriptBuilderMenu : HandleObject
        {
            uint64_t id = 0;
        };

        /// Menu id -> its handle, for the callbacks' menu argument.
        std::unordered_map<uint64_t, cell_t> g_handles;

        HandleType BuilderType()
        {
            static const HandleType t = Handles().CreateType("Source2ModMenu", [](void* p)
            {
                auto* m = static_cast<ScriptBuilderMenu*>(p);
                const uint64_t id = m->id;
                g_handles.erase(id);
                delete m;
                // Closing runs OnEnd: not from inside the handle system.
                scheduler::schedulerManager.NextFrame(0, [id]()
                {
                    if (menubuilder::MenuImpl* menu = menubuilder::menuBuilderManager.Find(id))
                        menu->Release();
                });
            });
            return t;
        }

        menubuilder::MenuImpl* MenuOf(IPluginContext* ctx, cell_t handle)
        {
            auto* m = static_cast<ScriptBuilderMenu*>(ReadHandle(ctx, handle, BuilderType()));
            if (!m)
                return nullptr;
            menubuilder::MenuImpl* menu = menubuilder::menuBuilderManager.Find(m->id);
            if (!menu)
                ctx->ThrowNativeError("Menu handle %x was released", handle);
            return menu;
        }

        cell_t HandleOf(ISource2ModMenu* menu)
        {
            auto it = g_handles.find(menu ? static_cast<menubuilder::MenuImpl*>(menu)->id : 0);
            return it != g_handles.end() ? it->second : 0;
        }

        menubuilder::MenuImpl* ItemOk(IPluginContext* ctx, const cell_t* params)
        {
            menubuilder::MenuImpl* menu = MenuOf(ctx, params[1]);
            if (menu && (params[2] < 0 || params[2] >= menu->GetItemCount()))
            {
                ctx->ThrowNativeError("Invalid item %d (count: %d)", params[2], menu->GetItemCount());
                return nullptr;
            }
            return menu;
        }

        Source2ModMenuSelect SelectOf(IPluginFunction* fn, cell_t data)
        {
            if (!fn)
                return {};
            return [fn, data](CCSPlayerController* player, ISource2ModMenu* menu, int item)
            {
                sp::CallArgs args;
                args.PushCell(IndexOf(player));
                args.PushCell(HandleOf(menu));
                args.PushCell(item);
                args.PushCell(data);
                Invoke(fn, args);
            };
        }

        Source2ModMenuText TextOf(IPluginFunction* fn, cell_t data)
        {
            if (!fn)
                return {};
            return [fn, data](CCSPlayerController* player) -> std::string
            {
                char buffer[256] = {};
                sp::CallArgs args;
                args.PushCell(IndexOf(player));
                args.PushString(buffer, sizeof(buffer), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY | SM_PARAM_COPYBACK);
                args.PushCell(sizeof(buffer));
                args.PushCell(data);
                Invoke(fn, args);
                buffer[sizeof(buffer) - 1] = '\0';
                return buffer;
            };
        }

        Source2ModMenuCheck CheckOf(IPluginFunction* fn, cell_t data)
        {
            if (!fn)
                return {};
            return [fn, data](CCSPlayerController* player) -> bool
            {
                sp::CallArgs args;
                args.PushCell(IndexOf(player));
                args.PushCell(data);
                cell_t result = 0;
                return Invoke(fn, args, &result) && result;
            };
        }

        CCSPlayerController* PlayerArg(IPluginContext* ctx, cell_t index, bool allowNull)
        {
            if (allowNull && index < 0)
                return nullptr;
            auto* player = (index >= 0 && index < 32768) ? CBaseEntity::FromIndex<CCSPlayerController>(index) : nullptr;
            if (!player && !allowNull)
                ctx->ThrowNativeError("Player %d is invalid", index);
            return player;
        }

        cell_t N_New(IPluginContext* ctx, const cell_t* params)
        {
            const PluginId owner = CallerId(ctx);
            auto* menu = static_cast<menubuilder::MenuImpl*>(menubuilder::menuBuilderManager.CreateMenu(owner, LocalString(ctx, params[1])));
            auto* obj = new ScriptBuilderMenu();
            obj->id = menu->id;
            const cell_t h = Handles().Create(BuilderType(), obj, owner);
            if (!h)
            {
                delete obj;
                menu->Release();
                return ctx->ThrowNativeError("Handle limit reached");
            }
            g_handles[menu->id] = h;
            return h;
        }

        cell_t N_SetTitle(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* menu = MenuOf(ctx, params[1]))
                menu->SetTitle(LocalString(ctx, params[2]));
            return 0;
        }

        // SetDynamicTitle(title, data)
        cell_t N_SetDynamicTitle(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            IPluginFunction* fn;
            if (menu && OptionalFunction(ctx, params[2], fn))
                menu->SetDynamicTitle(TextOf(fn, params[0] >= 3 ? params[3] : 0));
            return 0;
        }

        // AddItem(text, onSelect, data, flags)
        cell_t N_AddItem(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            IPluginFunction* fn = nullptr;
            if (!menu || (params[0] >= 3 && !OptionalFunction(ctx, params[3], fn)))
                return -1;
            menu->AddItem(LocalString(ctx, params[2]), SelectOf(fn, params[0] >= 4 ? params[4] : 0), params[0] >= 5 ? params[5] : 0);
            return menu->GetItemCount() - 1;
        }

        // AddDynamicItem(text, onSelect, data, flags, enabled, visible)
        cell_t N_AddDynamicItem(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            IPluginFunction *text, *select = nullptr, *enabled = nullptr, *visible = nullptr;
            if (!menu || !OptionalFunction(ctx, params[2], text))
                return -1;
            if ((params[0] >= 3 && !OptionalFunction(ctx, params[3], select)) || (params[0] >= 6 && !OptionalFunction(ctx, params[6], enabled))
                || (params[0] >= 7 && !OptionalFunction(ctx, params[7], visible)))
                return -1;
            const cell_t data = params[0] >= 4 ? params[4] : 0;
            menu->AddDynamicItem(TextOf(text, data), SelectOf(select, data), params[0] >= 5 ? params[5] : 0, CheckOf(enabled, data), CheckOf(visible, data));
            return menu->GetItemCount() - 1;
        }

        // AddSubmenu(text, submenu, flags)
        cell_t N_AddSubmenu(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            auto* submenu = menu ? MenuOf(ctx, params[3]) : nullptr;
            if (!submenu)
                return -1;
            if (submenu == menu)
                return ctx->ThrowNativeError("A menu cannot be its own submenu");
            menu->AddSubmenu(LocalString(ctx, params[2]), submenu, params[0] >= 4 ? params[4] : 0);
            return menu->GetItemCount() - 1;
        }

        cell_t N_AddText(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            if (!menu)
                return -1;
            menu->AddText(LocalString(ctx, params[2]));
            return menu->GetItemCount() - 1;
        }

        cell_t N_ItemCount(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            return menu ? menu->GetItemCount() : 0;
        }

        cell_t N_SetItemText(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* menu = ItemOk(ctx, params))
                menu->SetItemText(params[2], LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_SetItemFlags(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* menu = ItemOk(ctx, params))
                menu->SetItemFlags(params[2], params[3]);
            return 0;
        }

        cell_t N_GetItemFlags(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = ItemOk(ctx, params);
            return menu ? menu->GetItemFlags(params[2]) : 0;
        }

        cell_t N_RemoveItem(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* menu = ItemOk(ctx, params))
                menu->RemoveItem(params[2]);
            return 0;
        }

        cell_t N_ClearItems(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* menu = MenuOf(ctx, params[1]))
                menu->ClearItems();
            return 0;
        }

        cell_t N_PerPageGet(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            return menu ? menu->itemsPerPage : 0;
        }

        cell_t N_PerPageSet(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* menu = MenuOf(ctx, params[1]))
                menu->SetItemsPerPage(params[2]);
            return 0;
        }

        cell_t N_ExitGet(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            return menu && menu->exitButton;
        }

        cell_t N_ExitSet(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* menu = MenuOf(ctx, params[1]))
                menu->SetExitButton(params[2] != 0);
            return 0;
        }

        cell_t N_PostSelectGet(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            return menu ? static_cast<cell_t>(menu->postSelect) : 0;
        }

        cell_t N_PostSelectSet(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            if (!menu)
                return 0;
            if (params[2] < static_cast<cell_t>(PostSelectAction::Close) || params[2] > static_cast<cell_t>(PostSelectAction::Nothing))
                return ctx->ThrowNativeError("Invalid PostSelectAction %d", params[2]);
            menu->SetPostSelect(static_cast<PostSelectAction>(params[2]));
            return 0;
        }

        cell_t N_TimeoutGet(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            return menu ? menu->timeout : 0;
        }

        cell_t N_TimeoutSet(IPluginContext* ctx, const cell_t* params)
        {
            if (auto* menu = MenuOf(ctx, params[1]))
                menu->SetTimeout(params[2]);
            return 0;
        }

        cell_t N_SetColors(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            if (!menu)
                return 0;
            const std::string title = LocalString(ctx, params[2]);
            const std::string enabled = LocalString(ctx, params[3]);
            const std::string disabled = LocalString(ctx, params[4]);
            menu->SetColors(title.c_str(), enabled.c_str(), disabled.c_str(), LocalString(ctx, params[5]));
            return 0;
        }

        // OnEnd(callback): (player, menu, reason)
        cell_t N_OnEnd(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            IPluginFunction* fn;
            if (!menu || !OptionalFunction(ctx, params[2], fn))
                return 0;
            if (!fn)
            {
                menu->OnEnd({});
                return 0;
            }
            menu->OnEnd([fn](CCSPlayerController* player, ISource2ModMenu* m, ESource2ModMenuEnd reason)
            {
                sp::CallArgs args;
                args.PushCell(IndexOf(player));
                args.PushCell(HandleOf(m));
                args.PushCell(reason);
                Invoke(fn, args);
            });
            return 0;
        }

        cell_t N_Display(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            CCSPlayerController* player = menu ? PlayerArg(ctx, params[2], false) : nullptr;
            if (player)
                menu->Display(player);
            return 0;
        }

        cell_t N_Close(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            if (!menu)
                return 0;
            const cell_t index = params[0] >= 2 ? params[2] : -1;
            CCSPlayerController* player = PlayerArg(ctx, index, true);
            if (index >= 0 && !player)
                return 0;
            menu->Close(player);
            return 0;
        }

        cell_t N_IsOpenFor(IPluginContext* ctx, const cell_t* params)
        {
            auto* menu = MenuOf(ctx, params[1]);
            CCSPlayerController* player = menu ? PlayerArg(ctx, params[2], false) : nullptr;
            return player && menu->IsOpenFor(player);
        }

        // GetOpenMenu(player): only this script's.
        cell_t N_GetOpenMenu(IPluginContext* ctx, const cell_t* params)
        {
            CCSPlayerController* player = PlayerArg(ctx, params[1], false);
            auto* open = player ? static_cast<menubuilder::MenuImpl*>(menubuilder::menuBuilderManager.GetOpenMenu(player)) : nullptr;
            if (!open || open->owner != CallerId(ctx))
                return 0;
            return HandleOf(open);
        }

        const sp_nativeinfo_t g_builderNatives[] = {
            { "Source2ModMenu.Source2ModMenu", N_New },
            { "Source2ModMenu.SetTitle", N_SetTitle },
            { "Source2ModMenu.SetDynamicTitle", N_SetDynamicTitle },
            { "Source2ModMenu.AddItem", N_AddItem },
            { "Source2ModMenu.AddDynamicItem", N_AddDynamicItem },
            { "Source2ModMenu.AddSubmenu", N_AddSubmenu },
            { "Source2ModMenu.AddText", N_AddText },
            { "Source2ModMenu.ItemCount.get", N_ItemCount },
            { "Source2ModMenu.SetItemText", N_SetItemText },
            { "Source2ModMenu.SetItemFlags", N_SetItemFlags },
            { "Source2ModMenu.GetItemFlags", N_GetItemFlags },
            { "Source2ModMenu.RemoveItem", N_RemoveItem },
            { "Source2ModMenu.ClearItems", N_ClearItems },
            { "Source2ModMenu.ItemsPerPage.get", N_PerPageGet },
            { "Source2ModMenu.ItemsPerPage.set", N_PerPageSet },
            { "Source2ModMenu.ExitButton.get", N_ExitGet },
            { "Source2ModMenu.ExitButton.set", N_ExitSet },
            { "Source2ModMenu.PostSelect.get", N_PostSelectGet },
            { "Source2ModMenu.PostSelect.set", N_PostSelectSet },
            { "Source2ModMenu.Timeout.get", N_TimeoutGet },
            { "Source2ModMenu.Timeout.set", N_TimeoutSet },
            { "Source2ModMenu.SetColors", N_SetColors },
            { "Source2ModMenu.OnEnd", N_OnEnd },
            { "Source2ModMenu.Display", N_Display },
            { "Source2ModMenu.Close", N_Close },
            { "Source2ModMenu.IsOpenFor", N_IsOpenFor },
            { "Source2ModMenu.GetOpenMenu", N_GetOpenMenu },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetMenuBuilderNatives()
    {
        return g_builderNatives;
    }
}
