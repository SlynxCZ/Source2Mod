# Gamedata validator

Checks `configs/addons/source2mod/gamedata/*.json` against CS2's Linux and
Windows server binaries, so a CS2 update that breaks a signature or shifts a
vtable shows up within half an hour -- before a server owner finds out.

| Entry | Check | Result |
|---|---|---|
| signature | scanned in the library's `.text`, as DynLibUtils does | 🟢 one match · 🟡 several (the first is used) · 🔴 none |
| symbol (`"@name"`) | looked up in the exports | 🟢 found · 🔴 missing |
| vtable offset | the class's primary vtable is found through RTTI, the index checked against its size | 🟢 in range · 🟡 the vtable changed size since the last run · 🔴 past the end |
| field offset, unknown interface | listed in `config.json` / no RTTI for the class | ⚪ not checked |

## CI

`.github/workflows/gamedata-validator.yml` runs every 30 minutes and compares
the manifests of the binary depots (2347773 Linux, 2347771 Windows) with the
last validated ones. On a CS2 update, on a gamedata change on `main`, on a pull
request touching gamedata and by hand (*Run workflow*) it downloads the
libraries with DepotDownloader and validates. Results go to:

- the job summary,
- Discord, through the `GAMEDATA_WEBHOOK` repository secret (`#gamedata-status`;
  `/setup webhook` in the Discord server prints the URL),
- the `gamedata-status` branch: `latest/` and one folder per CS2 build, each with
  `results.json` and `report.md`.

A broken entry fails the run.

## Locally

No packages needed, Python 3.8+:

```bash
python3 tools/gamedata_validator/validate.py \
  --binaries /path/to/cs2/game/bin/linuxsteamrt64 /path/to/cs2/game/csgo/bin/linuxsteamrt64 \
             /path/to/windows/game/bin/win64 /path/to/windows/game/csgo/bin/win64
```

Either platform alone works too; the other is reported as not checked.

## config.json

- `libraries` -- the libraries searched for vtables (signatures name their own).
- `multi_match_signatures` -- signatures that are meant to match many places
  (`NetworkVar::StateChanged` is compared against every NetworkVar wrapper),
  with the reason; one or more matches is good for them.
- `field_offsets` -- offsets that are struct fields, not vtable indices. Names
  or `fnmatch` patterns: `*::m_*` and `*::sizeof` cover plugin gamedata
  (e.g. FUNPLAY's `CEntityWriteInfo::m_pBuf`) that follows the `m_` naming.
- `class_aliases` -- the class whose vtable an offset is checked against, when
  the gamedata name is an interface (`ISource2GameClients` ->
  `CSource2GameClients`). An offset on a derived class belongs under that
  class's name (`CCSGameRules::GoToIntermission`, not `CGameRules::`), so it is
  checked against the right vtable.

## Credits

Modelled on [swiftly-solution/gamedata-validator](https://github.com/swiftly-solution/gamedata-validator)
by the SwiftlyS2 team -- the idea, the depot-watching loop and the
per-signature Discord report are theirs. This one is a from-scratch Python
implementation for Source2Mod's gamedata format, with RTTI-based vtable checks
instead of a vtable size dump.
