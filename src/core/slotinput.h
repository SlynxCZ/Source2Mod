/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once

#include <playerslot.h>

class CCheckTransmitInfo;

// Menu keys 1-9 without binds. The number keys run slot1..slot9 on the
// client, which never reaches the server as such -- only as the weapon the
// client picked for that slot (usercmd weaponselect). While a living player
// has a menu open that takes keys, every key gets a fake weapon: owned by the
// pawn, invisible, never networked to anyone else, and in m_hMyWeapons only
// while a snapshot is packed. The client picks the fake for the key, the
// server sees its index in weaponselect, drops the switch and presses the
// key in the menu. Players who bound "slotN;N" are learned per key and get
// no fake for it (their "N" command already does the job).
namespace slotinput {
    void Init();
    void Shutdown();

    // GameFrame pre: the fakes leave m_hMyWeapons before any game code runs.
    void Eject();
    // GameFrame post: fakes created / dropped for the menus open now, and
    // the keys whose command did not show up delivered.
    void OnFrame();
    // CheckTransmit, before the pack: the fakes go into m_hMyWeapons; and per
    // viewer, the other players' fakes are kept off the wire.
    void Inject();
    void BlockForViewer(CCheckTransmitInfo* info);

    // A "1".."9" command, before the menu gets it. False: the same press
    // already went to the menu through its slot key ("slotN;N" whose slot
    // half arrived first), so the menu must not get it again.
    bool OnMenuCommand(CPlayerSlot slot, int key);

    void ResetPlayer(CPlayerSlot slot);
}
