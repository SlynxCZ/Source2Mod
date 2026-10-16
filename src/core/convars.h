/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once
#include "hookid.h"

#include <source2mod/ISource2ModConVars.h>

#include <vector>

namespace convars {
    struct ConVarChangeEntry
    {
        PluginId owner;
        ConVarChangeHandler handler;
        Source2ModHookId id;
    };

    class ConVarsManager final : public ISource2ModConVars
    {
    public:
        uint16 GetConvarAccessIndexByName(const char* name) override;
        ConVarRefAbstract GetConvarRef(uint16 accessIndex) override;

        const char* GetName(uint16 accessIndex) override;
        const char* GetHelpText(uint16 accessIndex) override;

        EConVarType GetType(uint16 accessIndex) override;

        uint64 GetFlags(uint16 accessIndex) override;
        void SetFlags(uint16 accessIndex, uint64 flags) override;

        void* GetValueAddress(uint16 accessIndex, CSplitScreenSlot slot) override;

        const char* GetString(uint16 accessIndex, CSplitScreenSlot slot) override;
        void SetString(uint16 accessIndex, const char* value, CSplitScreenSlot slot) override;

        bool GetBool(uint16 accessIndex, CSplitScreenSlot slot) override;
        int32 GetInt(uint16 accessIndex, CSplitScreenSlot slot) override;
        float GetFloat(uint16 accessIndex, CSplitScreenSlot slot) override;
        double GetDouble(uint16 accessIndex, CSplitScreenSlot slot) override;

        void SetBool(uint16 accessIndex, bool value, CSplitScreenSlot slot) override;
        void SetInt(uint16 accessIndex, int32 value, CSplitScreenSlot slot) override;
        void SetFloat(uint16 accessIndex, float value, CSplitScreenSlot slot) override;
        void SetDouble(uint16 accessIndex, double value, CSplitScreenSlot slot) override;

        Vector2D GetVector2(uint16 accessIndex, CSplitScreenSlot slot) override;
        Vector GetVector3(uint16 accessIndex, CSplitScreenSlot slot) override;
        Vector4D GetVector4(uint16 accessIndex, CSplitScreenSlot slot) override;
        QAngle GetQAngle(uint16 accessIndex, CSplitScreenSlot slot) override;
        Color GetColor(uint16 accessIndex, CSplitScreenSlot slot) override;

        void SetVector2(uint16 accessIndex, const Vector2D& value, CSplitScreenSlot slot) override;
        void SetVector3(uint16 accessIndex, const Vector& value, CSplitScreenSlot slot) override;
        void SetVector4(uint16 accessIndex, const Vector4D& value, CSplitScreenSlot slot) override;
        void SetQAngle(uint16 accessIndex, const QAngle& value, CSplitScreenSlot slot) override;
        void SetColor(uint16 accessIndex, const Color& value, CSplitScreenSlot slot) override;

        void GetValue(uint16 accessIndex, void* outValue, CSplitScreenSlot slot) override;
        void SetValue(uint16 accessIndex, const void* value, CSplitScreenSlot slot) override;

        uint16 CreateConVar(const char* name, EConVarType type, const char* help, uint64 flags,
                            bool hasMin, bool hasMax,
                            void* defaultValue, const void* minValue, const void* maxValue) override;

        void DeleteConVar(uint16 accessIndex) override;


        Source2ModConVarHookId HookConVarChange(ConVarChangeHandler handler) override { const PluginId owner = hookid::OwnerOfHandler(handler); return HookConVarChange(owner, std::move(handler)); }
        bool UnhookConVarChange(const ConVarChangeHandler& handler) override { return UnhookConVarChange(hookid::OwnerOfHandler(handler), handler); }
        bool UnhookConVarChange(Source2ModConVarHookId id) override { return UnhookConVarChangeId(id); }

        // The same with the owner spelled out: what the calls above resolve
        // to, and what the core itself calls.
        Source2ModConVarHookId HookConVarChange(PluginId owner, ConVarChangeHandler handler);
        bool UnhookConVarChange(PluginId owner, const ConVarChangeHandler& handler);
        bool UnhookConVarChangeId(Source2ModHookId id);

    public:
        void UnlockConVars();

        // Called from the one engine-level callback the manager installs.
        void DispatchConVarChange(ConVarRefAbstract* ref, CSplitScreenSlot slot,
                                  const char* pszNewValue, const char* pszOldValue);

        // A handler is a std::function holding code inside the plugin's library,
        // so it cannot outlive it.
        void RemoveAllForPlugin(PluginId id);
        void Shutdown();

    private:
        void EnsureGlobalCallbackInstalled();
        void RemoveGlobalCallbackIfIdle();

        std::vector<ConVarChangeEntry> m_changeHandlers;
        bool m_bGlobalCallbackInstalled = false;

        inline ConVarRefAbstract GetRef(uint16 idx)
        {
            return ConVarRefAbstract(idx);
        }
    };

    extern ConVarsManager convarsManager;
}
