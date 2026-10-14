# Contributing to Source2Mod

Thanks for helping. Bug fixes, gamedata updates, new source2mod features and docs
fixes are all welcome. Contributions go through pull requests from your own
fork, against `main`.

## Branches

| Branch | What it is |
|---|---|
| `main` | The released line. **Open your pull requests against `main`.** |
| `dev` | The maintainer's own working branch. It may be rebased or force-pushed at any time -- do not base work on it and do not open pull requests against it. |

## Workflow

1. **Fork** [Source2Mod/source2mod](https://github.com/SlynxCZ/Source2Mod)
   on GitHub (the *Fork* button, top right).

2. **Clone your fork** with its submodules:

   ```bash
   git clone --recurse-submodules https://github.com/<you>/source2mod.git
   cd source2mod
   ```

3. **Add the upstream remote**, so you can pull in what lands on `main`:

   ```bash
   git remote add upstream https://github.com/SlynxCZ/Source2Mod.git
   git fetch upstream
   ```

4. **Make a branch** for your change, from an up-to-date `upstream/main`. One
   branch per pull request:

   ```bash
   git switch -c fix/take-damage-crash upstream/main
   ```

5. **Commit** your work. Keep commits focused; the first line says what the
   change does (`Fix a crash in TakeDamage when the attacker is gone`), the body
   says why if it is not obvious.

6. **Stay current** before you open the pull request, and whenever `main` moves
   under you:

   ```bash
   git fetch upstream
   git rebase upstream/main
   git submodule update --init --recursive
   ```

7. **Push** the branch to your fork and **open a pull request** from it into
   `Source2Mod/source2mod:main`:

   ```bash
   git push -u origin fix/take-damage-crash
   ```

   After a rebase, `git push --force-with-lease`.

## Before you open the pull request

- **It builds** on Linux (and on Windows if you touched platform code). See
  [Building](https://www.source2toolkit.net/docs/development/building).
- **It was tested** on a server, if it changes runtime behaviour. Say in the
  pull request what you tested and how.
- **Public interfaces** (`ISource2Mod*` in the SDK) are versioned. A change to an
  existing interface's layout is a new revision (`ISource2ModX002` -> `003`) with
  the old one still served -- see
  [Compatibility](https://www.source2toolkit.net/docs/development/compatibility).
  Such changes usually need a matching pull request in
  [source2mod-sdk](https://github.com/SlynxCZ/Source2Mod-sdk);
  link the two.
- **Gamedata** changes: give the Linux and the Windows entry, and the CS2 build
  you checked them against.
- **Match the surrounding code**: naming, comment density, formatting.
- No `dynamic_cast` on engine types -- CS2's RTTI is not reliable; use
  `static_cast` or the `Is*()` helpers.

## Licence

Source2Mod is GPLv3 (see `LICENSE` and `LICENSE_INFO.txt`). By opening a
pull request you agree that your contribution is licensed under the same terms.

## Questions

Ask in the [Discord](https://discord.gg/4Ck56eDNXj) (`#plugin-developers`, or a
post in `#help`) or open an issue before starting on something large, so it does
not collide with work already under way.
