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

    void CenterHtmlMenuInstance::Display()
    {
        if (!player_ || !menu_) return;

        if (menuManager.GetActiveMenu(player_) != this)
        {
            Reset();
            return;
        }

        // The evaluators are plugin code and may close this menu or open
        // another one for the player, which destroys this instance.
        CCSPlayerController* player = player_;
        const uint64_t serial = serial_;

        auto& options = menu_->Options();
        for (size_t i = 0; i < options.size(); ++i)
        {
            auto& opt = options[i];
            if (!opt.DisabledEvaluator) continue;

            opt.Disabled = opt.DisabledEvaluator();
            if (!menuManager.IsOpen(player, serial)) return;
        }

        const auto& opts = menu_->Options();
        const int total = static_cast<int>(opts.size());

        std::string html;
        html.reserve(512);
        html += "<b><font color='";
        html += chMenu_->TitleColor;
        html += "'>";
        html += chMenu_->Title();
        html += "</font></b><br>\n";

        int keyOffset = 1;
        const int pageEnd = (std::min)(currentOffset_ + MenuItemsPerPage(), total);
        for (int i = currentOffset_; i < pageEnd; ++i)
        {
            const auto& opt = opts[i];
            const std::string& color = opt.Disabled ? chMenu_->DisabledColor : chMenu_->EnabledColor;
            html += "<font color='";
            html += color;
            html += "'>!";
            html += std::to_string(keyOffset++);
            html += "</font> ";
            html += opt.Text;
            html += "<br>\n";
        }

        if (HasPrevButton())
        {
            html += "<font color='";
            html += chMenu_->PrevPageColor;
            html += "'>!7</font> &#60;- Prev<br>\n";
        }
        if (HasNextButton())
        {
            html += "<font color='";
            html += chMenu_->NextPageColor;
            html += "'>!8</font> -> Next<br>\n";
        }
        if (HasExitButton())
        {
            html += "<font color='";
            html += chMenu_->CloseColor;
            html += "'>!9</font> -> Close<br>\n";
        }

        player_->PrintToCenterHtml(html.c_str(), 3, true);
    }

    void CenterHtmlMenuInstance::OnKeyPress(CCSPlayerController* p, int key)
    {
        if (p != player_) return;
        if (!menu_) return;

        // 8 = Next, 7 = Prev, 9 = Close (matches C#)
        if (key == 8 && HasNextButton())
        {
            NextPage();
            return;
        }
        if (key == 7 && HasPrevButton())
        {
            PrevPage();
            return;
        }
        if (key == 9 && HasExitButton())
        {
            Close();
            return;
        }

        // Item keys are only the ones Display() printed for this page. C#
        // accepts 1..9 here, so !6, or !7 without a Prev button, picked an
        // option from the next page that the player never saw.
        if (key < 1 || key > MenuItemsPerPage()) return;

        const int idx = currentOffset_ + (key - 1);
        auto& options = menu_->Options();
        if (idx < 0 || idx >= (int)options.size()) return;

        auto& opt = options[idx];
        if (opt.Disabled || !opt.OnSelect) return;

        // The handler may close this menu or open another one for the player
        // (a submenu), and either destroys this instance. Nothing of `this`
        // may be touched after the call unless it is still the open menu.
        CCSPlayerController* player = player_;
        const uint64_t serial = serial_;

        // Called through a copy: a handler that rebuilds this same menu in
        // place (ClearOptions + AddMenuOption) destroys the std::function it
        // is running from.
        const auto onSelect = opt.OnSelect;
        {
            ::slow::Guard slowGuard("menu option", opt.Text.c_str(), menuManager.OwnerOf(player));
            onSelect(player, opt);
        }

        if (!menuManager.IsOpen(player, serial))
            return;

        // Apply PostSelectAction just like CSSharp BaseMenuInstance
        switch (menu_->GetPostSelectAction())
        {
        case PostSelectAction::Close:
            Close();
            break;
        case PostSelectAction::Reset:
            while (!prevPageOffsets_.empty()) prevPageOffsets_.pop();
            page_ = 0;
            currentOffset_ = 0;
            break;
        case PostSelectAction::Nothing:
        default:
            break;
        }
    }

    void CenterHtmlMenuInstance::Close()
    {
        // CloseActiveMenu destroys this instance.
        CCSPlayerController* player = player_;
        menuManager.CloseActiveMenu(player);
        if (player)
        {
            player->PrintToCenterHtml(" ", 3, true);
        }
    }


    PluginId MenuManager::OwnerOf(CCSPlayerController* player) const
    {
        if (!player) return 0;

        auto it = activeMenus.find(player->GetSlot());
        return it == activeMenus.end() ? 0 : it->second.owner;
    }

    void MenuManager::OnClientDisconnect(CPlayerSlot slot)
    {
        auto it = activeMenus.find(slot.Get());
        if (it != activeMenus.end())
            activeMenus.erase(it);
    }

    void MenuManager::OpenCenterHtmlMenu(PluginId owner, CCSPlayerController* player, CenterHtmlMenu* menu)
    {
        if (!player || !menu) return;
        CloseActiveMenu(player);

        const uint64_t serial = ++nextSerial_;

        auto& active = activeMenus[player->GetSlot()];
        active.owner = owner;
        active.serial = serial;
        active.instance.reset(new CenterHtmlMenuInstance(player, menu, serial));
        active.instance->Display();
    }

    void MenuManager::OpenMenu(PluginId owner, CCSPlayerController* player, IMenuInstance* instance)
    {
        if (!player || !instance) return;
        CloseActiveMenu(player);

        auto& active = activeMenus[player->GetSlot()];
        active.owner = owner;
        active.serial = ++nextSerial_;
        active.instance.reset(instance);
        active.instance->Display();
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

        auto it = activeMenus.find(player->GetSlot());
        if (it != activeMenus.end())
        {
            it->second.instance->Reset();
            activeMenus.erase(it);
        }
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
        // Display() runs the plugins' DisabledEvaluators, which may open or
        // close menus and so change activeMenus under a live iterator. Walk a
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
        for (auto it = activeMenus.begin(); it != activeMenus.end(); )
        {
            if (it->second.owner != id)
            {
                ++it;
                continue;
            }

            // Clears the panel the player is looking at, the same as closing
            // it by hand would.
            if (it->second.instance)
                it->second.instance->Reset();

            it = activeMenus.erase(it);
        }
    }
}
