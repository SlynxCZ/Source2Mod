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
#include "slotinput.h"

#include "gameconfig.h"
#include "gamehooks.h"
#include "menus.h"
#include "pluginapi.h"
#include "shared.h"
#include "sounds.h"
#include "usercmd.h"
#include "utils/log.h"

#include <engine2/isource2server.h>
#include <source2mod/schema/entity/classes/CBasePlayerWeapon.h>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>
#include <source2mod/schema/entity/classes/CCSPlayerPawn.h>
#include <source2mod/schema/entity/classes/CCSPlayer_MovementServices.h>
#include <source2mod/schema/entity/classes/CCSWeaponBaseVData.h>
#include <source2mod/schema/entity/classes/CPlayer_WeaponServices.h>
#include <source2mod/schema/schema.h>
#include <tier0/tier0_strtools.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace slotinput {
    namespace
    {
        struct KeyDef
        {
            int Key;
            const char* Classname;
            uint16_t DefIndex;      // item definition index
            const char* Subclass;   // the vdata the fake gets (m_nSubclassID)
        };

        // How the client finds the weapon for each key (CCSHudWeaponSelection):
        //   1..5 -> vdata m_GearSlot == key-1 with m_GearSlotPosition in 0..20,
        //           lowest position wins, the LAST one on a tie
        //   6..9 -> econ item definition (44 HE, 43 flash, 45 smoke, 47
        //           decoy), the first match in m_hMyWeapons
        // 6..9 are found by definition only, so their fakes carry the knife's
        // vdata: gear slot knife, where the player always has a real knife.
        // A fake alone in its gear slot makes the client's view flick on the
        // key (1, 2, 4, 5 without a real weapon there; not fixable without an
        // addon -- a second fake in the slot does not help). "4" needs the
        // econ HE vdata "44" (grenade position 0..3; the class-named
        // "weapon_hegrenade" has -1 and is never found by the slot).
        // Inject() puts 6..9 in front of the real weapons and 1..5 behind
        // them, so every key finds its fake before any real weapon.
        constexpr KeyDef kKeys[] = {
            { 1, "weapon_ak47",          7, "weapon_ak47" },
            // Any pistol does (gear slot 1); the client plays the fake's draw
            // sound when it predicts the switch, and the Glock's is loud.
            { 2, "weapon_p250",         36, "weapon_p250" },
            { 3, "weapon_knife",        42, "weapon_knife" },
            { 6, "weapon_hegrenade",    44, "weapon_knife" },
            { 4, "weapon_hegrenade",    44, "44" },
            { 5, "weapon_c4",           49, "weapon_c4" },
            { 7, "weapon_flashbang",    43, "weapon_knife" },
            { 8, "weapon_smokegrenade", 45, "weapon_knife" },
            { 9, "weapon_decoy",        47, "weapon_knife" },
        };

        constexpr int kMaxKey = 9;

        struct Fake
        {
            int Key;
            CHandle<CBasePlayerWeapon> Weapon;
            // Snapshots its owner has been sent it in; after a couple it is
            // kept dormant on the owner's client (see BlockForViewer).
            int Sent = 0;
        };

        // Sent this many times to its owner, a fake is not transmitted to
        // them any more: the client keeps it dormant.
        constexpr int kSendsBeforeDormant = 3;

        struct Session
        {
            bool Active = false;
            CHandle<CCSPlayerPawn> Pawn;
            std::vector<Fake> Fakes;
            bool Injected = false;
            // Keys whose fake could not be created -- not retried every frame.
            uint32_t FailedKeys = 0;
            // What the open menu page does something with (bit = key).
            uint32_t MenuKeys = 0;
            // The injected m_hMyWeapons the client was last told about.
            uint64_t SentSignature = 0;
            // Frames left before the fakes go; while > 0 they are owned and
            // still injected, so the client sees them owned once.
            int Handover = 0;
        };

        enum class KeySource : uint8_t
        {
            Unknown,
            SlotOnly,       // plain slotN: take it right away
            CommandBound,   // "slotN;N": the command does it, the slot is ignored
        };

        // Per connected player, kept across menus.
        struct Learned
        {
            CHandle<CCSPlayerController> Owner;
            KeySource Source[kMaxKey + 1] = {};
            int CommandTick[kMaxKey + 1] = {};
            // An "N" command that did something in the open menu. "slotN;N"
            // sends the command first and the slot's weapon switch a tick
            // later -- by then the menu may have moved on or closed, so the
            // switch is matched against this instead.
            int MenuCommandTick[kMaxKey + 1] = {};
            int SlotDeliveredTick[kMaxKey + 1] = {};
        };

        struct PendingKey
        {
            int Slot;
            int Key;
            int DeliverTick;
        };

        Session g_Sessions[ABSOLUTE_PLAYER_LIMIT];
        Learned g_Learned[ABSOLUTE_PLAYER_LIMIT];
        std::vector<PendingKey> g_Pending;
        bool g_bAnyInjected = false;
        Source2ModGameHookId g_RunCommandHook = 0;

        int TickCount()
        {
            const CGlobalVars* globals = pluginApi.GetGlobalVars();
            return globals ? globals->tickcount : 0;
        }

        // How long a slot key and the "N" command of the same press may lie
        // apart: both leave the client in the same frame. 150 ms at 64 tick.
        constexpr int kWindowTicks = 10;

        Learned& LearnedFor(int slot, CCSPlayerController* player)
        {
            Learned& learned = g_Learned[slot];
            CHandle<CCSPlayerController> handle(player);
            if (learned.Owner != handle)
            {
                learned = Learned{};
                learned.Owner = handle;
            }
            return learned;
        }

        // NetworkStateChanged for the whole of m_hMyWeapons: its size AND every
        // element. The plain field NSC (array index -1) only sends the size,
        // so a list that kept its length but changed its contents (page 1 with
        // fake 8, page 2 with fake 7) stayed on the client as it was.
        void MarkListChanged(CPlayer_WeaponServices* ws)
        {
            static const int16_t chainOffset = schema::FindChainOffset("CPlayer_WeaponServices");
            static const int32_t fieldOffset = schema::GetServerOffset("CPlayer_WeaponServices", "m_hMyWeapons");

            if (chainOffset == 0 || fieldOffset < 0)
            {
                ws->m_hMyWeapons.NetworkStateChanged();
                return;
            }

            auto* chainer = reinterpret_cast<CNetworkVarChainer*>(reinterpret_cast<uintptr_t>(ws) + chainOffset);

            NetworkStateChangedData list(static_cast<uint32>(fieldOffset));
            chainer->NetworkStateChanged(list);

            const int count = ws->m_hMyWeapons().Count();
            for (int i = 0; i < count; i++)
            {
                NetworkStateChangedData element(static_cast<uint32>(fieldOffset), i);
                chainer->NetworkStateChanged(element);
            }
        }

        CPlayer_WeaponServices* WeaponServicesOf(Session& s)
        {
            CCSPlayerPawn* pawn = s.Pawn.Get();
            return pawn ? pawn->m_pWeaponServices() : nullptr;
        }

        // CBaseEntity::UpdateSubclassVData: looks the vdata up for the
        // entity's m_nSubclassID (the subclass name as a string token) and
        // stores it. A bare CreateEntityByName leaves the vdata null and
        // CCSWeaponBase::Spawn reads it straight away. SetSubclass (what
        // subclass_change runs) does the same and then sets the model, which
        // asserts for an entity not spawned yet (SetupModel,
        // EF_IN_STAGING_LIST) -- Spawn sets the model from the vdata anyway.
        using UpdateSubclassVData_t = void (*)(CBaseEntity* entity);

        UpdateSubclassVData_t GetUpdateSubclassVData()
        {
            static UpdateSubclassVData_t fn = [] {
                UpdateSubclassVData_t found = shared::g_pGameConfig ? shared::g_pGameConfig->ResolveSignature("CBaseEntity::UpdateSubclassVData").RCast<UpdateSubclassVData_t>() : nullptr;
                if (!found)
                    FP_ERROR("SlotInput: gamedata signature CBaseEntity::UpdateSubclassVData not found; menu keys need binds");
                return found;
            }();
            return fn;
        }

        CBasePlayerWeapon* CreateFake(CCSPlayerPawn* pawn, const KeyDef& def)
        {
            UpdateSubclassVData_t updateSubclassVData = GetUpdateSubclassVData();
            if (!updateSubclassVData)
                return nullptr;

            // Always a knife, carrying the vdata of the weapon the key stands
            // for, and no owner while the menu is open: the client's HUD lets
            // a knife be selected without an owner, so the key still leaves as
            // weaponselect, and its prediction cannot deploy a weapon without
            // one (CanDeploy) -- no switch on the press. Before the fakes go,
            // they get the pawn as owner for one snapshot (BeginHandover), or
            // the client keeps them in its slot selection after they are gone.
            auto* weapon = CBaseEntity::New<CBasePlayerWeapon>("weapon_knife");
            if (!weapon)
                return nullptr;

            *weapon->m_nSubclassID() = CUtlStringToken(def.Subclass);
            updateSubclassVData(weapon);
            if (!weapon->GetVData())
            {
                FP_ERROR("SlotInput: no vdata for {} ({})", def.Classname, def.Subclass);
                weapon->Remove();
                return nullptr;
            }

            // Before the spawn, the way the econ item system sets it up.
            auto& item = weapon->m_AttributeManager().m_Item();
            item.m_iItemDefinitionIndex() = def.DefIndex;
            item.m_bInitialized() = true;

            weapon->DispatchSpawn();

            // The client only selects a grenade that has ammo, and checks the
            // clip first -- the player's own grenade ammo stays untouched.
            weapon->m_iClip1() = 1;
            weapon->m_iClip1.NetworkStateChanged();

            Vector origin = pawn->GetAbsOrigin();
            weapon->Teleport(&origin, nullptr, nullptr);

            // No owner while the menu is open (see above). Non-solid, so
            // nobody touches it to pick it up.
            weapon->m_fEffects() |= EF_NODRAW;
            weapon->m_fEffects.NetworkStateChanged();

            // An ownerless weapon is a dropped one to the client, which draws
            // its world model whatever EF_NODRAW says; nothing to draw, then.
            weapon->m_nRenderMode() = kRenderNone;
            weapon->m_nRenderMode.NetworkStateChanged();
            weapon->m_clrRender() = Color(255, 255, 255, 0);
            weapon->m_clrRender.NetworkStateChanged();

            weapon->m_Collision().m_nSolidType() = SOLID_NONE;
            weapon->m_Collision().m_usSolidFlags() |= FSOLID_NOT_SOLID;
            weapon->CollisionRulesChanged();

            // Follows the pawn, so it stays in the owner's PVS.
            weapon->AcceptInput("SetParent", pawn, pawn, "!activator");
            return weapon;
        }

        void RemoveFromList(CPlayer_WeaponServices* ws, Session& s)
        {
            auto& myWeapons = ws->m_hMyWeapons();
            for (const Fake& fake : s.Fakes)
                myWeapons.FindAndRemove(fake.Weapon);
        }

        // Closing, in two steps: the fakes are sent again (out of dormancy)
        // still ownerless, and only once the client has them active do they
        // get the pawn as owner, then they go. The client drops a fake from
        // its slot selection only on an owner change of an ACTIVE entity --
        // leaving dormancy with the owner already set in the same snapshot
        // left the fake behind.
        constexpr int kHandoverFrames = 8;
        constexpr int kHandoverOwnerAt = 4;

        bool BeginHandover(Session& s)
        {
            CCSPlayerPawn* pawn = s.Pawn.Get();
            if (!pawn || !pawn->m_pWeaponServices())
                return false;

            s.Handover = kHandoverFrames;
            return true;
        }

        void HandOver(Session& s)
        {
            CCSPlayerPawn* pawn = s.Pawn.Get();
            if (!pawn)
                return;

            for (const Fake& fake : s.Fakes)
            {
                if (CBasePlayerWeapon* weapon = fake.Weapon.Get())
                {
                    weapon->m_hOwnerEntity() = CHandle<CBaseEntity>(pawn);
                    weapon->m_hOwnerEntity.NetworkStateChanged();
                }
            }
        }

        void Stop(int slot)
        {
            Session& s = g_Sessions[slot];
            if (!s.Active)
                return;


            if (CPlayer_WeaponServices* ws = WeaponServicesOf(s))
            {
                if (s.Injected)
                    RemoveFromList(ws, s);

                // Once more with the real list, so the client drops the fakes too.
                MarkListChanged(ws);
            }

            for (const Fake& fake : s.Fakes)
            {
                if (CBasePlayerWeapon* weapon = fake.Weapon.Get())
                    weapon->Remove();
            }

            s = Session{};
        }

        // One fake per key for the whole menu, except the keys the player's
        // binds already send as "N". The page only decides what Inject() puts
        // in the list: deleting a fake on a page change left the client with
        // the one it had just switched to gone, and it then ignored a new one.
        void Sync(Session& s, const Learned& learned)
        {
            CCSPlayerPawn* pawn = s.Pawn.Get();
            if (!pawn)
                return;

            for (const KeyDef& def : kKeys)
            {
                auto it = std::find_if(s.Fakes.begin(), s.Fakes.end(), [&](const Fake& f) { return f.Key == def.Key; });

                if (it != s.Fakes.end() && !it->Weapon.Get())
                {
                    FP_WARN("SlotInput: the fake for key {} was deleted by someone", def.Key);
                    s.Fakes.erase(it);
                    it = s.Fakes.end();
                }

                const bool wanted = learned.Source[def.Key] != KeySource::CommandBound;

                if (!wanted && it != s.Fakes.end())
                {
                    it->Weapon.Get()->Remove();
                    s.Fakes.erase(it);
                }
                else if (wanted && it == s.Fakes.end() && !(s.FailedKeys & (1u << def.Key)))
                {
                    CBasePlayerWeapon* weapon = CreateFake(pawn, def);
                    if (!weapon)
                    {
                        FP_WARN("SlotInput: could not create {} for key {}", def.Classname, def.Key);
                        s.FailedKeys |= 1u << def.Key;
                        continue;
                    }

                    s.Fakes.push_back({ def.Key, CHandle<CBasePlayerWeapon>(weapon) });
                }
            }
        }

        // Keys the player's open menu does something with right now; 0 = no
        // menu, a menu that takes no keys, or nobody alive to hold a fake.
        uint32_t MenuKeys(CCSPlayerController* player)
        {
            if (!player || player->IsBot() || !player->m_bPawnIsAlive())
                return 0;

            CCSPlayerPawn* pawn = player->GetPlayerPawn();
            if (!pawn || !pawn->m_pWeaponServices())
                return 0;

            return menus::menuManager.ActiveKeyMask(player);
        }

        // The menu sound SourceMod menus had, for a key from the keyboard and
        // only one the page does something with. Stock CS2 UI soundevents.
        void PlayKeySound(CCSPlayerController* player, int key)
        {
            const int slot = player->GetPlayerSlot().Get();
            if (slot < 0 || slot >= ABSOLUTE_PLAYER_LIMIT)
                return;

            const Session& s = g_Sessions[slot];
            if (!s.Active || !(s.MenuKeys & (1u << key)))
                return;

            const char* sound = key == 9 ? "UIPanorama.mainmenu_press_quit"
                : (key == 7 || key == 8) ? "UIPanorama.sidemenu_select"
                : "UIPanorama.submenu_select";

            static_cast<ISource2ModSounds&>(sounds::soundsManager).EmitSoundToPlayers(SoundRecipients(1) << slot, sound);
        }

        // A slot key read from a usercmd. Either the command already brought
        // it, or it goes out right away; should the "N" of a "slotN;N" bind
        // turn up after it, OnMenuCommand swallows that one.
        void OnSlotKey(int slot, CCSPlayerController* player, int key)
        {
            Learned& learned = LearnedFor(slot, player);
            const int tick = TickCount();

            if (learned.Source[key] == KeySource::CommandBound)
                return;

            if (learned.CommandTick[key] && tick - learned.CommandTick[key] <= kWindowTicks)
            {
                learned.Source[key] = KeySource::CommandBound;
                return;
            }

            g_Pending.push_back({ slot, key, tick });
        }

        void DeliverPending()
        {
            if (g_Pending.empty())
                return;

            const int tick = TickCount();
            std::vector<PendingKey> due;

            for (auto it = g_Pending.begin(); it != g_Pending.end();)
            {
                if (it->DeliverTick <= tick)
                {
                    due.push_back(*it);
                    it = g_Pending.erase(it);
                }
                else
                    ++it;
            }

            // Menu handlers may open other menus or queue more; work on a copy.
            for (const PendingKey& p : due)
            {
                CCSPlayerController* player = CCSPlayerController::FromSlot(p.Slot);
                if (!player)
                    continue;

                Learned& learned = LearnedFor(p.Slot, player);
                learned.SlotDeliveredTick[p.Key] = tick;

                // A slot key that went out without its command: slot-only,
                // also for one learned as bound before (binds changed).
                learned.Source[p.Key] = KeySource::SlotOnly;

                PlayKeySound(player, p.Key);
                menus::menuManager.OnKeyPress(player, p.Key);
            }
        }

        // The usercmd's weaponselect: a fake is a key and never a switch; a
        // real weapon in a slot whose key the menu uses is not switched to
        // either (the key arrives through the fake, or through "N").
        bool OnWeaponSelect(CCSPlayerController* player, CUserCmd* cmd)
        {
            const int slot = player->GetPlayerSlot().Get();
            if (slot < 0 || slot >= ABSOLUTE_PLAYER_LIMIT)
                return false;

            Session& s = g_Sessions[slot];
            const int index = cmd->base().weaponselect();

            if (s.Active)
            {
                for (const Fake& fake : s.Fakes)
                {
                    CBasePlayerWeapon* weapon = fake.Weapon.Get();
                    if (weapon && weapon->GetEntityIndex().Get() == index)
                    {
                        cmd->mutable_base()->clear_weaponselect();
                        OnSlotKey(slot, player, fake.Key);
                        return true;
                    }
                }
            }

            CCSPlayerPawn* pawn = player->GetPlayerPawn();
            CPlayer_WeaponServices* ws = pawn ? pawn->m_pWeaponServices() : nullptr;
            if (!ws)
                return false;

            CBasePlayerWeapon* selected = nullptr;
            auto& myWeapons = ws->m_hMyWeapons();
            FOR_EACH_VEC(myWeapons, i)
            {
                CBasePlayerWeapon* weapon = myWeapons[i].Get();
                if (weapon && weapon->GetEntityIndex().Get() == index)
                {
                    selected = weapon;
                    break;
                }
            }

            if (!selected)
                return false;

            auto* vdata = selected->GetWeaponVData();
            const int gearSlot = vdata ? static_cast<int>(vdata->m_GearSlot()) : -1;
            const char* classname = selected->GetClassname();

            static const char* const kKeyGrenades[] = { "weapon_hegrenade", "weapon_flashbang", "weapon_smokegrenade", "weapon_decoy" };
            auto fitsKey = [&](int key) {
                if (key <= 5)
                    return gearSlot == key - 1;
                return !V_strcmp(classname, kKeyGrenades[key - 6]);
            };

            // 1) The switch half of a "slotN;N" bind whose "N" was a menu action
            //    a moment ago -- the menu may have changed page or closed since.
            Learned& learned = LearnedFor(slot, player);
            const int tick = TickCount();
            for (int key = 1; key <= kMaxKey; key++)
            {
                if (learned.MenuCommandTick[key] && tick - learned.MenuCommandTick[key] <= kWindowTicks && fitsKey(key))
                {
                    cmd->mutable_base()->clear_weaponselect();
                    return true;
                }
            }

            // 2) A menu open and the key of that slot does something in it: no
            //    switch. Not taken as a key press either -- the wheel and
            //    lastinv land here too.
            if (!s.Active || gearSlot < 0 || gearSlot > 4)
                return false;

            int key = gearSlot + 1;
            for (int g = 0; g < 4; g++)
            {
                if (!V_strcmp(classname, kKeyGrenades[g]) && learned.Source[6 + g] == KeySource::CommandBound)
                    key = 6 + g;
            }

            if (!(s.MenuKeys & (1u << key)))
                return false;

            cmd->mutable_base()->clear_weaponselect();

            // 3) A key learned as bound that came without its command first:
            //    the binds may have changed. Taken as the key after the window,
            //    unless the command still turns up.
            if (learned.Source[key] == KeySource::CommandBound)
            {
                const bool queued = std::any_of(g_Pending.begin(), g_Pending.end(),
                    [&](const PendingKey& p) { return p.Slot == slot && p.Key == key; });
                if (!queued)
                    g_Pending.push_back({ slot, key, tick + kWindowTicks });
            }

            return true;
        }

        GameHookReturn<void> OnRunCommand(RunCommandContext& ctx, bool post)
        {
            Eject();

            auto* cmd = static_cast<CUserCmd*>(ctx.cmd);
            if (!cmd || !ctx.services)
                return { Action::Ignore };

            const auto& base = cmd->base();
            if (!base.has_weaponselect() || base.weaponselect() <= 0)
                return { Action::Ignore };

            CCSPlayerPawn* pawn = ctx.services->GetPlayerPawn();
            CCSPlayerController* controller = pawn ? pawn->GetController() : nullptr;
            if (!controller || !controller->m_bPawnIsAlive() || controller->IsBot())
                return { Action::Ignore };

            OnWeaponSelect(controller, cmd);
            return { Action::Ignore };
        }
    }

    void Init()
    {
        g_RunCommandHook = gamehooks::gameHooksManager.HookRunCommand(OnRunCommand, false);
    }

    void Shutdown()
    {
        if (g_RunCommandHook)
            gamehooks::gameHooksManager.UnhookRunCommand(g_RunCommandHook);
        g_RunCommandHook = 0;

        Eject();
        for (int slot = 0; slot < ABSOLUTE_PLAYER_LIMIT; slot++)
            Stop(slot);

        g_Pending.clear();
    }

    void ResetPlayer(CPlayerSlot slot)
    {
        const int s = slot.Get();
        if (s < 0 || s >= ABSOLUTE_PLAYER_LIMIT)
            return;

        g_Learned[s] = Learned{};
        std::erase_if(g_Pending, [s](const PendingKey& p) { return p.Slot == s; });
    }

    bool OnMenuCommand(CPlayerSlot playerSlot, int key)
    {
        const int slot = playerSlot.Get();
        if (key < 1 || key > kMaxKey || slot < 0 || slot >= ABSOLUTE_PLAYER_LIMIT)
            return true;

        CCSPlayerController* player = CCSPlayerController::FromSlot(slot);
        if (!player)
            return true;

        Learned& learned = LearnedFor(slot, player);
        const int tick = TickCount();
        learned.CommandTick[key] = tick;

        // Called before the menu gets the key: MenuKeys is the page it was
        // pressed on.
        const Session& session = g_Sessions[slot];
        if (session.Active && (session.MenuKeys & (1u << key)))
            learned.MenuCommandTick[key] = tick;

        // The same press already queued through the slot path: drop that.
        const auto pending = std::find_if(g_Pending.begin(), g_Pending.end(),
            [&](const PendingKey& p) { return p.Slot == slot && p.Key == key; });

        const bool slotDelivered = learned.SlotDeliveredTick[key] && tick - learned.SlotDeliveredTick[key] <= kWindowTicks;
        const bool twin = pending != g_Pending.end() || slotDelivered;

        if (pending != g_Pending.end())
            g_Pending.erase(pending);

        if (!slotDelivered)
            PlayKeySound(player, key);

        if (twin)
            learned.Source[key] = KeySource::CommandBound;

        return !slotDelivered;
    }

    void OnFrame()
    {
        for (int slot = 0; slot < ABSOLUTE_PLAYER_LIMIT; slot++)
        {
            CCSPlayerController* player = CCSPlayerController::FromSlot(slot);
            Session& s = g_Sessions[slot];

            const uint32_t menuKeys = MenuKeys(player);
            if (!menuKeys)
            {
                // Closing: out of dormancy, then the owner, then the fakes go.
                if (s.Active && s.Handover == 0 && BeginHandover(s))
                    continue;
                if (s.Handover > 0 && --s.Handover > 0)
                {
                    if (s.Handover == kHandoverOwnerAt)
                        HandOver(s);
                    continue;
                }
                Stop(slot);
                continue;
            }

            CCSPlayerPawn* pawn = player->GetPlayerPawn();
            if (s.Active && (s.Pawn.Get() != pawn || s.Handover > 0))
                Stop(slot);

            if (!s.Active)
            {
                s.Active = true;
                s.Pawn = CHandle<CCSPlayerPawn>(pawn);

                // A menu opening: keys learned as bound get their fake back,
                // or after the binds are dropped the key could never unlearn
                // itself. Still bound, the first press puts it back.
                Learned& learned = LearnedFor(slot, player);
                for (KeySource& source : learned.Source)
                {
                    if (source == KeySource::CommandBound)
                        source = KeySource::Unknown;
                }
            }

            s.MenuKeys = menuKeys;
            Sync(s, LearnedFor(slot, player));
        }

        DeliverPending();
    }

    void Inject()
    {
        if (g_bAnyInjected)
            Eject();

        for (Session& s : g_Sessions)
        {
            if (!s.Active || s.Fakes.empty())
                continue;

            CCSPlayerPawn* pawn = s.Pawn.Get();
            if (!pawn || !pawn->m_pWeaponServices())
                continue;

            // Only the keys the page uses. 6..9 are found by item definition,
            // the FIRST match wins: in front. 1..5 by gear slot, and on a tie
            // the LAST one wins: at the end.
            auto& myWeapons = pawn->m_pWeaponServices()->m_hMyWeapons();
            int front = 0;
            for (const KeyDef& def : kKeys)
            {
                if (!(s.MenuKeys & (1u << def.Key)))
                    continue;

                for (const Fake& fake : s.Fakes)
                {
                    if (fake.Key != def.Key || !fake.Weapon.Get())
                        continue;

                    if (def.Key >= 6)
                        myWeapons.InsertBefore(front++, fake.Weapon);
                    else
                        myWeapons.AddToTail(fake.Weapon);
                }
            }

            // Marked only when the list differs from the last one sent: marking
            // it every snapshot made the client rebuild its loadout HUD every
            // tick (it flickered, in a different order each time). A full
            // repack reads the memory as it is now, fakes included, so an
            // unchanged list needs no mark.
            uint64_t signature = 1469598103934665603ull;
            for (int i = 0; i < myWeapons.Count(); i++)
                signature = (signature ^ static_cast<uint32_t>(myWeapons[i].ToInt())) * 1099511628211ull;
            if (signature != s.SentSignature)
            {
                MarkListChanged(pawn->m_pWeaponServices());
                s.SentSignature = signature;
            }
            s.Injected = true;
            g_bAnyInjected = true;
        }
    }

    void Eject()
    {
        if (!g_bAnyInjected)
            return;

        for (Session& s : g_Sessions)
        {
            if (!s.Active || !s.Injected)
                continue;

            if (CPlayer_WeaponServices* ws = WeaponServicesOf(s))
                RemoveFromList(ws, s);

            s.Injected = false;
        }

        g_bAnyInjected = false;
    }

    // Nobody else ever gets a player's fakes. The owner gets each one a few
    // times and then no more: the client keeps it dormant (non-transmit, not
    // deleted). The loadout HUD skips dormant weapons -- it would otherwise add
    // an ownerless fake's row every update and delete it the next, because it
    // keeps a row only for a weapon owned by the player -- while the slot
    // selection still finds the dormant fake and sends the key. The owner gets
    // them again on a full update, and during the handover, so the owner the
    // fakes get before they go reaches the client.
    void BlockForViewer(CCheckTransmitInfo* info)
    {
        if (!info || !info->m_pTransmitEntity || !info->m_pNonTransmitEntity)
            return;

        const int viewerSlot = info->m_nPlayerSlot.Get();
        for (int slot = 0; slot < ABSOLUTE_PLAYER_LIMIT; slot++)
        {
            Session& s = g_Sessions[slot];
            if (!s.Active)
                continue;

            const bool own = slot == viewerSlot;
            for (Fake& fake : s.Fakes)
            {
                CBasePlayerWeapon* weapon = fake.Weapon.Get();
                if (!weapon)
                    continue;

                if (own)
                {
                    if (info->m_bFullUpdate || s.Handover > 0)
                        fake.Sent = 0;
                    if (s.Handover > 0 || fake.Sent++ < kSendsBeforeDormant)
                        continue;
                }

                const int index = weapon->GetEntityIndex().Get();
                info->m_pTransmitEntity->Clear(index);
                info->m_pNonTransmitEntity->Set(index);
            }
        }
    }
}
