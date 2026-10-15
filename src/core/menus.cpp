/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "menus.h"

#include "slowguard.h"

#include <source2mod/schema/entity/classes/CCSCustomHudLayout.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace menus
{
    MenuManager menuManager;

    namespace
    {
        constexpr int kMaxPerPage = 6;   // 7, 8, 9 are Back, Next, Exit

        std::string Escape(const std::string& text)
        {
            std::string out;
            out.reserve(text.size());
            for (char c : text)
            {
                if (c == '<')
                    out += "&lt;";
                else if (c == '>')
                    out += "&gt;";
                else
                    out += c;
            }
            return out;
        }
    }

    // ---- CenterHtmlMenuInstance -----------------------------------------------------------
    //
    // Every evaluator and handler is plugin code and may close this menu, open
    // another one or rebuild the options; after each call the instance checks
    // it is still the open one (IsOpen) before touching anything of itself,
    // and it copies an option before calling into it.

    CenterHtmlMenuInstance::CenterHtmlMenuInstance(CCSPlayerController *player, CenterHtmlMenu *menu, uint64_t serial)
        : IMenuInstance(player, menu), chMenu_(menu), serial_(serial), opened_(std::chrono::steady_clock::now())
    {
        path_.push_back({ menu, 0 });
        menuManager.ownInstances_.insert(this);
    }

    CenterHtmlMenuInstance::~CenterHtmlMenuInstance()
    {
        menuManager.ownInstances_.erase(this);
    }

    bool CenterHtmlMenuInstance::Uses(const IMenu* menu) const
    {
        return std::any_of(path_.begin(), path_.end(), [menu](const Level& l) { return l.menu == menu; });
    }

    void CenterHtmlMenuInstance::Reset()
    {
        IMenuInstance::Reset();
        path_.resize(1);
        path_[0].page = 0;
    }

    void CenterHtmlMenuInstance::OnEnd(MenuEndReason reason)
    {
        if (chMenu_)
            chMenu_->OnMenuEnd(player_, reason);
    }

    void CenterHtmlMenuInstance::End(MenuEndReason reason)
    {
        CCSPlayerController* player = player_;
        if (!player)
            return;
        // Destroys this instance.
        menuManager.CloseWith(player->GetSlot(), reason);
        player->PrintToCenterHtml(" ", 3, true);
    }

    void CenterHtmlMenuInstance::Close()
    {
        End(MenuEndReason::Closed);
    }

    void CenterHtmlMenuInstance::OnFrame()
    {
        const int timeout = chMenu_->GetTimeout();
        if (timeout > 0 && std::chrono::steady_clock::now() - opened_ >= std::chrono::seconds(timeout))
        {
            End(MenuEndReason::Timeout);
            return;
        }
        Display();
    }

    bool CenterHtmlMenuInstance::Enabled(const ChatMenuOption& option, bool& alive)
    {
        alive = true;
        if (option.Disabled || option.IsText)
            return false;
        if (!option.DisabledEvaluator)
            return true;
        CCSPlayerController* player = player_;
        const uint64_t serial = serial_;
        const auto evaluator = option.DisabledEvaluator;
        const bool disabled = evaluator(player);
        alive = menuManager.IsOpen(player, serial);
        return !disabled;
    }

    bool CenterHtmlMenuInstance::PageItems(std::vector<int>& items, int& pages)
    {
        CCSPlayerController* player = player_;
        const uint64_t serial = serial_;
        IBaseMenu* menu = Current();

        std::vector<int> visible;
        for (int i = 0; i < static_cast<int>(menu->Options().size()); i++)
        {
            const ChatMenuOption& opt = menu->Options()[i];
            if (opt.Hidden)
                continue;
            if (opt.VisibleEvaluator)
            {
                const auto evaluator = opt.VisibleEvaluator;
                const bool show = evaluator(player);
                if (!menuManager.IsOpen(player, serial))
                    return false;
                if (!show)
                    continue;
            }
            visible.push_back(i);
        }

        // Pages of up to ItemsPerPage numbered options; text lines ride along.
        const int perPage = std::clamp(menu->GetItemsPerPage(), 1, kMaxPerPage);
        std::vector<std::vector<int>> split(1);
        int numbered = 0;
        for (int i : visible)
        {
            const bool isText = menu->Options()[i].IsText;
            if (!isText && numbered == perPage)
            {
                split.emplace_back();
                numbered = 0;
            }
            split.back().push_back(isText ? -1 - i : i);
            if (!isText)
                numbered++;
        }

        pages = static_cast<int>(split.size());
        Level& level = path_.back();
        level.page = std::clamp(level.page, 0, pages - 1);
        items = split[level.page];
        return true;
    }

    void CenterHtmlMenuInstance::Display()
    {
        if (!player_ || !menu_) return;

        if (menuManager.GetActiveMenu(player_) != this)
        {
            Reset();
            return;
        }

        CCSPlayerController* player = player_;
        const uint64_t serial = serial_;
        IBaseMenu* menu = Current();

        const std::string title = menu->DisplayTitle(player);
        if (!menuManager.IsOpen(player, serial))
            return;

        std::vector<int> items;
        int pages = 1;
        if (!PageItems(items, pages))
            return;

        std::string html;
        html.reserve(512);
        html += "<b><font color='" + chMenu_->TitleColor + "'>" + Escape(title) + "</font></b><br>\n";

        int key = 1;
        for (int entry : items)
        {
            const bool isText = entry < 0;
            const int index = isText ? -1 - entry : entry;
            if (index >= static_cast<int>(menu->Options().size()))
                continue;
            const ChatMenuOption opt = menu->Options()[index];

            std::string text = opt.Text;
            if (opt.TextEvaluator)
            {
                text = opt.TextEvaluator(player);
                if (!menuManager.IsOpen(player, serial))
                    return;
            }
            if (isText)
            {
                html += "<font color='" + chMenu_->DisabledColor + "'>" + Escape(text) + "</font><br>\n";
                continue;
            }

            bool alive;
            const bool enabled = Enabled(opt, alive);
            if (!alive)
                return;
            const std::string& color = enabled ? chMenu_->EnabledColor : chMenu_->DisabledColor;
            html += "<font color='" + color + "'>!" + std::to_string(key++) + "</font> " + Escape(text);
            if (opt.Submenu)
                html += " &gt;";
            html += "<br>\n";
        }

        const Level& level = path_.back();
        if (level.page > 0 || path_.size() > 1)
            html += "<font color='" + chMenu_->PrevPageColor + "'>!7</font> &lt;- Back<br>\n";
        if (level.page < pages - 1)
            html += "<font color='" + chMenu_->NextPageColor + "'>!8</font> -&gt; Next<br>\n";
        if (chMenu_->GetExitButton())
            html += "<font color='" + chMenu_->CloseColor + "'>!9</font> -&gt; Close<br>\n";

        player->PrintToCenterHtml(html.c_str(), 3, true);
    }

    void CenterHtmlMenuInstance::OnKeyPress(CCSPlayerController* p, int key)
    {
        if (p != player_) return;
        if (!menu_) return;

        CCSPlayerController* player = player_;
        const uint64_t serial = serial_;

        std::vector<int> items;
        int pages = 1;
        if (!PageItems(items, pages))
            return;
        Level& level = path_.back();

        if (key == 7 && (level.page > 0 || path_.size() > 1))
        {
            if (level.page > 0)
                level.page--;
            else
                path_.pop_back();
            Display();
            return;
        }
        if (key == 8 && level.page < pages - 1)
        {
            level.page++;
            Display();
            return;
        }
        if (key == 9 && chMenu_->GetExitButton())
        {
            End(MenuEndReason::Exit);
            return;
        }
        if (key < 1 || key > kMaxPerPage)
            return;

        // The key-th numbered option of the page: only what Display() showed.
        int index = -1;
        int numbered = 0;
        for (int entry : items)
        {
            if (entry >= 0 && ++numbered == key)
            {
                index = entry;
                break;
            }
        }
        IBaseMenu* menu = Current();
        if (index < 0 || index >= static_cast<int>(menu->Options().size()))
            return;

        bool alive;
        if (!Enabled(menu->Options()[index], alive) || !alive)
            return;

        // Re-read after the evaluator: it may have rebuilt the options.
        if (index >= static_cast<int>(menu->Options().size()))
            return;
        if (IBaseMenu* submenu = menu->Options()[index].Submenu)
        {
            path_.push_back({ submenu, 0 });
            Display();
            return;
        }

        // Through a copy: a handler that rebuilds this same menu in place
        // (ClearOptions + AddMenuOption) destroys the option it is running from.
        ChatMenuOption opt = menu->Options()[index];
        {
            ::slow::Guard slowGuard("menu option", opt.Text.c_str(), menuManager.OwnerOf(player));
            menu->OnOptionSelected(player, opt, index);
        }

        if (!menuManager.IsOpen(player, serial))
            return;

        if (opt.CloseOnSelect)
        {
            End(MenuEndReason::Selected);
            return;
        }
        if (!opt.StayOnPage)
        {
            switch (Current()->GetPostSelectAction())
            {
            case PostSelectAction::Close:
                End(MenuEndReason::Selected);
                return;
            case PostSelectAction::Reset:
                path_.back().page = 0;
                break;
            case PostSelectAction::Nothing:
            default:
                break;
            }
        }
        Display();
    }

    // ---- MenuManager ------------------------------------------------------------------------

    PluginId MenuManager::OwnerOf(CCSPlayerController* player) const
    {
        if (!player) return 0;

        auto it = activeMenus.find(player->GetSlot());
        return it == activeMenus.end() ? 0 : it->second.owner;
    }

    void MenuManager::CloseWith(int slot, MenuEndReason reason, bool notify)
    {
        auto it = activeMenus.find(slot);
        if (it == activeMenus.end())
            return;

        std::unique_ptr<IMenuInstance, InstanceDeleter> instance = std::move(it->second.instance);
        activeMenus.erase(it);
        if (!instance)
            return;

        instance->Reset();
        if (notify)
            instance->OnEnd(reason);
        // instance is Destroy()ed here.
    }

    void MenuManager::OnClientDisconnect(CPlayerSlot slot)
    {
        CloseWith(slot.Get(), MenuEndReason::Disconnected);
    }

    void MenuManager::Install(PluginId owner, CCSPlayerController* player, IMenuInstance* instance, uint64_t serial)
    {
        const int slot = player->GetSlot();

        // What OnMenuEnd(Replaced) opens is replaced in turn; give up after a
        // few rounds rather than let two plugins reopen each other forever.
        for (int round = 0; round < 4 && activeMenus.count(slot); round++)
            CloseWith(slot, MenuEndReason::Replaced);
        CloseWith(slot, MenuEndReason::Replaced, false);

        auto& active = activeMenus[slot];
        active.owner = owner;
        active.serial = serial;
        active.instance.reset(instance);
        active.instance->Display();
    }

    void MenuManager::OpenCenterHtmlMenu(PluginId owner, CCSPlayerController* player, CenterHtmlMenu* menu)
    {
        if (!player || !menu) return;

        const uint64_t serial = ++nextSerial_;
        Install(owner, player, new CenterHtmlMenuInstance(player, menu, serial), serial);
    }

    void MenuManager::OpenMenu(PluginId owner, CCSPlayerController* player, IMenuInstance* instance)
    {
        if (!player || !instance) return;

        Install(owner, player, instance, ++nextSerial_);
    }

    bool MenuManager::IsOpen(CCSPlayerController* player, uint64_t serial) const
    {
        if (!player) return false;

        auto it = activeMenus.find(player->GetSlot());
        return it != activeMenus.end() && it->second.instance && it->second.serial == serial;
    }

    IMenuInstance* MenuManager::GetActiveMenu(CCSPlayerController* player)
    {
        if (!player) return nullptr;

        auto it = activeMenus.find(player->GetSlot());
        return (it == activeMenus.end()) ? nullptr : it->second.instance.get();
    }

    void MenuManager::CloseActiveMenu(CCSPlayerController* player)
    {
        if (!player) return;

        CloseWith(player->GetSlot(), MenuEndReason::Closed);
    }

    void MenuManager::CloseMenu(IMenu* menu)
    {
        if (!menu) return;

        std::vector<int> slots;
        for (const auto& [slot, active] : activeMenus)
        {
            const IMenuInstance* inst = active.instance.get();
            if (!inst)
                continue;
            const bool uses = ownInstances_.count(inst) ? static_cast<const CenterHtmlMenuInstance*>(inst)->Uses(menu)
                                                        : inst->Menu() == menu;
            if (uses)
                slots.push_back(slot);
        }
        for (int slot : slots)
        {
            auto it = activeMenus.find(slot);
            CCSPlayerController* player = (it != activeMenus.end() && it->second.instance) ? it->second.instance->Player() : nullptr;
            CloseWith(slot, MenuEndReason::Closed);
            if (player)
                player->PrintToCenterHtml(" ", 3, true);
        }
    }

    bool MenuManager::IsMenuOpen(CCSPlayerController* player, IMenu* menu)
    {
        IMenuInstance* inst = GetActiveMenu(player);
        if (!inst || !menu)
            return false;
        return ownInstances_.count(inst) ? static_cast<CenterHtmlMenuInstance*>(inst)->Uses(menu) : inst->Menu() == menu;
    }

    IMenu* MenuManager::GetOpenMenu(CCSPlayerController* player)
    {
        IMenuInstance* inst = GetActiveMenu(player);
        if (!inst)
            return nullptr;
        return ownInstances_.count(inst) ? static_cast<CenterHtmlMenuInstance*>(inst)->Current() : inst->Menu();
    }

    void MenuManager::OnKeyPress(CCSPlayerController* player, int key)
    {
        if (!player) return;

        auto it = activeMenus.find(player->GetSlot());
        if (it == activeMenus.end() || !it->second.instance)
            return;

        // A menu the player clicks (a HUD menu with the mouse) is not driven
        // by the keys: a "3" typed or bound while it is open is not a pick.
        if (!it->second.instance->AcceptsKeys())
            return;

        it->second.instance->OnKeyPress(player, key);
    }

    void MenuManager::Tick()
    {
        // Display() runs the plugins' evaluators, which may open or close
        // menus and so change activeMenus under a live iterator. Walk a
        // snapshot and skip whatever was closed or replaced meanwhile.
        tickSnapshot_.clear();
        for (const auto& kv : activeMenus)
            tickSnapshot_.emplace_back(kv.first, kv.second.serial);

        for (const auto& [slot, serial] : tickSnapshot_)
        {
            auto it = activeMenus.find(slot);
            if (it == activeMenus.end() || it->second.serial != serial || !it->second.instance)
                continue;

            it->second.instance->OnFrame();
        }
    }

    void MenuManager::RemoveAllForPlugin(PluginId id)
    {
        std::vector<int> slots;
        for (const auto& [slot, active] : activeMenus)
        {
            if (active.owner == id)
                slots.push_back(slot);
        }

        // Without OnMenuEnd: the plugin is going away. Clears the panel the
        // player is looking at, the same as closing it by hand would.
        for (int slot : slots)
        {
            auto it = activeMenus.find(slot);
            CCSPlayerController* player = (it != activeMenus.end() && it->second.instance) ? it->second.instance->Player() : nullptr;
            CloseWith(slot, MenuEndReason::Closed, false);
            if (player)
                player->PrintToCenterHtml(" ", 3, true);
        }
    }
}
