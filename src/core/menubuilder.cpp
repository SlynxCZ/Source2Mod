/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Menus built item by item (ISource2ModMenuBuilder). See menubuilder.h.
 *
 * Every callback here is plugin code and may close the menu, open another
 * one or release menus; after each, the instance checks it is still the open
 * one (Alive) before touching anything of itself.
 *
 * Project: Source2Mod
 */
#include "menubuilder.h"

#include "clients.h"
#include "menus.h"
#include "scheduler.h"
#include "slowguard.h"

#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <algorithm>

namespace menubuilder
{
    MenuBuilderManager menuBuilderManager;

    namespace
    {
        constexpr int kMaxPerPage = 6;   // 7, 8, 9 are Back, Next, Exit
        uint64_t g_nextSerial = 0;

        /// Whether @p inst (opened under @p serial) is still what the player has open.
        bool Alive(CCSPlayerController* player, BuilderInstance* inst, uint64_t serial)
        {
            if (!player || !menuBuilderManager.instances.count(inst))
                return false;
            return menus::menuManager.GetActiveMenu(player) == inst && inst->serial == serial;
        }

        CCSPlayerController* PlayerInSlot(int slot)
        {
            return clients::ClientsManager::Connected(CPlayerSlot(slot));
        }

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

    // ---- BuilderInstance ----------------------------------------------------------------

    BuilderInstance::BuilderInstance(CCSPlayerController* player, MenuImpl* root)
        : IMenuInstance(player, &root->shadow), serial(++g_nextSerial), m_root(root), m_slot(player->GetSlot()),
          m_opened(std::chrono::steady_clock::now())
    {
        m_path.push_back({ root, 0 });
        menuBuilderManager.instances.insert(this);
    }

    BuilderInstance::~BuilderInstance()
    {
        menuBuilderManager.instances.erase(this);
        if (m_ending)
            return;

        // Replaced by another menu or dropped with a leaving player: the
        // manager is destroying this from inside its own bookkeeping, so the
        // plugin hears of it a frame later -- if the menu is still there.
        const uint64_t rootId = m_root->id;
        const int slot = m_slot;
        scheduler::schedulerManager.NextFrame(0, [rootId, slot]()
        {
            MenuImpl* root = menuBuilderManager.Find(rootId);
            if (!root || !root->onEnd)
                return;
            CCSPlayerController* player = PlayerInSlot(slot);
            const auto onEnd = root->onEnd;
            onEnd(player, root, player ? SOURCE2MOD_MENUEND_REPLACED : SOURCE2MOD_MENUEND_DISCONNECTED);
        });
    }

    bool BuilderInstance::Uses(const MenuImpl* menu) const
    {
        return std::any_of(m_path.begin(), m_path.end(), [menu](const Level& l) { return l.menu == menu; });
    }

    void BuilderInstance::End(ESource2ModMenuEnd reason)
    {
        m_ending = true;
        CCSPlayerController* player = player_;
        const uint64_t rootId = m_root->id;

        // Destroys this instance.
        menus::menuManager.CloseActiveMenu(player);
        if (player)
            player->PrintToCenterHtml(" ", 3, true);

        MenuImpl* root = menuBuilderManager.Find(rootId);
        if (root && root->onEnd)
        {
            const auto onEnd = root->onEnd;
            onEnd(player, root, reason);
        }
    }

    void BuilderInstance::Close()
    {
        End(SOURCE2MOD_MENUEND_CLOSED);
    }

    void BuilderInstance::OnFrame()
    {
        if (m_root->timeout > 0 && std::chrono::steady_clock::now() - m_opened >= std::chrono::seconds(m_root->timeout))
        {
            End(SOURCE2MOD_MENUEND_TIMEOUT);
            return;
        }
        Display();
    }

    bool BuilderInstance::PageItems(std::vector<int>& items, int& pages)
    {
        CCSPlayerController* player = player_;
        const uint64_t mySerial = serial;
        MenuImpl* menu = Current();
        Level& level = m_path.back();

        // The visible entries, in order; a visibility callback may close the menu.
        std::vector<int> visible;
        const int count = static_cast<int>(menu->items.size());
        for (int i = 0; i < count; i++)
        {
            if (i >= static_cast<int>(menu->items.size()))
                break;
            const MenuImpl::Item& item = menu->items[i];
            if (item.flags & SOURCE2MOD_MENUITEM_HIDDEN)
                continue;
            if (item.visible)
            {
                const auto check = item.visible;
                const bool show = check(player);
                if (!Alive(player, this, mySerial))
                    return false;
                if (!show)
                    continue;
            }
            visible.push_back(i);
        }

        // Pages of up to itemsPerPage numbered items; text lines ride along.
        std::vector<std::vector<int>> split(1);
        int numbered = 0;
        for (int i : visible)
        {
            const bool isText = menu->items[i].kind == MenuImpl::Item::Text;
            if (!isText && numbered == menu->itemsPerPage)
            {
                split.emplace_back();
                numbered = 0;
            }
            split.back().push_back(isText ? -1 - i : i);
            if (!isText)
                numbered++;
        }

        pages = static_cast<int>(split.size());
        level.page = std::clamp(level.page, 0, pages - 1);
        items = split[level.page];
        return true;
    }

    void BuilderInstance::Display()
    {
        CCSPlayerController* player = player_;
        if (!player)
            return;
        const uint64_t mySerial = serial;
        MenuImpl* menu = Current();

        std::string title = menu->title;
        if (menu->dynamicTitle)
        {
            const auto text = menu->dynamicTitle;
            title = text(player);
            if (!Alive(player, this, mySerial))
                return;
        }

        std::vector<int> items;
        int pages = 1;
        if (!PageItems(items, pages))
            return;

        std::string html;
        html.reserve(512);
        html += "<b><font color='" + menu->titleColor + "'>" + Escape(title) + "</font></b><br>\n";

        int key = 1;
        for (int entry : items)
        {
            const bool isText = entry < 0;
            const int index = isText ? -1 - entry : entry;
            if (index >= static_cast<int>(menu->items.size()))
                continue;
            MenuImpl::Item item = menu->items[index];

            std::string text = item.text;
            if (item.dynamicText)
            {
                text = item.dynamicText(player);
                if (!Alive(player, this, mySerial))
                    return;
            }
            if (isText)
            {
                html += "<font color='" + menu->disabledColor + "'>" + Escape(text) + "</font><br>\n";
                continue;
            }

            bool enabled = !(item.flags & SOURCE2MOD_MENUITEM_DISABLED)
                && (item.kind == MenuImpl::Item::Submenu ? item.submenu != nullptr : static_cast<bool>(item.onSelect));
            if (enabled && item.enabled)
            {
                enabled = item.enabled(player);
                if (!Alive(player, this, mySerial))
                    return;
            }
            const std::string& color = enabled ? menu->enabledColor : menu->disabledColor;
            html += "<font color='" + color + "'>!" + std::to_string(key++) + "</font> " + Escape(text);
            if (item.kind == MenuImpl::Item::Submenu)
                html += " &gt;";
            html += "<br>\n";
        }

        const Level& level = m_path.back();
        if (level.page > 0 || m_path.size() > 1)
            html += "<font color='" + menu->navigationColor + "'>!7</font> &lt;- Back<br>\n";
        if (level.page < pages - 1)
            html += "<font color='" + menu->navigationColor + "'>!8</font> -&gt; Next<br>\n";
        if (m_root->exitButton)
            html += "<font color='" + menu->navigationColor + "'>!9</font> Exit<br>\n";

        player->PrintToCenterHtml(html.c_str(), 3, true);
    }

    void BuilderInstance::OnKeyPress(CCSPlayerController* p, int key)
    {
        CCSPlayerController* player = player_;
        if (p != player)
            return;
        const uint64_t mySerial = serial;

        std::vector<int> items;
        int pages = 1;
        if (!PageItems(items, pages))
            return;
        Level& level = m_path.back();

        if (key == 7 && (level.page > 0 || m_path.size() > 1))
        {
            if (level.page > 0)
                level.page--;
            else
                m_path.pop_back();
            Display();
            return;
        }
        if (key == 8 && level.page < pages - 1)
        {
            level.page++;
            Display();
            return;
        }
        if (key == 9 && m_root->exitButton)
        {
            End(SOURCE2MOD_MENUEND_EXIT);
            return;
        }
        if (key < 1 || key > kMaxPerPage)
            return;

        // The key-th numbered entry of the page.
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
        MenuImpl* menu = Current();
        if (index < 0 || index >= static_cast<int>(menu->items.size()))
            return;
        const MenuImpl::Item item = menu->items[index];
        if (item.flags & SOURCE2MOD_MENUITEM_DISABLED)
            return;
        if (item.enabled)
        {
            const bool enabled = item.enabled(player);
            if (!Alive(player, this, mySerial) || !enabled)
                return;
        }

        if (item.kind == MenuImpl::Item::Submenu)
        {
            if (!item.submenu)
                return;
            m_path.push_back({ item.submenu, 0 });
            Display();
            return;
        }
        if (!item.onSelect)
            return;

        {
            ::slow::Guard slowGuard("menu item", item.text.c_str(), menu->owner);
            item.onSelect(player, menu, index);
        }
        if (!Alive(player, this, mySerial))
            return;

        if (item.flags & SOURCE2MOD_MENUITEM_CLOSE)
        {
            End(SOURCE2MOD_MENUEND_SELECTED);
            return;
        }
        if (!(item.flags & SOURCE2MOD_MENUITEM_STAY))
        {
            switch (Current()->postSelect)
            {
            case PostSelectAction::Close:
                End(SOURCE2MOD_MENUEND_SELECTED);
                return;
            case PostSelectAction::Reset:
                m_path.back().page = 0;
                break;
            default:
                break;
            }
        }
        Display();
    }

    // ---- MenuImpl ------------------------------------------------------------------------

    MenuImpl::MenuImpl(PluginId owner, const char* title)
        : owner(owner), id(menuBuilderManager.nextId++), title(title ? title : ""), shadow(title ? title : "")
    {
    }

    ISource2ModMenu* MenuImpl::SetTitle(const char* title)
    {
        this->title = title ? title : "";
        shadow.SetTitle(this->title);
        return this;
    }

    ISource2ModMenu* MenuImpl::SetDynamicTitle(Source2ModMenuText title)
    {
        dynamicTitle = std::move(title);
        return this;
    }

    ISource2ModMenu* MenuImpl::AddItem(const char* text, Source2ModMenuSelect onSelect, int flags)
    {
        Item item;
        item.text = text ? text : "";
        item.onSelect = std::move(onSelect);
        item.flags = flags;
        items.push_back(std::move(item));
        return this;
    }

    ISource2ModMenu* MenuImpl::AddDynamicItem(Source2ModMenuText text, Source2ModMenuSelect onSelect, int flags,
                                              Source2ModMenuCheck enabled, Source2ModMenuCheck visible)
    {
        Item item;
        item.dynamicText = std::move(text);
        item.onSelect = std::move(onSelect);
        item.flags = flags;
        item.enabled = std::move(enabled);
        item.visible = std::move(visible);
        items.push_back(std::move(item));
        return this;
    }

    ISource2ModMenu* MenuImpl::AddSubmenu(const char* text, ISource2ModMenu* submenu, int flags)
    {
        Item item;
        item.kind = Item::Submenu;
        item.text = text ? text : "";
        item.submenu = static_cast<MenuImpl*>(submenu);
        item.flags = flags;
        items.push_back(std::move(item));
        return this;
    }

    ISource2ModMenu* MenuImpl::AddText(const char* text)
    {
        Item item;
        item.kind = Item::Text;
        item.text = text ? text : "";
        items.push_back(std::move(item));
        return this;
    }

    void MenuImpl::SetItemText(int item, const char* text)
    {
        if (item >= 0 && item < GetItemCount())
        {
            items[item].text = text ? text : "";
            items[item].dynamicText = {};
        }
    }

    void MenuImpl::SetItemFlags(int item, int flags)
    {
        if (item >= 0 && item < GetItemCount())
            items[item].flags = flags;
    }

    int MenuImpl::GetItemFlags(int item)
    {
        return (item >= 0 && item < GetItemCount()) ? items[item].flags : 0;
    }

    void MenuImpl::RemoveItem(int item)
    {
        if (item >= 0 && item < GetItemCount())
            items.erase(items.begin() + item);
    }

    void MenuImpl::ClearItems()
    {
        items.clear();
    }

    ISource2ModMenu* MenuImpl::SetItemsPerPage(int count)
    {
        itemsPerPage = std::clamp(count, 1, kMaxPerPage);
        return this;
    }

    ISource2ModMenu* MenuImpl::SetColors(const char* title, const char* enabled, const char* disabled, const char* navigation)
    {
        if (title && *title)
            titleColor = title;
        if (enabled && *enabled)
            enabledColor = enabled;
        if (disabled && *disabled)
            disabledColor = disabled;
        if (navigation && *navigation)
            navigationColor = navigation;
        return this;
    }

    void MenuImpl::Display(CCSPlayerController* player)
    {
        if (!player || !clients::ClientsManager::Connected(CPlayerSlot(player->GetSlot())))
            return;
        menus::menuManager.OpenMenu(owner, player, new BuilderInstance(player, this));
    }

    void MenuImpl::Close(CCSPlayerController* player)
    {
        if (player)
        {
            BuilderInstance* inst = menuBuilderManager.OpenInstance(player);
            if (inst && inst->Uses(this))
                inst->End(SOURCE2MOD_MENUEND_CLOSED);
            return;
        }
        const std::vector<BuilderInstance*> all(menuBuilderManager.instances.begin(), menuBuilderManager.instances.end());
        for (BuilderInstance* inst : all)
        {
            if (menuBuilderManager.instances.count(inst) && inst->Uses(this))
                inst->End(SOURCE2MOD_MENUEND_CLOSED);
        }
    }

    bool MenuImpl::IsOpenFor(CCSPlayerController* player)
    {
        BuilderInstance* inst = menuBuilderManager.OpenInstance(player);
        return inst && inst->Uses(this);
    }

    void MenuImpl::Release()
    {
        // Closing runs OnEnd, which may release this menu again: forget it first.
        if (!menuBuilderManager.Find(id))
            return;
        Close(nullptr);
        menuBuilderManager.Forget(this);
        for (MenuImpl* other : menuBuilderManager.menus)
        {
            for (Item& item : other->items)
            {
                if (item.submenu == this)
                    item.submenu = nullptr;
            }
        }
        delete this;
    }

    // ---- MenuBuilderManager ------------------------------------------------------------

    ISource2ModMenu* MenuBuilderManager::CreateMenu(PluginId owner, const char* title)
    {
        auto* menu = new MenuImpl(owner, title);
        menus.push_back(menu);
        return menu;
    }

    BuilderInstance* MenuBuilderManager::OpenInstance(CCSPlayerController* player)
    {
        if (!player)
            return nullptr;
        auto* active = menus::menuManager.GetActiveMenu(player);
        auto* inst = static_cast<BuilderInstance*>(active);
        return (active && instances.count(inst)) ? inst : nullptr;
    }

    ISource2ModMenu* MenuBuilderManager::GetOpenMenu(CCSPlayerController* player)
    {
        BuilderInstance* inst = OpenInstance(player);
        return inst ? inst->Current() : nullptr;
    }

    MenuImpl* MenuBuilderManager::Find(uint64_t id)
    {
        for (MenuImpl* menu : menus)
        {
            if (menu->id == id)
                return menu;
        }
        return nullptr;
    }

    void MenuBuilderManager::Forget(MenuImpl* menu)
    {
        std::erase(menus, menu);
    }

    void MenuBuilderManager::RemoveAllForPlugin(PluginId id)
    {
        std::vector<MenuImpl*> going;
        for (MenuImpl* menu : menus)
        {
            if (menu->owner == id)
                going.push_back(menu);
        }
        // Silently: the plugin is going, its OnEnd is not called any more.
        for (MenuImpl* menu : going)
            menu->onEnd = {};
        for (MenuImpl* menu : going)
        {
            if (Find(menu->id))
                menu->Release();
        }
    }
}
