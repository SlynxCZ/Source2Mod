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

## Starter bundle

A set of first plugins to ship next to the core, so a fresh server has
something to run: helpers like the HUD manager (on-screen texts, toasts,
timers and HUD menus), plus the community plugins above as they appear.

## Core

The pieces that were listed here are in: translations, files and KeyValues
with a streaming parser, utilities (strings, sorting, regex, profiler, time,
file logging, IntMap), SQL with MySQL and SQLite drivers, targeting with
immunity, the menu builder, client helpers, client preferences, GeoIP, map
lists and next map, and feature checks -- in C++ and in SourcePawn.

Still open:

- **Hot reload on Windows** -- the file watcher is Linux-only (inotify).
  Plugins load from a private copy now, so the file in `plugins/` can be
  replaced while it is loaded; what is missing is the watch itself
  (`ReadDirectoryChangesW`).
- **Menu keys without binds, keys 1, 2, 4 and 5** -- the view flicks for one
  frame when the player has no real weapon in that slot (the fake key weapon
  is then alone in its gear slot). Keys 3 and 6-9 do not flick. No fix found
  without a client addon.
- **SourcePawn gaps** -- custom menu instances (a C++ `IMenuInstance`
  subclass), per-plugin menu cooldown handlers, `TraceShapeEx` and
  `CheckAreaOverlappingEntity` are C++ only.
- **Guides** -- the API reference covers every interface; hand-written guides
  for translations, targeting, clients, client preferences and files /
  KeyValues are still to be written.
