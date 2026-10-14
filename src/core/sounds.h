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
#include "hookid.h"

#include <source2mod/ISource2ModSounds.h>

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sounds
{
    /// Game event ids of the sound messages (gameevents.proto, EBaseGameEvents).
    inline constexpr int GE_SosStartSoundEvent     = 208;
    inline constexpr int GE_SosStopSoundEvent      = 209;
    inline constexpr int GE_SosSetSoundEventParams = 210;
    inline constexpr int GE_SosStopSoundEventHash  = 212;

    /// Seeds the engine hashes sound event names and parameter names with.
    inline constexpr uint32_t SOUNDEVENT_NAME_HASH_SEED       = 0x53524332;
    inline constexpr uint32_t SOUNDEVENT_PARAM_NAME_HASH_SEED = 0x31415926;

    /// One entry of a packed parameter block, kept in wire form: the name is
    /// only ever a hash, and a type this code does not know the meaning of is
    /// still a run of `data.size()` bytes that can be written back as it came.
    struct SoundParam
    {
        uint32_t hash = 0;
        uint8_t type = 0;
        std::vector<uint8_t> data;
    };

    class Source2ModSound : public ISource2ModSound
    {
    public:
        explicit Source2ModSound(PluginId owner) : m_owner(owner) {}
        ~Source2ModSound() override;

        void SetName(const char* name) override;
        const char* GetName() const override;
        void SetNameHash(uint32_t hash) override;
        uint32_t GetNameHash() const override { return m_nameHash; }
        SoundGuid GetGuid() const override { return m_guid; }

        void SetSourceEntityIndex(int index) override;
        int GetSourceEntityIndex() const override { return m_sourceEntityIndex; }
        void SetSourceEntity(CEntityInstance* entity) override;
        void SetPosition(const Vector& position) override;

        void SetVolume(float volume) override;
        float GetVolume() const override;
        void SetPitch(float pitch) override;
        float GetPitch() const override;

        void SetChannel(const char* channel) override;
        const char* GetChannel() const override { return m_channel.c_str(); }

        bool HasParam(const char* name) const override;
        SoundParamType GetParamType(const char* name) const override;
        void RemoveParam(const char* name) override;
        void ClearParams() override;

        void SetBool(const char* name, bool value) override;
        void SetInt32(const char* name, int32_t value) override;
        void SetUInt32(const char* name, uint32_t value) override;
        void SetUInt64(const char* name, uint64_t value) override;
        void SetFloat(const char* name, float value) override;
        void SetFloat3(const char* name, const Vector& value) override;

        bool GetBool(const char* name, bool def) const override;
        int32_t GetInt32(const char* name, int32_t def) const override;
        uint32_t GetUInt32(const char* name, uint32_t def) const override;
        uint64_t GetUInt64(const char* name, uint64_t def) const override;
        float GetFloat(const char* name, float def) const override;
        Vector GetFloat3(const char* name, const Vector& def) const override;

        void AddRecipient(CPlayerSlot slot) override;
        void RemoveRecipient(CPlayerSlot slot) override;
        bool HasRecipient(CPlayerSlot slot) const override;
        void AddAllRecipients() override;
        void ClearRecipients() override;
        void SetRecipients(SoundRecipients mask) override;
        SoundRecipients GetRecipients() const override { return m_recipients; }

        SoundGuid Emit() override;

    public:
        /// Reads a packed_params blob. False when it does not follow the
        /// layout this code knows; the sound then keeps the bytes as they are
        /// and only gives them up once a parameter is actually written.
        bool ParseParams(const std::string& packed);
        std::string PackParams() const;

        /// PackParams() with "public.volume" replaced, for the per-recipient
        /// copies of a sound on a volume channel.
        std::string PackParamsWithVolume(float volume) const;

        PluginId m_owner = 0;

        /// Set on the view a hook handler gets: that sound is already being
        /// sent, so Emit() on it is refused.
        bool m_bHooked = false;
        /// What a handler changed, so an Override only rewrites that much.
        bool m_bIdentityDirty = false;
        bool m_bParamsDirty = false;
        bool m_bRecipientsDirty = false;

        SoundGuid m_guid = 0;

    private:
        const SoundParam* FindParam(const char* name, SoundParamType type) const;
        void SetParam(const char* name, SoundParamType type, const void* data, size_t size);

        std::string m_name;
        uint32_t m_nameHash = 0;
        int m_sourceEntityIndex = SOUND_SOURCE_LISTENER;
        std::string m_channel;
        SoundRecipients m_recipients = SOUND_RECIPIENTS_NONE;

        std::vector<SoundParam> m_params;
        /// The blob as it arrived, for a hooked sound whose parameters could
        /// not be parsed.
        std::string m_rawParams;
        bool m_bRawParams = false;
    };

    /// A sound this interface sent and may still be asked to stop or re-volume.
    struct ActiveSound
    {
        SoundGuid guid = 0;
        std::string channel;
        float volume = 1.0f;
        SoundRecipients recipients = SOUND_RECIPIENTS_NONE;
    };

    class SoundsManager : public ISource2ModSounds
    {
    public:
        ISource2ModSound* CreateSound(PluginId owner, const char* name) override;
        void DestroySound(ISource2ModSound* sound) override;

        SoundGuid EmitSoundToPlayer(CPlayerSlot slot, const char* name, float volume, float pitch, const char* channel) override;
        SoundGuid EmitSoundToPlayers(SoundRecipients recipients, const char* name, float volume, float pitch, const char* channel) override;
        SoundGuid EmitSoundToAll(const char* name, float volume, float pitch, const char* channel) override;
        SoundGuid EmitSoundFromEntity(CEntityInstance* entity, const char* name, float volume, float pitch, SoundRecipients recipients, const char* channel) override;

        void StopSound(SoundGuid guid) override;
        void StopSoundForPlayers(SoundGuid guid, SoundRecipients recipients) override;
        void StopSoundByName(const char* name, int sourceEntityIndex, SoundRecipients recipients) override;
        void SetSoundVolume(SoundGuid guid, float volume) override;
        void UpdateSoundParams(SoundGuid guid, const ISource2ModSound* params, SoundRecipients recipients) override;

        void SetPlayerVolume(CPlayerSlot slot, const char* channel, float volume) override;
        float GetPlayerVolume(CPlayerSlot slot, const char* channel) override;
        void ResetPlayerVolumes(CPlayerSlot slot) override;

        uint32_t HashSoundName(const char* name) override;
        const char* LookupSoundName(uint32_t hash) override;


        Source2ModHookId HookSound(SoundHook handler) override { const PluginId owner = hookid::OwnerOfHandler(handler); return HookSound(owner, std::move(handler)); }
        bool UnhookSound(const SoundHook& handler) override { return UnhookSound(hookid::OwnerOfHandler(handler), handler); }
        bool UnhookSound(Source2ModHookId id) override { return UnhookSoundId(id); }

        // The same with the owner spelled out: what the calls above resolve
        // to, and what the core itself calls.
        Source2ModHookId HookSound(PluginId owner, SoundHook handler);
        bool UnhookSound(PluginId owner, const SoundHook& handler);
        bool UnhookSoundId(Source2ModHookId id);

    public:
        /// Resolves the engine's sound system, which is where guids come from.
        void Init();

        SoundGuid Emit(Source2ModSound& sound);

        /// Called from the PostEventAbstract hook for a CMsgSosStartSoundEvent
        /// the game is sending. May rewrite @p msg and @p clients.
        Action DispatchSoundHook(uint64_t* clients, void* msg);

        /// Every recipient bit of a connected, human player.
        static SoundRecipients AllPlayers();

        void OnClientDisconnect(CPlayerSlot slot);

        /// Called by ~Source2ModSound().
        void Forget(Source2ModSound* sound);

        // Called when a plugin unloads, like every other plugin-owned registry.
        void RemoveAllForPlugin(PluginId id);
        /// Level change: nothing that was playing survives it on the clients.
        void Clear();

    private:
        SoundGuid TakeGuid();
        void SendStart(const Source2ModSound& sound, SoundGuid guid, const std::string& packedParams, SoundRecipients recipients);
        void SendStop(SoundGuid guid, SoundRecipients recipients);
        void SendParams(SoundGuid guid, const std::string& packedParams, SoundRecipients recipients);
        void SendVolume(SoundGuid guid, float volume, SoundRecipients recipients);

        ActiveSound* FindActive(SoundGuid guid);
        float ChannelVolume(int slot, const std::string& channel) const;

        /// The sounds plugins made and have not deleted yet. Not owned: a plugin
        /// releases a sound with a plain `delete`, and the sound takes itself
        /// off this list in its destructor. What is still here when the plugin
        /// unloads is deleted for it.
        std::vector<Source2ModSound*> m_sounds;
        hookid::HookList<SoundHook> m_hooks;
        std::unordered_map<uint32_t, std::string> m_names;

        /// Per slot, channel name -> volume. Missing means 1.0.
        std::unordered_map<std::string, float> m_volumes[64];

        /// Newest at the back. Nothing says when a sound has finished on a
        /// client, so this is simply the last so many that were sent; a stop or
        /// a volume change for a sound that is already over is ignored there.
        std::deque<ActiveSound> m_active;

        int m_nTakeGuidOffset = -1;
        uint32_t m_nFallbackGuid = 0x40000000;

        /// Set while this interface is sending, so its own messages do not
        /// come back through DispatchSoundHook().
        bool m_bSending = false;
    };

    extern SoundsManager soundsManager;
}
