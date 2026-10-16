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

/*
 * SourcePawn natives for source2mod/targeting.inc: Source2ModTargeting on
 * ISource2ModTargeting. A script's target filters go when it unloads.
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/targeting.h"

#include <sp_callable.h>

#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        targeting::TargetingManager& Targeting()
        {
            return targeting::targetingManager;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        // ProcessTargetString(pattern, caller, flags, targets, maxTargets, name, nameLen, &nameIsPhrase)
        cell_t N_ProcessTargetString(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<CPlayerSlot> targets;
            Source2ModTargetName name;
            const int result = Targeting().ProcessTargetString(LocalString(ctx, params[1]), CPlayerSlot(params[2]), params[3], targets, &name);

            cell_t* out;
            ctx->LocalToPhysAddr(params[4], &out);
            const cell_t max = params[5];
            const cell_t count = std::min<cell_t>(static_cast<cell_t>(targets.size()), std::max<cell_t>(max, 0));
            for (cell_t i = 0; i < count; i++)
                out[i] = targets[i].Get();

            if (params[0] >= 7)
                ToBuffer(ctx, params[6], params[7], name.text.c_str());
            cell_t* isPhrase;
            if (params[0] >= 8 && ctx->LocalToPhysAddr(params[8], &isPhrase) == SP_ERROR_NONE)
                *isPhrase = name.isPhrase;
            return result > 0 ? count : result;
        }

        // FindTarget(pattern, caller, flags, &target)
        cell_t N_FindTarget(IPluginContext* ctx, const cell_t* params)
        {
            CPlayerSlot target(-1);
            const int result = Targeting().FindTarget(LocalString(ctx, params[1]), CPlayerSlot(params[2]), params[3], target);
            cell_t* out;
            ctx->LocalToPhysAddr(params[4], &out);
            *out = target.Get();
            return result;
        }

        cell_t N_GetTargetErrorPhrase(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], Targeting().GetTargetErrorPhrase(params[1]));
        }

        cell_t N_GetTargetErrorText(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], Targeting().GetTargetErrorText(params[1]));
        }

        // AddTargetFilter(name, filter, phrase, phraseIsTranslation)
        cell_t N_AddTargetFilter(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[2]));
            if (!fn)
                return ctx->ThrowNativeError("Invalid function id (%X)", params[2]);
            const PluginId owner = CallerId(ctx);

            // The script fills an ArrayList of slots; it lives for the call only.
            Source2ModTargetFilter filter = [fn, owner](CPlayerSlot caller, std::vector<CPlayerSlot>& targets) -> bool
            {
                const cell_t list = NewCellList({}, owner);
                if (!list)
                    return false;
                sp::CallArgs args;
                args.PushCell(caller.Get());
                args.PushCell(list);
                cell_t result = 0;
                const bool ok = Invoke(fn, args, &result) && result;
                std::vector<cell_t> slots;
                if (ok)
                    ReadCellList(fn->GetParentRuntime()->GetDefaultContext(), list, slots);
                Handles().Free(list, 0, true);
                for (cell_t slot : slots)
                {
                    if (slot >= 0 && slot < 64)
                        targets.push_back(CPlayerSlot(slot));
                }
                return ok;
            };
            const bool translated = params[0] >= 4 && params[4];
            return Targeting().AddTargetFilterFor(owner, LocalString(ctx, params[1]), std::move(filter), LocalString(ctx, params[3]), translated);
        }

        cell_t N_RemoveTargetFilter(IPluginContext*, const cell_t* params)
        {
            return Targeting().RemoveTargetFilter(params[1]);
        }

        const sp_nativeinfo_t g_targetingNatives[] = {
            { "Source2ModTargeting.ProcessTargetString", N_ProcessTargetString },
            { "Source2ModTargeting.FindTarget", N_FindTarget },
            { "Source2ModTargeting.GetTargetErrorPhrase", N_GetTargetErrorPhrase },
            { "Source2ModTargeting.GetTargetErrorText", N_GetTargetErrorText },
            { "Source2ModTargeting.AddTargetFilter", N_AddTargetFilter },
            { "Source2ModTargeting.RemoveTargetFilter", N_RemoveTargetFilter },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetTargetingNatives()
    {
        return g_targetingNatives;
    }
}
