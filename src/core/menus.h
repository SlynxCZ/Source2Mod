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
#pragma once
#include <playerslot.h>
#include <source2mod/ISource2ModMenus.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace menus {
    // AddMenuOptionWithCooldown lives in IBaseMenu now, so the local
    // BaseMenu that used to supply it is gone.

    class CenterHtmlMenuInstance : public IMenuInstance
    {
    public:
        CenterHtmlMenuInstance(CCSPlayerController *player, CenterHtmlMenu *menu, uint64_t serial)
            : IMenuInstance(player, menu), chMenu_(menu), serial_(serial)
        {
        }

        void Display() override;
        void OnKeyPress(CCSPlayerController* player, int key) override;
        void Close() override;

    protected:
        int NumPerPage() const override { return 5; }

        int MenuItemsPerPage() const override
        {
            const bool exit = HasExitButton();
            const bool both = HasPrevButton() && HasNextButton();
            return (exit ? 0 : 1) + (both ? (NumPerPage() - 1) : NumPerPage());
        }

        // Hides IMenuInstance::HasNextButton, which counts NumPerPage() items
        // per page and misses the extra slot a menu without an exit button
        // gets in MenuItemsPerPage(). With exactly six options left it showed
        // all six plus a Next that led to an empty page. Kept out of the SDK
        // header so IMenuInstance stays as plugins were built against it.
        bool HasNextButton() const
        {
            const int remaining = static_cast<int>(menu_->Options().size()) - currentOffset_;
            return remaining > (HasExitButton() ? 0 : 1) + NumPerPage();
        }

    private:
        CenterHtmlMenu *chMenu_;

        /// Which opening of a menu this is, see MenuManager::IsOpen.
        uint64_t serial_;
    };

    class MenuManager : public ISource2ModMenus
    {
    public:
        void OpenCenterHtmlMenu(PluginId owner, CCSPlayerController *player, CenterHtmlMenu *menu) override;
        IMenuInstance *GetActiveMenu(CCSPlayerController *player) override;
        void CloseActiveMenu(CCSPlayerController *player) override;
        void OnKeyPress(CCSPlayerController *player, int key) override;
        void OpenMenu(PluginId owner, CCSPlayerController* player, IMenuInstance* instance) override;
    public:
        /// The plugin whose menu the player has open, 0 for none.
        PluginId OwnerOf(CCSPlayerController* player) const;

        void Tick();

        /// Closes the player's menu while the controller still exists, so a
        /// HUD menu can hide itself and the next occupant of the slot does
        /// not inherit it.
        void OnClientDisconnect(CPlayerSlot slot);

        /// Closes whatever this plugin still has open. The menu object and the
        /// option handlers behind it live inside its library, so a menu left
        /// on someone's screen is a key press away from a closed library.
        void RemoveAllForPlugin(PluginId id);

        /// Whether the menu opened under this serial is still the one the
        /// player has open. An option handler can close its menu or open
        /// another one, which destroys the instance it was called from, and
        /// the next instance often gets the same address back from the
        /// allocator, so comparing pointers is not enough.
        bool IsOpen(CCSPlayerController *player, uint64_t serial) const;

    protected:
        /// An instance is freed through IMenuInstance::Destroy(), so one
        /// another library built (OpenMenu) is deleted where it was allocated.
        struct InstanceDeleter
        {
            void operator()(IMenuInstance* instance) const { if (instance) instance->Destroy(); }
        };

        struct ActiveMenu
        {
            PluginId owner = 0;
            uint64_t serial = 0;
            std::unique_ptr<IMenuInstance, InstanceDeleter> instance;
        };

        std::unordered_map<int, ActiveMenu> activeMenus;
        uint64_t nextSerial_ = 0;

        /// (slot, serial) pairs Tick() walks, kept to avoid a per-frame allocation.
        std::vector<std::pair<int, uint64_t>> tickSnapshot_;
    };

    extern MenuManager menuManager;
}
