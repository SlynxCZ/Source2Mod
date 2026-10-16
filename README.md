# Source2Mod

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/SlynxCZ/source2mod/build.yml?branch=main)](https://github.com/SlynxCZ/source2mod/actions)
[![Website](https://img.shields.io/badge/Website-source2mod.net-blue)](https://source2mod.net)
[![Discord](https://img.shields.io/badge/Discord-join-7289da?logo=discord&logoColor=white)](https://discord.gg/4Ck56eDNXj)

**Source2Mod** is a server modification for Counter-Strike 2 on top of
Metamod:Source. Plugins are written in **C++** or **SourcePawn** -- both are
first-class, load side by side and use the same API: full access to Source 2
internals, together with the high-level pieces every server plugin needs.

Source2Mod is the successor of [Source2Toolkit](https://github.com/Source2Toolkit/source2toolkit):
its C++ core and SDK carried over, and SourcePawn was added on top of them.

---

## Two languages, one API

| | C++ | SourcePawn |
| --- | --- | --- |
| Plugin file | `.smx` -- a native library | `.smx` -- a compiled SourcePawn script |
| Built with | The [SDK](https://github.com/SlynxCZ/source2mod-sdk) (CMake or AMBuild) | `spcomp`, shipped in `addons/source2mod/scripting` |
| API | `ISource2Mod*` interfaces, `g_pSource2Mod*` globals | `Source2Mod*` methodmaps, `#include <source2mod/api>` |
| Schema | Generated classes (`CCSPlayerController`, ...) | Methodmaps of the same names, fields as properties |
| Hot reload | Yes | Yes |

The core tells the two kinds apart by the file itself, so both go into
`addons/source2mod/plugins/`. The script API mirrors the C++ one name for
name -- `ISource2ModCommands::RegisterConCommand` is
`Source2ModCommands.RegisterConCommand` -- and C++ plugins can give scripts
natives of their own. `samples/cs2/cpp_sample` and `samples/cs2/sp_sample` are
the same plugin in both languages, section for section.

```cpp
// C++
g_pSource2ModCommands->RegisterConCommand("hello", [](const Source2ModCommandContext& ctx, const Source2ModCommandArgs&, bool)
{
    if (auto* player = CCSPlayerController::FromSlot(ctx.GetPlayerSlot()))
        player->PrintToChat("Hello!");
});
```

```sourcepawn
// SourcePawn
public void OnPluginStart()
{
    Source2ModCommands.RegisterConCommand("hello", Cmd_Hello);
}

public void Cmd_Hello(Source2ModCommandContext context, Source2ModCommandArgs args, bool post)
{
    CCSPlayerController player = CCSPlayerController.FromSlot(context.GetPlayerSlot());
    if (player != NULL_ENTITY)
        player.PrintToChat("Hello!");
}
```

---

## Features

Everything below is there for C++ and SourcePawn alike.

- **Schema system** -- entities, fields and offsets from the schema the game ships, networked writes included
- **Entities** -- find, create, spawn, listen to entities and their I/O
- **Hooks** -- game hooks from the core's gamedata, KHook virtual / vtable / function detours shared with Metamod, raw hooks from scripts
- **Commands & ConVars** -- console and chat commands, listeners on the game's own, typed ConVars with replication
- **Events** -- game events pre / post, typed fields, `dontBroadcast`, firing your own
- **Clients & targeting** -- players by slot, SteamID or name, `@all` / `@ct` targets with immunity
- **Permissions** -- admins, groups and dotted permissions, commands gated by them
- **Menus** -- center-screen menus with pages and submenus; keys 1-9 work without binds
- **Translations** -- phrase files per plugin, every player in their own language
- **Network messages** -- build, send and hook protobuf net messages by id or name
- **Sounds** -- per-player sound events, stop by guid, volume channels, a hook on every game sound
- **Transmit** -- per-viewer entity visibility and a CheckTransmit hook
- **Custom HUD** -- Panorama layouts and their click callbacks
- **Scheduler & tracing** -- timers, next-frame tasks, ray and hull traces
- **Scripts** -- run cs_script from plugins, inputs into scripts and messages back
- **SQL** -- MySQL and SQLite, off the main thread, named connections in `configs/databases.json`
- **HTTP, JSON, files, KeyValues** -- async requests, a JSON value API, file I/O and a streaming KeyValues parser
- **Client preferences, GeoIP, map lists** -- per-player settings, a player's country, the map cycle and next map
- **GameConfig & memory** -- signatures, offsets and patches from JSON gamedata, pattern scans, module access
- **Crash handler** -- Breakpad minidumps, local symbolization and optional Discord crash reports

---

## Supported Games

- **Counter-Strike 2** (Linux and Windows servers)

---

## Building Source2Mod

Prerequisites: CMake 3.18+, a C++20 compiler, Metamod:Source on the server.

```bash
git clone --recurse-submodules https://github.com/SlynxCZ/source2mod.git
cd source2mod

docker compose -f docker/docker-compose.yml up
```

The build puts the core, the gamedata and `addons/source2mod/scripting`
(`spcomp` and the include set) under `addons/`, ready to copy to the server.

## Writing plugins

The documentation is on [source2mod.net](https://source2mod.net): every
example in C++ and SourcePawn, guides and the full API reference.

- **C++** -- add the [Source2Mod SDK](https://github.com/SlynxCZ/source2mod-sdk)
  to your project and `add_source2mod_plugin(...)`; `samples/cs2/cpp_sample`
  shows every area of the API.
- **SourcePawn** -- `#include <source2mod/api>` and compile with
  `addons/source2mod/scripting/spcomp`; `samples/cs2/sp_sample` is the same
  plugin as a script.

---

## Contributing

Fork the repository, branch off `main`, and open the pull request against
`main` -- `dev` is the maintainer's own working branch. The full walkthrough
(upstream remote, rebasing, what a pull request needs) is in
[CONTRIBUTING.md](CONTRIBUTING.md). What is still open -- and the community
plugins Source2Mod is waiting for -- is in [TODO.md](TODO.md).

## License

This project is licensed under the GNU General Public License v3.0, with a
linking exception for Valve's engines and games, a dual-licensing exception
and an MIT exception for derivative works. See [LICENSE_INFO.txt](LICENSE_INFO.txt) for the terms
and [LICENSE](LICENSE) for the full GPLv3 text.

### Do I have to open-source my plugins?

No. The GPLv3 license carries an explicit MIT exception for derivative works:
your plugins, C++ or SourcePawn, can stay closed-source or commercial. The
exception covers what you build on Source2Mod, not Source2Mod itself -- a
modified Source2Mod or SDK stays under the GPLv3.

SourcePawn and the parts of the include set that come from SourceMod stay
under AlliedModders LLC's own license terms; the exception above does not
change them.

## Credits

- **Source2Toolkit** -- the C++ framework Source2Mod grew out of.
- **SourceMod** and **SourcePawn** -- AlliedModders LLC. The script API follows
  SourceMod's; the SourcePawn language, compiler and VM are theirs.
- **Metamod:Source** and **s2sdk** -- AlliedModders LLC.
- **KHook** -- Benoist "Kenzzer" André.

The full list is in [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md).

---

<div align="center">
  <strong>C++ or SourcePawn -- everything you need to build Counter-Strike 2 server plugins.</strong>
</div>
