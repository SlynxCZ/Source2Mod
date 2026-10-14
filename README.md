# Source2Mod

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/SlynxCZ/Source2Mod/build.yml?branch=main)](https://github.com/SlynxCZ/Source2Mod/actions)
[![Website](https://img.shields.io/badge/Website-source2toolkit.net-blue)](https://www.source2toolkit.net)
[![Discord](https://img.shields.io/badge/Discord-join-7289da?logo=discord&logoColor=white)](https://discord.gg/4Ck56eDNXj)

**Source2Mod** is the ultimate scripting layer for Source 2 games built on top of Metamod:Source.  
Low-level power. High-level simplicity.

Build anything from simple plugins to advanced engine hooks with full access to Source 2 internals.

👉 **Get started quickly:** https://www.source2toolkit.net

---

## Why Source2Mod?

Designed for both beginners and hardcore engine hackers.

- **High-Level API** – Clean abstractions for rapid development  
- **Low-Level Access** – Full control over memory, hooks and engine internals  
- **Metamod Ready** – Built specifically for Metamod:Source  
- **Schema System** – Work with entities like native engine code  
- **Zero Overhead** – Near-native execution speed  

---

## Features

- **One plugin format** – every plugin is a `.smx`: a native C++ plugin (a shared library) or a SourcePawn script, told apart by the file itself  
- **SourcePawn** – SourceMod's language with its x86-64 JIT, `spcomp` and the SourceMod include set (as `#include <source2mod>`) in `addons/source2mod/scripting`  
- **Commands** – Register console & chat commands  
- **Custom HUD** – Panorama layouts with per-player state and click callbacks  
- **HUD elements and menus** – on-screen texts, toasts, timers and menus on the HUD through the [CustomhudManager_s2t](https://github.com/SlynxCZ/CustomhudManager_s2t) plugin  
- **ConVars** – Read, modify and replicate variables  
- **Entity System** – Schema-based entity access  
- **Events & GameEvents** – Pre/Post hook support with typed data  
- **Memory** – Direct memory access & manipulation  
- **Hooks** – KHook: virtual, vtable and function detours, one engine shared with Metamod  
- **Schema System** – Access SDK classes, offsets and fields  
- **Scheduler** – Timers and next-frame execution  
- **Sounds** – Per-player sound events, instant stop, per-player volume channels, hook & rewrite of game sounds  
- **Tracing** – Raycasts and collision queries  
- **Transmit** – Per-player entity visibility: hide players and entities, owner-following, a CheckTransmit hook  
- **Scripts** – Run cs_script from plugins: compiled assets, raw source and hot-reloaded files, inputs into scripts and messages back  
- **GameConfig** – Signature & offset management  
- **Dynamic Libraries** – Extend functionality with external modules  
- **Crash Handler** – Breakpad minidumps, local symbolization and Discord crash reports  

---

## Supported Games

- **Counter-Strike 2**

---

## Quick Start

👉 Full documentation: https://www.source2toolkit.net

### Prerequisites

- CMake 3.18+
- C++20 compatible compiler  
- Installed Metamod:Source  

---

## Building Source2Mod

```bash
git clone --recurse-submodules https://github.com/SlynxCZ/Source2Mod.git
cd Source2Mod

docker compose -f docker/docker-compose.yml up
```

## Documentation

- Docs: https://www.source2toolkit.net  
- Getting Started: https://www.source2toolkit.net/docs
- API Reference: https://www.source2toolkit.net/docs/core-api

---

## Contributing

Fork the repository, branch off `main`, and open the pull request against
`main` -- `dev` is the maintainer's own working branch. The full walkthrough
(upstream remote, rebasing, what a pull request needs) is in
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

This project is licensed under the GNU General Public License v3.0, with a
linking exception for Valve's engines and games, a dual-licensing exception
and an MIT exception for derivative works. See [LICENSE_INFO.txt](LICENSE_INFO.txt) for the terms
and [LICENSE](LICENSE) for the full GPLv3 text.

### Do I have to open-source my plugins?

No. The GPLv3 license carries an explicit MIT exception for derivative works:
your plugins, or anything built against the SDK, can stay closed-source or
commercial. The exception covers what you build on Source2Mod, not
Source2Mod itself -- a modified Source2Mod or SDK stays under the GPLv3.

SourcePawn plugins compile against SourceMod's include set, which stays under
AlliedModders LLC's own license terms; the exception above does not change them.

## Credits

- **SourceMod** and **SourcePawn** -- AlliedModders LLC. Source2Mod's script
  plugin API, its include set, the SourcePawn language, compiler and VM are
  their work.
- **Metamod:Source** and **s2sdk** -- AlliedModders LLC.
- **KHook** -- Benoist "Kenzzer" André.

The full list is in [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md).

---

<div align="center">
  <strong>Everything you need to build Source2Mod plugins. Nothing more.</strong>
</div>
