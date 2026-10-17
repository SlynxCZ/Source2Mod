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
#include "commands.h"

#include "hooks.h"
#include "hookid.h"
#include "menus.h"
#include "permissions.h"
#include "plugin.h"
#include "pluginapi.h"
#include "pluginmanager.h"
#include "raytrace.h"
#include "shared.h"
#include "slotinput.h"
#include "slowguard.h"
#include "utils/consoletable.h"
#include "utils/log.h"

#include <dynlibutils/module.hpp>
#include <source2mod/ISource2ModPlugin.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>

#include <cstdarg>

#define VERSION_STRING SEMVER " @ " GITHUB_SHA
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#define BUILD_ID SEMVER ":" GITHUB_SHA

#define SOURCE2MOD_WEBSITE "https://www.source2toolkit.net"
#define SOURCE2MOD_REPO    "https://github.com/Source2Mod/source2mod"

// The console colours of the "s2m" command, FUNPLAY's log style: the
// thing in question picked out, the rest plain or dim.
#define C_RESET "\033[0m"
#define C_LINK  "\033[96m"      // the website
#define C_HEAD  "\033[1;97m"    // a heading
#define C_OK    "\033[92m"      // done, stable
#define C_WARN  "\033[93m"      // careful, raw
#define C_ERR   "\033[91m"      // failed
#define C_ID    "\033[93m"      // a plugin id
#define C_NAME  "\033[97m"      // a name, a value
#define C_DIM   "\033[37m"      // version, author, detail (90 is unreadable on a dark console)
#define C_LABEL "\033[36m"      // "Name:", "Path:"
#define C_CMD   "\033[96m"      // a subcommand in the help


// A "source2mod ..." typed in the server console has no player behind it and
// belongs in that console; typed by a client it has to go back to that
// client's console instead, which is what ClientPrintf is for. Colour is
// dropped on that path -- the escapes are a terminal thing and the game
// console would print them literally -- the colours inside the line too;
// and on the Windows server console as well (see below).
static void Source2ModReply(const Source2ModCommandContext& ctx, const char* pszColor, const char* fmt, ...)
{
    char buf[1024];

    va_list ap;
    va_start(ap, fmt);
    V_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    // The line without its colour escapes.
    char line[1088];
    size_t n = 0;
    for (const char* p = buf; *p && n < sizeof(line) - 2; ++p)
    {
        if (*p == '\033')
        {
            while (*p && *p != 'm')
                ++p;
            if (!*p)
                break;
            continue;
        }
        line[n++] = *p;
    }
    line[n++] = '\n';
    line[n] = '\0';

    if (const CPlayerSlot slot = ctx.GetPlayerSlot(); slot.IsValid() && g_pEngineServer)
    {
        g_pEngineServer->ClientPrintf(slot, line);
        return;
    }

#ifdef _WIN32
    // The Windows dedicated console maps the escapes onto a palette of its
    // own (cyan comes out purple, white green), so it gets the plain line in
    // the console's own colour.
    ConMsg("%s", line);
#else
    ConMsg("%s%s" C_RESET "\n", pszColor, buf);
#endif
}

// A table goes out line by line, the frame dim and the cells as coloured as
// the caller made them; consoletable lines it up by display width, so it
// still lines up on a client's console once the colours are stripped.
static void ReplyTable(const Source2ModCommandContext& ctx, const consoletable::Table& table)
{
    for (const std::string& line : table.Render(C_DIM, C_RESET))
        Source2ModReply(ctx, C_RESET, "%s", line.c_str());
}

static void ReplyBanner(const Source2ModCommandContext& ctx)
{
    static const char* const kBanner[] = {
        "  /$$$$$$                                                     /$$$$$$  /$$      /$$                 /$$",
        " /$$__  $$                                                   /$$__  $$| $$$    /$$$                | $$",
        "| $$  \\__/  /$$$$$$  /$$   /$$  /$$$$$$   /$$$$$$$  /$$$$$$ |__/  \\ $$| $$$$  /$$$$  /$$$$$$   /$$$$$$$",
        "|  $$$$$$  /$$__  $$| $$  | $$ /$$__  $$ /$$_____/ /$$__  $$  /$$$$$$/| $$ $$/$$ $$ /$$__  $$ /$$__  $$",
        " \\____  $$| $$  \\ $$| $$  | $$| $$  \\__/| $$      | $$$$$$$$ /$$____/ | $$  $$$| $$| $$  \\ $$| $$  | $$",
        " /$$  \\ $$| $$  | $$| $$  | $$| $$      | $$      | $$_____/| $$      | $$\\  $ | $$| $$  | $$| $$  | $$",
        "|  $$$$$$/|  $$$$$$/|  $$$$$$/| $$      |  $$$$$$$|  $$$$$$$| $$$$$$$$| $$ \\/  | $$|  $$$$$$/|  $$$$$$$",
        " \\______/  \\______/  \\______/ |__/       \\_______/ \\_______/|________/|__/     |__/ \\______/  \\_______/",
    };
    Source2ModReply(ctx, C_RESET, " ");
    for (const char* line : kBanner)
        Source2ModReply(ctx, C_LINK, "  %s", line);
    Source2ModReply(ctx, C_RESET, " ");
}

// The line's base colour; the parts of it are coloured inline (C_*).
#define REPLY_INFO(fmt, ...)  Source2ModReply(ctx, C_RESET, fmt, ##__VA_ARGS__)
#define REPLY_OK(fmt, ...)    Source2ModReply(ctx, C_OK,    fmt, ##__VA_ARGS__)
#define REPLY_WARN(fmt, ...)  Source2ModReply(ctx, C_WARN,  fmt, ##__VA_ARGS__)
#define REPLY_ERROR(fmt, ...) Source2ModReply(ctx, C_ERR,   fmt, ##__VA_ARGS__)

// Source2Mod's own binary, resolved from an address inside it rather than
// assembled out of the game directory: the module the engine actually has
// open is the one worth reporting, and a stale copy left somewhere else on
// the search path is exactly what this line is there to catch.
static const char* Source2ModModulePath()
{
    static const DynLibUtils::CModule module(DynLibUtils::CMemory(reinterpret_cast<void*>(&Source2ModReply)));

    const std::string_view svPath = module.GetPath();
    return svPath.empty() ? "<unknown>" : svPath.data();
}

namespace commands {
    // Keyed by name and carrying the owner, so unloading a plugin can drop
    // the ConCommands it created. ConCommand registers itself with the engine
    // in its constructor and unregisters in its destructor, so erasing the
    // entry is what takes the command back out.
    struct RegisteredCommand
    {
        PluginId owner;
        std::unique_ptr<ConCommand> cmd;
    };

    static std::unordered_map<std::string, RegisteredCommand> registeredCommands;
    static std::unordered_map<std::string, std::vector<CommandEntry> > consoleListeners;

    // Ids are unique across every kind, so Unregister(id) needs no kind.
    static Source2ModHookId s_lastCommandId = 0;

    CommandsManager commandsManager;

    // A player reaches this handler through the same dispatch the server
    // console does -- from their own console, and from chat through the "!" and
    // "/" aliases RegisterConCommand sets up. Only the read-only half is
    // theirs to run; load, unload and refresh decide what code the server runs.
    // Allowlist rather than denylist, so a subcommand added later is not
    // exposed to players by forgetting about it here.
    static bool IsPlayerSubcommand(const char* cmd)
    {
        return strcmp(cmd, "list") == 0
            || strcmp(cmd, "info") == 0
            || strcmp(cmd, "version") == 0
            || strcmp(cmd, "credits") == 0;
    }

    static void HandleSource2ModCommand(const Source2ModCommandContext& ctx, const Source2ModCommandArgs& args, bool post)
    {
        int argc = args.ArgC();

        // A valid slot means a player issued this; the server console has none.
        const bool bFromPlayer = ctx.GetPlayerSlot().IsValid();

        if (argc < 2)
        {
            using consoletable::Table;
            Table help({ { "Command" }, { "Arguments" }, { "Description", consoletable::Align::Left, 60 } });
            help.SetTitle(C_HEAD "Source2Mod commands");
            help.AddRow({ C_CMD "s2m list", "", "Loaded plugins" });
            if (!bFromPlayer)
            {
                help.AddRow({ C_CMD "s2m load", C_DIM "<name>", "Load a plugin from plugins/" });
                help.AddRow({ C_CMD "s2m unload", C_DIM "<id>", "Unload a plugin" });
            }
            help.AddRow({ C_CMD "s2m info", C_DIM "<id>", "Everything about one plugin" });
            if (!bFromPlayer)
            {
                help.AddRow({ C_CMD "s2m refresh", "", "Load the plugins that are not loaded yet" });
                help.AddRow({ C_CMD "s2m hookdebug", C_DIM "<game hook|all|off>", "Log every call of matching game hooks" });
                help.AddRow({ C_CMD "s2m admins", C_DIM "<list|reload|info <slot|steamid>>", "Players' permissions" });
            }
            help.AddRow({ C_CMD "s2m version", "", "Build, API and Metamod versions" });
            help.AddRow({ C_CMD "s2m credits", "", "Who made Source2Mod" });

            ReplyBanner(ctx);
            ReplyTable(ctx, help);
            return;
        }

        const char* cmd = args.Arg(1);

        if (bFromPlayer && !IsPlayerSubcommand(cmd))
        {
            REPLY_ERROR("'" C_NAME "%s" C_ERR "' is not available from a client console.", cmd);
            return;
        }

        if (strcmp(cmd, "list") == 0)
        {
            if (pluginManager.m_plugins.empty())
            {
                REPLY_WARN("No plugins loaded.");
                return;
            }

            using consoletable::Align;
            consoletable::Table list({ { "Id", Align::Right }, { "Name" }, { "Version" }, { "Author", Align::Left, 40 }, { "Type", Align::Center } });

            char title[64];
            V_snprintf(title, sizeof(title), C_HEAD "%zu plugin(s) loaded", pluginManager.m_plugins.size());
            list.SetTitle(title);

            for (auto& p : pluginManager.m_plugins)
            {
                auto* api = p->api;
                list.AddRow({ C_ID + std::to_string(p->id), std::string(C_NAME) + api->GetName(), std::string(C_DIM) + api->GetVersion(),
                              std::string(C_DIM) + api->GetAuthor(), p->script ? C_WARN "SourcePawn" : C_OK "Native" });
            }

            ReplyTable(ctx, list);
        }

        else if (strcmp(cmd, "load") == 0)
        {
            if (argc < 3)
            {
                REPLY_ERROR("Usage: s2m load <name>");
                return;
            }

            char err[256]{};

            if (!pluginManager.LoadPlugin(args.Arg(2), err, sizeof(err)))
            {
                REPLY_ERROR("Load failed: %s", err);
                return;
            }

            REPLY_OK("Plugin '" C_NAME "%s" C_OK "' loaded.", args.Arg(2));
        }

        else if (strcmp(cmd, "unload") == 0)
        {
            if (argc < 3)
            {
                REPLY_ERROR("Usage: s2m unload <id>");
                return;
            }

            int id = atoi(args.Arg(2));

            if (id <= 0)
            {
                REPLY_ERROR("Invalid plugin id.");
                return;
            }

            // Synchronous: the command buffer runs between frames, outside
            // every hooked function. Only a plugin hooking
            // ICvar::DispatchConCommand itself could not be unloaded here.
            if (!pluginManager.UnloadPlugin(id))
            {
                REPLY_ERROR("Plugin " C_ID "%d" C_ERR " not found or failed to unload.", id);
                return;
            }

            REPLY_OK("Plugin " C_ID "%d" C_OK " unloaded.", id);
        }

        else if (strcmp(cmd, "_pending") == 0)
        {
            // Internal: queued by PluginManager::RequestUnload/RequestReload.
            pluginManager.RunPending();
        }

        else if (strcmp(cmd, "info") == 0)
        {
            if (argc < 3)
            {
                REPLY_ERROR("Usage: s2m info <id>");
                return;
            }

            int id = atoi(args.Arg(2));

            for (auto& p : pluginManager.m_plugins)
            {
                if (p->id == id)
                {
                    auto* api = p->api;

                    char title[64];
                    V_snprintf(title, sizeof(title), C_HEAD "Plugin " C_ID "%d", id);
                    consoletable::Table info = consoletable::KeyValue(title);

                    info.AddRow({ C_LABEL "Name", std::string(C_NAME) + api->GetName() });
                    info.AddRow({ C_LABEL "Version", std::string(C_NAME) + api->GetVersion() });
                    info.AddRow({ C_LABEL "Author", std::string(C_NAME) + api->GetAuthor() });
                    info.AddRow({ C_LABEL "Description", std::string(C_NAME) + api->GetDescription() });
                    info.AddRow({ C_LABEL "Type", p->script ? C_WARN "SourcePawn" : C_OK "Native" });
                    info.AddRow({ C_LABEL "Path", C_DIM + p->path });
                    // API 2 plugins say what they were built against; the tier
                    // tells the admin whether an engine or KHook change means a
                    // rebuild of this plugin or only of the core.
                    if (p->apiVersion >= 2)
                    {
                        const int rawHooks = api->GetRawHookCount();
                        info.AddRow({ C_LABEL "Plugin API", C_NAME + std::to_string(p->apiVersion) });
                        const char* khook = api->GetKHookCommit();
                        info.AddRow({ C_LABEL "KHook", khook && *khook ? std::string(C_DIM) + khook : C_DIM "(none of its own)" });
                        if (rawHooks)
                            info.AddRow({ C_LABEL "Own KHook hooks", C_NAME + std::to_string(rawHooks) + C_WARN " (raw tier: rebuild on a KHook or engine change)" });
                        else
                            info.AddRow({ C_LABEL "Own KHook hooks", C_NAME "0 " C_OK "(stable tier)" });
                    }
                    else
                    {
                        info.AddRow({ C_LABEL "Plugin API", C_NAME + std::to_string(p->apiVersion) + C_WARN " (built against an older SDK)" });
                    }
                    if (p->ifaces.empty())
                        info.AddRow({ C_LABEL "Interfaces", C_DIM "(none)" });
                    for (size_t i = 0; i < p->ifaces.size(); i++)
                        info.AddRow({ i ? "" : C_LABEL "Interfaces", C_DIM + p->ifaces[i] });

                    ReplyTable(ctx, info);
                    return;
                }
            }

            REPLY_ERROR("Plugin " C_ID "%d" C_ERR " not found.", id);
        }

        else if (strcmp(cmd, "hookdebug") == 0)
        {
            // Logs every call of the matching game hooks: each plugin's
            // answer, the outcome and the return value (gamehooks.cpp).
            if (argc < 3)
            {
                REPLY_INFO(C_LABEL "hookdebug: " C_NAME "%s", hooks::GetHookDebug());
                REPLY_INFO(C_DIM "Usage: s2m hookdebug <part of a game hook name, e.g. TakeDamage|all|off>");
                return;
            }

            hooks::SetHookDebug(args.Arg(2));
            REPLY_OK("hookdebug: " C_NAME "%s", hooks::GetHookDebug());
        }

        else if (strcmp(cmd, "admins") == 0)
        {
            auto& perms = permissions::permissionsManager;
            const char* sub = argc >= 3 ? args.Arg(2) : "list";

            if (strcmp(sub, "reload") == 0)
            {
                std::string error;
                if (!perms.LoadFile(error))
                {
                    REPLY_ERROR("permissions.json not reloaded: %s", error.c_str());
                    return;
                }
                REPLY_OK("permissions.json reloaded; what plugins granted is unchanged.");
            }
            else if (strcmp(sub, "info") == 0)
            {
                if (argc < 4)
                {
                    REPLY_ERROR("Usage: s2m admins info <slot|steamid>");
                    return;
                }

                // A small number is a slot, anything else a SteamID in any
                // spelling. The console splits "STEAM_1:0:5" at the colons,
                // so the pieces are put back together first.
                std::string who;
                for (int i = 3; i < argc; i++)
                    who += args.Arg(i);
                uint64 steamId = permissions::PermissionsManager::ParseSteamID(who);
                if (!steamId && !who.empty() && std::all_of(who.begin(), who.end(), ::isdigit))
                    steamId = perms.GetPlayerSteamID(CPlayerSlot(atoi(who.c_str())));

                if (!steamId)
                {
                    REPLY_ERROR("'" C_NAME "%s" C_ERR "' is neither a connected player's slot nor a SteamID.", who.c_str());
                    return;
                }

                for (const std::string& line : perms.DescribeSteamID(steamId))
                    REPLY_INFO("  %s", line.c_str());
            }
            else if (strcmp(sub, "list") == 0)
            {
                const std::vector<std::string> lines = perms.DescribePlayers();
                if (lines.empty())
                {
                    REPLY_WARN("No players connected.");
                    return;
                }
                for (const std::string& line : lines)
                    REPLY_INFO("  %s", line.c_str());
            }
            else
            {
                REPLY_ERROR("Usage: s2m admins <list|reload|info <slot|steamid>>");
            }
        }

        else if (strcmp(cmd, "refresh") == 0)
        {
            REPLY_INFO(C_DIM "Loading missing plugins...");

            pluginManager.LoadMissing();

            REPLY_OK("Done.");
        }

        else if (strcmp(cmd, "version") == 0)
        {
            // Metamod's numbers are not Source2Mod's: a .smx plugin binds to
            // SOURCE2MOD_PLAPI_VERSION, a metamod plugin next to us binds to
            // metamod's own. A plugin that refuses to load was built against
            // one of the two.
            int mmApiMajor = 0, mmApiMinor = 0, mmPlVers = 0, mmPlMin = 0;

            if (g_SMAPI)
                g_SMAPI->GetApiVersions(mmApiMajor, mmApiMinor, mmPlVers, mmPlMin);

            char buf[128];
            consoletable::Table version = consoletable::KeyValue(C_HEAD "Source2Mod Version Information");

            version.AddRow({ C_LABEL "Source2Mod version", C_NAME VERSION_STRING });
            V_snprintf(buf, sizeof(buf), C_NAME "%d" C_DIM " (%s)", SOURCE2MOD_PLAPI_VERSION, SOURCE2MOD_INTERFACE_NAME);
            version.AddRow({ C_LABEL "Plugin API version", buf });
            version.AddRow({ C_LABEL "Hooks", std::string(C_NAME "KHook" C_DIM ", served to plugins as ") + SOURCE2MOD_KHOOK_INTERFACE });
            V_snprintf(buf, sizeof(buf), C_NAME "%d:%d", mmPlVers, mmPlMin);
            version.AddRow({ C_LABEL "Metamod:Source plugin interface", buf });
            version.AddRow({ C_LABEL "Loaded as", C_NAME "Metamod:Source plugin" });
            version.AddRow({ C_LABEL "Path", std::string(C_DIM) + Source2ModModulePath() });
            version.AddRow({ C_LABEL "Compiled on", C_NAME BUILD_TIMESTAMP });

            // GITHUB_SHA is "Local" on anything but a CI build, and a commit
            // URL built out of that would point nowhere.
            if (strcmp(GITHUB_SHA, "Local") == 0)
                version.AddRow({ C_LABEL "Built from", C_WARN "local working tree" });
            else
                version.AddRow({ C_LABEL "Built from", std::string(C_DIM SOURCE2MOD_REPO "/commit/" C_NAME) + GITHUB_SHA });

            version.AddRow({ C_LABEL "Build ID", std::string(C_DIM) + BUILD_ID });
            version.AddSeparator();
            version.AddRow({ C_LABEL "Website", C_LINK SOURCE2MOD_WEBSITE });

            ReplyBanner(ctx);
            ReplyTable(ctx, version);
        }

        else if (strcmp(cmd, "credits") == 0)
        {
            // Keep this in step with ACKNOWLEDGEMENTS.md -- it is the same
            // list, short enough to read in a console.
            consoletable::Table credits = consoletable::KeyValue(C_HEAD "Source2Mod was developed by");
            credits.AddRow({ C_LABEL "Core, plugin system and SDK", C_NAME + std::string(reinterpret_cast<const char*>(u8"Michal \"Slynx (˙·٠● S l y n x ●٠·˙)\" Přikryl")) });
            credits.AddRow({ C_LABEL "Metamod:Source", C_NAME "David \"BAILOPAN\" Anderson, Scott \"DS\" Ehlert" });
            credits.AddRow({ C_LABEL "KHook", C_NAME + std::string(reinterpret_cast<const char*>(u8"Benoist \"Kenzzer\" André")) });
            credits.AddRow({ C_LABEL "SourceMod (plugin API, include set)", C_NAME "AlliedModders LLC." });
            credits.AddRow({ C_LABEL "SourcePawn (language, compiler, VM)", C_NAME "AlliedModders LLC." });
            credits.AddRow({ C_LABEL "HL2SDK and engine research", C_NAME "AlliedModders LLC." });
            credits.AddSeparator();
            credits.AddRow({ C_LABEL "Full list", C_DIM "ACKNOWLEDGEMENTS.md" });
            credits.AddRow({ C_LABEL "Website", C_LINK SOURCE2MOD_WEBSITE });

            ReplyTable(ctx, credits);
        }

        else
        {
            REPLY_WARN("Unknown command '" C_NAME "%s" C_WARN "'", cmd);
        }
    }

    static void HandleMenuCommand(const Source2ModCommandContext& ctx, const Source2ModCommandArgs& args, bool post)
    {
        CCSPlayerController* player = CCSPlayerController::FromSlot(ctx.GetPlayerSlot().Get());
        if (!player || player->m_iConnected() != PlayerConnectedState::Connected)
            return;

        if (args.ArgC() < 1)
            return;

        const char* cmd = args.Arg(0);

        int key = atoi(cmd);
        if (key < 1 || key > 9)
            return;

        if (slotinput::OnMenuCommand(ctx.GetPlayerSlot(), key))
            menus::menuManager.OnKeyPress(player, key);
    }

    void InitCommands()
    {
        commandsManager.RegisterConCommand(0, "source2mod", HandleSource2ModCommand);
        commandsManager.RegisterConCommand(0, "s2m", HandleSource2ModCommand);
        // What SourceMod admins type out of habit; CS2 has no SourceMod to clash with.
        commandsManager.RegisterConCommand(0, "sm", HandleSource2ModCommand);

        commandsManager.RegisterConCommand(0, "1", HandleMenuCommand);
        commandsManager.RegisterConCommand(0, "2", HandleMenuCommand);
        commandsManager.RegisterConCommand(0, "3", HandleMenuCommand);
        commandsManager.RegisterConCommand(0, "4", HandleMenuCommand);
        commandsManager.RegisterConCommand(0, "5", HandleMenuCommand);
        commandsManager.RegisterConCommand(0, "6", HandleMenuCommand);
        commandsManager.RegisterConCommand(0, "7", HandleMenuCommand);
        commandsManager.RegisterConCommand(0, "8", HandleMenuCommand);
        commandsManager.RegisterConCommand(0, "9", HandleMenuCommand);
    }

    void DestructCommands()
    {
        registeredCommands.clear();
        consoleListeners.clear();
    }

    // The ConCommand exists so the engine knows the name; the handler is not
    // reached from here. Every console command, registered or not, goes
    // through DispatchConsoleListener off the DispatchConCommand and
    // ClientCommand hooks (virtualhooks.cpp), which is where a plugin's
    // handler is called. Dispatching here as well would run it twice.
    void ConCommandRouter(const CCommandContext &ctx, const CCommand &args) {
        (void) ctx;
        (void) args;
    }

    // The engine's types stop here: handlers get Source2Mod's own, so neither
    // CCommand nor CCommandContext's layout is part of the plugin API.
    static Source2ModCommandArgs Source2ModArgs(const CCommand& args)
    {
        const char* argv[Source2ModCommandArgs::kMaxArgs];
        const int argc = args.ArgC() < Source2ModCommandArgs::kMaxArgs ? args.ArgC() : Source2ModCommandArgs::kMaxArgs;
        for (int i = 0; i < argc; i++)
            argv[i] = args.Arg(i);
        return Source2ModCommandArgs(argc, argv, args.ArgS(), args.GetCommandString());
    }

    Action DispatchConsoleListener(const CCommandContext& engineCtx, const CCommand& engineArgs, bool post, Source2ModCommandSource source) {
        const Source2ModCommandContext ctx(engineCtx.GetPlayerSlot(), static_cast<int>(engineCtx.GetTarget()), source);
        const Source2ModCommandArgs args = Source2ModArgs(engineArgs);
        std::string name = args.Arg(0);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        auto it = consoleListeners.find(name);
        if (it == consoleListeners.end())
            return Action::Ignore;

        Action result = Action::Ignore;

        // Copied: a handler may register or unregister commands while it runs.
        std::vector<CommandEntry> entries = it->second;
        bool denied = false;

        // A command or chat listener of Source2Mod's (entry.command set) is
        // the command itself, kept in the same list as the listeners on its
        // name. The listeners go first, whatever order things were registered
        // in: a pre listener's Supersede is what keeps the command from
        // running at all, as it does for an engine command.
        std::stable_partition(entries.begin(), entries.end(), [](const CommandEntry& e) { return e.command.empty(); });

        for (const auto &entry: entries) {
            if (entry.post != post)
                continue;

            // A chat command / ConCommand with a permission -- its own, or the
            // one permissions.json re-assigns it to. Only a player can lack it;
            // they are told once however many handlers share the name.
            if (!entry.command.empty() && ctx.GetPlayerSlot().IsValid())
            {
                const std::string permission = permissions::permissionsManager.CommandPermission(entry.command, entry.permission);
                if (!permission.empty() && !permissions::permissionsManager.PlayerHasPermission(ctx.GetPlayerSlot(), permission.c_str()))
                {
                    if (!denied)
                    {
                        denied = true;
                        permissions::permissionsManager.Deny(ctx, args, permission);
                    }
                    continue;
                }
            }

            Action thisResult;
            {
                ::slow::Guard slowGuard("command handler", name.c_str(), entry.owner);
                thisResult = entry.handler(ctx, args, post);
            }

            // Only Supersede stops the chain: Override still lets the original
            // run, so the remaining listeners get to see the command too.
            if (thisResult == Action::Supersede)
                return Action::Supersede;

            if (static_cast<int>(thisResult) > static_cast<int>(result))
                result = thisResult;
        }

        return result;
    }

    void CommandsManager::AddAliases(PluginId owner, Source2ModHookId id, const char* pchName, const ChatHandler& handler, const char* pchPermission)
    {
        const CommandHandler nativeHandler = WrapVoidHandler(handler);

        std::string permission = pchPermission ? pchPermission : "";
        std::transform(permission.begin(), permission.end(), permission.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        for (const std::string& name : { std::string(pchName), "/" + std::string(pchName), "!" + std::string(pchName) })
            consoleListeners[name].push_back({ owner, id, nativeHandler, false, handler, pchName, permission });
    }

    // The engine's console print for a player goes to their console; chat is
    // the HUD's. Either way one line, colour codes left to the caller.
    void CommandsManager::ReplyToCommand(const Source2ModCommandContext& ctx, const char* pszMessage)
    {
        if (!pszMessage)
            return;

        const CPlayerSlot slot = ctx.GetPlayerSlot();
        if (!slot.IsValid() || !g_pEngineServer)
        {
            ConMsg("%s\n", pszMessage);
            return;
        }

        if (ctx.IsFromChat())
        {
            if (CCSPlayerController* player = CCSPlayerController::FromSlot(slot))
                player->PrintToChat(pszMessage);
            return;
        }

        const std::string line = std::string(pszMessage) + "\n";
        g_pEngineServer->ClientPrintf(slot, line.c_str());
    }

    bool CommandsManager::RemoveAliases(PluginId owner, const char* pchName, const ChatHandler& handler)
    {
        if (!handler.HasIdentity())
            return false;

        bool found = false;
        for (const std::string& name : { std::string(pchName), "/" + std::string(pchName), "!" + std::string(pchName) })
        {
            auto it = consoleListeners.find(name);
            if (it == consoleListeners.end())
                continue;

            found |= std::erase_if(it->second, [owner, &handler](const CommandEntry& e)
            {
                return e.owner == owner && handler.SameAs(e.chatSource);
            }) > 0;

            if (it->second.empty())
                consoleListeners.erase(it);
        }
        return found;
    }

    Source2ModChatListenerId CommandsManager::RegisterChatListener(PluginId owner, const char* pchName, ChatHandler handler, const char* pchPermission) {
        const Source2ModHookId id = ++s_lastCommandId;
        AddAliases(owner, id, pchName, handler, pchPermission);
        return id;
    }


    // Takes a command name back out of the engine.
    //
    // ICvar offers no way to do this -- RegisterConCommand and
    // UnregisterConCommandCallbacks and nothing else -- so it goes through
    // CCvar's own layout, which hl2sdk carries reverse engineered in
    // public/icvar.h. FindConCommand() resolves a name through
    // m_ConCommandHashes (a name hash -> access index table), so dropping the
    // entry there frees the name; the ConCommandData in m_ConCommandList stays
    // behind, which is the class's own habit anyway -- it allocates those from
    // a bump buffer it never frees.
    //
    // The layout is reverse engineered, so it is checked against the public
    // API before anything is written: look the name up both ways and only
    // touch the table if the two agree on the access index. A mismatch means
    // this build's CCvar is not the one described here, and the caller falls
    // back to parking the command instead.
    static bool ReleaseConCommandName(const char* pchName)
    {
        if (!g_pCVar || !pchName || !*pchName)
            return false;

        const ConCommandRef ref = g_pCVar->FindConCommand(pchName, true);
        if (!ref.IsValidRef())
            return false;

        auto* pCvar = static_cast<CCvar*>(g_pCVar);
        auto& hashes = pCvar->m_ConCommandHashes;

        // The token is itself the hash the table is keyed by, so it is passed
        // explicitly -- CUtlHashtable's default functor has no overload for
        // CUtlStringToken.
        const CUtlStringToken token(pchName);
        const auto handle = hashes.Find(token, token.GetHashCode());

        if (handle == hashes.InvalidHandle())
        {
            FP_WARN("Could not release '{}': CCvar's command table does not look the way this build expects", pchName);
            return false;
        }

        if (hashes.Element(handle) != ref.GetAccessIndex())
        {
            FP_WARN("Could not release '{}': CCvar's command table disagrees with FindConCommand", pchName);
            return false;
        }

        return hashes.Remove(token, token.GetHashCode());
    }

    Source2ModCommandId CommandsManager::RegisterConCommand(PluginId owner, const char* pchName, ChatHandler handler, const char* pchPermission) {

        // Already ours: the ConCommand is Source2Mod's claim on the name and
        // outlives the plugin that asked for it. Unloading parks it (see
        // RemoveAllForPlugin); registering again brings the same one back
        // rather than fighting the engine for a name it will not give up.
        if (auto it = registeredCommands.find(pchName); it != registeredCommands.end())
        {
            if (it->second.cmd)
                it->second.cmd->RemoveFlags(FCVAR_DEFENSIVE | FCVAR_HIDDEN);

            it->second.owner = owner;
        }
        else
        {
            if (g_pCVar && g_pCVar->FindConCommand(pchName).IsValidRef())
            {
                // Somebody else's -- the engine's own, or another plugin's. The
                // name cannot be taken over, so the handler is reached through
                // the console listener instead.
                FP_WARN("Command '{}' already exists in the engine, registering a chat alias for it instead", pchName);
            }
            else
            {
                auto cmd = std::make_unique<ConCommand>(pchName, ConCommandRouter, ("Registered command: " + std::string(pchName)).c_str(), FCVAR_NONE);
                registeredCommands.emplace(pchName, RegisteredCommand{ owner, std::move(cmd) });
            }
        }

        const Source2ModHookId id = ++s_lastCommandId;
        AddAliases(owner, id, pchName, handler, pchPermission);
        return id;
    }

    Source2ModCommandHookId CommandsManager::RegisterConListener(PluginId owner, const char* pchName, CommandHandler handler, bool post) {
        const Source2ModHookId id = ++s_lastCommandId;
        consoleListeners[pchName].push_back({ owner, id, std::move(handler), post, {}, {}, {} });
        return id;
    }

    // RegisterChatListener / RegisterConCommand listen on three names: the bare
    // name and the / and ! prefixed aliases. All three go together.
    bool CommandsManager::UnregisterChatListener(PluginId owner, const char* pchName, const ChatHandler& handler)
    {
        return RemoveAliases(owner, pchName, handler);
    }

    bool CommandsManager::UnregisterConCommand(PluginId owner, const char* pchName, const ChatHandler& handler)
    {
        // The ConCommand itself stays Source2Mod's claim on the name until the
        // plugin unloads (RemoveAllForPlugin); only the handler goes.
        return RemoveAliases(owner, pchName, handler);
    }

    bool CommandsManager::UnregisterConListener(PluginId owner, const char* pchName, const CommandHandler& handler, bool post)
    {
        if (!handler.HasIdentity())
            return false;

        auto it = consoleListeners.find(pchName);
        if (it == consoleListeners.end())
            return false;

        const bool found = std::erase_if(it->second, [owner, post, &handler](const CommandEntry& e)
        {
            return e.owner == owner && e.post == post && handler.SameAs(e.handler);
        }) > 0;

        if (it->second.empty())
            consoleListeners.erase(it);
        return found;
    }

    bool CommandsManager::Unregister(Source2ModHookId id)
    {
        bool found = false;
        for (auto it = consoleListeners.begin(); it != consoleListeners.end(); )
        {
            found |= std::erase_if(it->second, [id](const CommandEntry& e) { return e.id == id; }) > 0;
            if (it->second.empty())
                it = consoleListeners.erase(it);
            else
                ++it;
        }
        return found;
    }

    void CommandsManager::RemoveAllForPlugin(PluginId id)
    {
        // Give the names back, so another plugin -- or this one with a
        // ConCommand of its own -- can take them afterwards.
        std::erase_if(registeredCommands, [id](auto& kv)
        {
            if (kv.second.owner != id)
                return false;

            if (ReleaseConCommandName(kv.first.c_str()))
                return true;

            // The table did not look the way we expect, so the name cannot be
            // released on this build. Park the command instead: FCVAR_DEFENSIVE
            // is what FindConCommand() skips unless asked for it explicitly and
            // FCVAR_HIDDEN keeps it out of find and autocomplete, so it is gone
            // from everything except a fresh registration of the same name.
            // Keep the entry, so registering again revives it.
            if (kv.second.cmd)
                kv.second.cmd->AddFlags(FCVAR_DEFENSIVE | FCVAR_HIDDEN);

            return false;
        });

        for (auto it = consoleListeners.begin(); it != consoleListeners.end(); )
        {
            auto& vec = it->second;

            std::erase_if(vec, [id](const CommandEntry& e)
            {
                return e.owner == id;
            });

            if (vec.empty())
                it = consoleListeners.erase(it);
            else
                ++it;
        }
    }

    void CommandsManager::UnlockConCommands()
    {
        if (shared::g_pCoreConfig->UnlockConCommands)
        {
            ConCommandData* data = g_pCVar->GetConCommandData(ConCommandRef());
            for (ConCommandRef ref = ConCommandRef((uint16)0); ref.GetRawData() != data; ref = ConCommandRef(ref.GetAccessIndex() + 1))
            {
                if (!ref.IsFlagSet(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY)) continue;
                ref.RemoveFlags(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY);
            }
        }
    }
}
