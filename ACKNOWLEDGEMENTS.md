# Acknowledgements

## CS2Fixes

We've cherrypicked certain reverse-engineered implementations and
adapted parts of the codebase (including custom cherry-picks and
adjustments for our use case). CS2Fixes is and will always be a great
repository for reverse engineering work in CS2.

``` cpp
/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2026 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 */
```

## EntityIO

We've implemented EntityIO support based on publicly available research
and custom cherry-picked implementations, adapting them to fit our
architecture and use cases.

## Nlohmann/json

JSON library used on the native side.

## AlliedModders

Metamod:Source, which loads Source2Mod, and s2sdk (formerly HL2SDK's `cs2`
branch), which it compiles against, are AlliedModders LLC's work --
Metamod:Source by David "BAILOPAN" Anderson and Scott "DS" Ehlert. The
source2mod's plugin manager, interface sharing and GameDLL communication follow
Metamod:Source's model, and s2sdk is its reference for engine structures and
interfaces.

## SourceMod

Source2Mod's SourcePawn plugin API follows SourceMod's, AlliedModders LLC's
server modification for Source 1 games. The include set (kept in the
Source2Mod SDK under `public/scripting`, shipped in
`addons/source2mod/scripting/include`) builds on SourceMod's, renamed to
Source2Mod; every file taken from it keeps its AlliedModders LLC license
header unchanged. The natives
behind it are implemented on Source2Mod's own systems.

``` cpp
/**
 * =============================================================================
 * SourceMod
 * Copyright (C) 2004-2026 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 */
```

## SourcePawn

The SourcePawn language, its compiler (spcomp) and its virtual machine with the
x86-64 JIT, which run every `.smx` script plugin, are AlliedModders LLC's work,
originally by David "BAILOPAN" Anderson. Built from `vendor/sourcepawn`.

## KHook

Detouring library Source2Mod and its plugins hook with -- virtual, vtable and
function detours on the one engine Metamod:Source runs. By Benoist "Kenzzer"
André, shipped as part of Metamod:Source.

## CounterStrikeSharp

We've used and modified parts of CounterStrikeSharp for schema
generation (schemagen).

## SwiftlyS2

We've used SwiftlyS2 as a reference for signatures and virtual
function indexes, cross-checking our own gamedata against theirs.
The gamedata validator (tools/gamedata_validator) is modelled on their
gamedata-validator (https://github.com/swiftly-solution/gamedata-validator),
and the sound system's reverse engineering started from what SwiftlyS2
had already worked out.

## sql_mm

The SQLite driver of ISource2ModSQL (src/core/sqlite.cpp) follows the
approach of zer0k-z's sql_mm (https://github.com/zer0k-z/sql_mm,
GPL-3.0): an SQLite file behind the same asynchronous connection,
query and transaction calls as MySQL, results read on a worker thread
and handed back on the main one.

## GeoLite2

configs/addons/source2mod/shared/GeoLite2-City.mmdb and
GeoLite2-Country.mmdb are GeoLite2 data created by MaxMind, available
from https://www.maxmind.com, under the GeoLite2 End User License
Agreement.
