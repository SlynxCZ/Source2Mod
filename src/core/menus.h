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

#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace menus {
    // AddMenuOptionWithCooldown lives in IBaseMenu now, so the local
    // BaseMenu that used to supply it is gone.

    /// One player's view of a CenterHtmlMenu: the submenu path and the page
    /// in each. Keys: the numbered options of the page, 7 Back (a previous
    /// page, else the parent menu), 8 Next, 9 Exit.
    class CenterHtmlMenuInstance : public IMenuInstance
    {
    public:
        CenterHtmlMenuInstance(CCSPlayerController *player, CenterHtmlMenu *menu, uint64_t serial);
        ~CenterHtmlMenuInstance() override;

        void Display() override;
        void OnKeyPress(CCSPlayerController* player, int key) override;
        void Close() override;
        void OnFrame() override;
        void OnEnd(MenuEndReason reason) override;
        /// Back to the root menu's first page.
        void Reset() override;

        /// The menu the player is looking at (the submenu they are in).
        IBaseMenu* Current() const { return path_.back().menu; }
        /// Whether @p menu is the root or on the submenu path.
        bool Uses(const IMenu* menu) const;
        /// The keys the page last drawn does something with (bit = key):
        /// its numbered options, 7 Back, 8 Next, 9 Exit.
        uint32_t KeyMask() const override { return keyMask_; }

    private:
        struct Level
        {
            IBaseMenu* menu;
            int page;
        };

        /// The options on the current page by index; text lines as -1 - index.
        /// False when a plugin callback closed the menu meanwhile.
        bool PageItems(std::vector<int>& items, int& pages);
        bool Enabled(const ChatMenuOption& option, bool& alive);
        /// Closes it (destroying this) with a reason for OnMenuEnd.
        void End(MenuEndReason reason);

        CenterHtmlMenu *chMenu_;

        /// Which opening of a menu this is, see MenuManager::IsOpen.
        uint64_t serial_;

        std::vector<Level> path_;
        std::chrono::steady_clock::time_point opened_;
        uint32_t keyMask_ = 0;
    };

    class MenuManager : public ISource2ModMenus
    {
    public:
        void OpenCenterHtmlMenu(PluginId owner, CCSPlayerController *player, CenterHtmlMenu *menu) override;
        IMenuInstance *GetActiveMenu(CCSPlayerController *player) override;
        void CloseActiveMenu(CCSPlayerController *player) override;
        void OnKeyPress(CCSPlayerController *player, int key) override;
        void OpenMenu(PluginId owner, CCSPlayerController* player, IMenuInstance* instance) override;
        void CloseMenu(IMenu* menu) override;
        bool IsMenuOpen(CCSPlayerController* player, IMenu* menu) override;
        IMenu* GetOpenMenu(CCSPlayerController* player) override;
    public:
        /// The plugin whose menu the player has open, 0 for none.
        PluginId OwnerOf(CCSPlayerController* player) const;

        /// The keys the player's open menu does something with right now
        /// (bit = key, IMenuInstance::KeyMask), 0 for no menu or one that
        /// takes no keys.
        uint32_t ActiveKeyMask(CCSPlayerController* player);

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

        /// Closes the player's menu: takes it out first, then tells it why
        /// (unless @p notify is false), then destroys it -- so whatever the
        /// plugin does in OnMenuEnd, opening another menu included, finds the
        /// bookkeeping already settled.
        void CloseWith(int slot, MenuEndReason reason, bool notify = true);

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

        /// Puts @p instance in as the player's menu, closing (Replaced) what was there.
        void Install(PluginId owner, CCSPlayerController* player, IMenuInstance* instance, uint64_t serial);

        std::unordered_map<int, ActiveMenu> activeMenus;
        uint64_t nextSerial_ = 0;

        /// The core's own instances, to tell them from ones other libraries built.
        std::unordered_set<const IMenuInstance*> ownInstances_;

        /// (slot, serial) pairs Tick() walks, kept to avoid a per-frame allocation.
        std::vector<std::pair<int, uint64_t>> tickSnapshot_;

        friend class CenterHtmlMenuInstance;
    };

    extern MenuManager menuManager;
}
