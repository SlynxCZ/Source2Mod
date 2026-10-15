# To-do

What Source2Mod does not have yet. The core and its C++ / SourcePawn API are
worked on in this repository; the plugins below are meant for the community --
they live in repositories of their own and build on the API.

## Community plugins

SourceMod shipped a set of base plugins that made it work "out of the box".
Source2Mod's equivalents are open for anyone to write:

- **Admin plugin** -- admin commands (kick, slay, slap, team, map, rcon, cvar,
  exec, who), an admin menu built with the core's menu builder, and admins
  loaded from the core's permissions (`configs/permissions.json`). Targeting
  (`@all`, `@ct`, `#userid`, ...) comes from the core.
- **Bans** -- banning by SteamID / IP with durations and reasons, stored through
  the core's SQL layer, checked on connect; a web panel is a plus.
- **Chat** -- admin say / chat / center / hint messages, private messages.
- **Communication** -- mute / gag / silence (voice and chat), timed.
- **Fun commands** -- beacon, freeze, burn, slap, noclip, gravity, ...
- **Votes** -- generic votes (kick, ban, map) on the core's menu builder.
- **Map voting** -- rock the vote, nominations, end-of-map vote and next map,
  on the core's map list and next map API.
- **Reserved slots** -- slots kept free for players with a permission.
- **Anti-flood** -- chat and command rate limits.

## Core (in progress)

Worked on in this repository:

- Translations (phrase files per plugin, the player's language, `%t` / `%T`).
- Files, KeyValues and a streaming KeyValues parser.
- Utilities: string helpers, sorting, regex, profiler, time, file logging,
  IntMap.
- SQL: `ISource2ModSQL` with MySQL and SQLite drivers (was `ISource2ModMySQL`).
- Targeting: command text to players, with immunity.
- Menu builder: pages, submenus, back button, dynamic items.
- Client helpers still missing from the API (current map, fake client
  commands, ...).
- Client preferences (cookies), GeoIP, map lists and next map, feature checks.
