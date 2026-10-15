/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Menus built item by item (ISource2ModMenuBuilder): pages, submenus with a
 * way back, items worked out per player, shown as the center HTML menu and
 * picked with the slot keys through the menu manager. The design follows
 * SwiftlyS2's menu builder; the code is Source2Mod's own.
 *
 * Keys: the numbered items of the page, then 7 Back (a previous page, else
 * the parent menu), 8 Next, 9 Exit -- so at most 6 items a page.
 *
 * Project: Source2Mod
 */
#pragma once

#include <source2mod/ISource2ModMenuBuilder.h>
#include <source2mod/ISource2ModMenus.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace menubuilder
{
    class MenuImpl;

    /// One player's view of a builder menu: the submenu path, the page.
    class BuilderInstance final : public IMenuInstance
    {
    public:
        BuilderInstance(CCSPlayerController* player, MenuImpl* root);
        ~BuilderInstance() override;

        void Display() override;
        void OnKeyPress(CCSPlayerController* player, int key) override;
        void Close() override;
        void OnFrame() override;

        MenuImpl* Root() const { return m_root; }
        MenuImpl* Current() const { return m_path.back().menu; }
        bool Uses(const MenuImpl* menu) const;

        /// Closes it (the manager destroys it) and then tells the root's OnEnd why.
        void End(ESource2ModMenuEnd reason);

        uint64_t serial = 0;

    private:
        struct Level
        {
            MenuImpl* menu;
            int page;
        };

        /// The items shown on the current page, by index in the current menu;
        /// -1 for a text line. Empty and false when a callback closed the menu.
        bool PageItems(std::vector<int>& items, int& pages);

        MenuImpl* m_root;
        std::vector<Level> m_path;
        int m_slot;
        std::chrono::steady_clock::time_point m_opened;
        bool m_ending = false;
    };

    class MenuImpl final : public ISource2ModMenu
    {
    public:
        MenuImpl(PluginId owner, const char* title);

        ISource2ModMenu* SetTitle(const char* title) override;
        ISource2ModMenu* SetDynamicTitle(Source2ModMenuText title) override;
        ISource2ModMenu* AddItem(const char* text, Source2ModMenuSelect onSelect, int flags = 0) override;
        ISource2ModMenu* AddDynamicItem(Source2ModMenuText text, Source2ModMenuSelect onSelect, int flags = 0,
                                        Source2ModMenuCheck enabled = {}, Source2ModMenuCheck visible = {}) override;
        ISource2ModMenu* AddSubmenu(const char* text, ISource2ModMenu* submenu, int flags = 0) override;
        ISource2ModMenu* AddText(const char* text) override;
        int GetItemCount() override { return static_cast<int>(items.size()); }
        void SetItemText(int item, const char* text) override;
        void SetItemFlags(int item, int flags) override;
        int GetItemFlags(int item) override;
        void RemoveItem(int item) override;
        void ClearItems() override;
        ISource2ModMenu* SetItemsPerPage(int count) override;
        ISource2ModMenu* SetExitButton(bool exit) override { exitButton = exit; return this; }
        ISource2ModMenu* SetPostSelect(PostSelectAction action) override { postSelect = action; return this; }
        ISource2ModMenu* SetTimeout(int seconds) override { timeout = seconds > 0 ? seconds : 0; return this; }
        ISource2ModMenu* SetColors(const char* title, const char* enabled, const char* disabled, const char* navigation) override;
        ISource2ModMenu* OnEnd(Source2ModMenuEnd onEnd) override { this->onEnd = std::move(onEnd); return this; }
        void Display(CCSPlayerController* player) override;
        void Close(CCSPlayerController* player = nullptr) override;
        bool IsOpenFor(CCSPlayerController* player) override;
        void SetData(void* data) override { this->data = data; }
        void* GetData() override { return data; }
        void Release() override;

        struct Item
        {
            enum Kind { Normal, Submenu, Text } kind = Normal;
            std::string text;
            Source2ModMenuText dynamicText;
            Source2ModMenuSelect onSelect;
            Source2ModMenuCheck enabled;
            Source2ModMenuCheck visible;
            MenuImpl* submenu = nullptr;
            int flags = 0;
        };

        PluginId owner;
        uint64_t id;                 // never reused: deferred work looks menus up by it
        std::string title;
        Source2ModMenuText dynamicTitle;
        std::vector<Item> items;
        int itemsPerPage = 5;
        bool exitButton = true;
        PostSelectAction postSelect = PostSelectAction::Reset;
        int timeout = 0;
        std::string titleColor = "#FFD700";
        std::string enabledColor = "#FFFFFF";
        std::string disabledColor = "#808080";
        std::string navigationColor = "#00BFFF";
        Source2ModMenuEnd onEnd;
        void* data = nullptr;

        /// What IMenuInstance::Menu() hands out for this menu (its title only).
        IBaseMenu shadow;
    };

    class MenuBuilderManager final : public ISource2ModMenuBuilder
    {
    public:
        ISource2ModMenu* CreateMenu(PluginId owner, const char* title) override;
        ISource2ModMenu* GetOpenMenu(CCSPlayerController* player) override;

        /// The builder instance @p player has open, or nullptr.
        BuilderInstance* OpenInstance(CCSPlayerController* player);
        /// A live menu by id; nullptr once released.
        MenuImpl* Find(uint64_t id);
        void Forget(MenuImpl* menu);
        void RemoveAllForPlugin(PluginId id);

        std::vector<MenuImpl*> menus;
        std::unordered_set<BuilderInstance*> instances;
        uint64_t nextId = 1;
    };

    extern MenuBuilderManager menuBuilderManager;
}
