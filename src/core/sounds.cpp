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
#include "sounds.h"

#include "plugin.h"
#include "shared.h"
#include "utils/log.h"

#include <engine2/igameeventsystem.h>
#include <entity2/entityinstance.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <networksystem/inetworkmessages.h>
#include <source2mod/schema/netmessages.h>
#include <source2mod/schema/recipientfilter.h>
#include <source2mod/utils/virtual.h>
#include <tier0/generichash.h>

#include <algorithm>
#include <cstring>
#include <map>

#ifdef GetMessage
#undef GetMessage
#endif

namespace sounds
{
    SoundsManager soundsManager;

    namespace
    {
        constexpr const char* PARAM_VOLUME   = "public.volume";
        constexpr const char* PARAM_PITCH    = "public.pitch";
        constexpr const char* PARAM_POSITION = "public.position";

        /// How many sent sounds stay addressable by guid.
        constexpr size_t MAX_ACTIVE_SOUNDS = 256;

        /// Wire layout of one packed parameter: name hash, type, little-endian
        /// 16-bit payload size, payload.
        constexpr size_t PARAM_HEADER_SIZE = 7;

        uint32_t HashParamName(const char* name)
        {
            return name ? MurmurHash2LowerCase(name, SOUNDEVENT_PARAM_NAME_HASH_SEED) : 0;
        }

        size_t ParamTypeSize(SoundParamType type)
        {
            switch (type)
            {
            case SoundParamType::Bool:   return 1;
            case SoundParamType::Int32:  return 4;
            case SoundParamType::UInt32: return 4;
            case SoundParamType::UInt64: return 8;
            case SoundParamType::Float:  return 4;
            case SoundParamType::Float3: return 12;
            default:                     return 0;
            }
        }

        void AppendParam(std::string& out, uint32_t hash, uint8_t type, const uint8_t* data, size_t size)
        {
            const uint16_t size16 = static_cast<uint16_t>(size);

            out.append(reinterpret_cast<const char*>(&hash), sizeof(hash));
            out.push_back(static_cast<char>(type));
            out.append(reinterpret_cast<const char*>(&size16), sizeof(size16));
            out.append(reinterpret_cast<const char*>(data), size);
        }

        /// A sound message as the engine allocates it. The protobuf half is a
        /// second base of CNetMessagePB, so the conversion has to go through
        /// the type rather than through a void*.
        struct SoundMessage
        {
            INetworkMessageInternal* info = nullptr;
            CNetMessagePB<google::protobuf::Message>* net = nullptr;
            google::protobuf::Message* pb = nullptr;

            explicit SoundMessage(int id)
            {
                info = g_pNetworkMessages->FindNetworkMessageById(id);
                if (!info)
                    return;

                net = info->AllocateMessage()->ToPB<google::protobuf::Message>();
                pb = net;
            }

            ~SoundMessage()
            {
                delete net;
            }

            SoundMessage(const SoundMessage&) = delete;
            SoundMessage& operator=(const SoundMessage&) = delete;

            explicit operator bool() const { return pb != nullptr; }
        };

        const google::protobuf::FieldDescriptor* Field(const google::protobuf::Message* pb, const char* name)
        {
            return pb->GetDescriptor()->FindFieldByName(name);
        }

        void SetInt32(google::protobuf::Message* pb, const char* name, int32_t value)
        {
            if (const auto* field = Field(pb, name))
                pb->GetReflection()->SetInt32(pb, field, value);
        }

        void SetUInt32(google::protobuf::Message* pb, const char* name, uint32_t value)
        {
            if (const auto* field = Field(pb, name))
                pb->GetReflection()->SetUInt32(pb, field, value);
        }

        void SetBytes(google::protobuf::Message* pb, const char* name, const std::string& value)
        {
            if (const auto* field = Field(pb, name))
                pb->GetReflection()->SetString(pb, field, value);
        }

        int32_t GetInt32(const google::protobuf::Message* pb, const char* name, int32_t def)
        {
            const auto* field = Field(pb, name);
            return field ? pb->GetReflection()->GetInt32(*pb, field) : def;
        }

        uint32_t GetUInt32(const google::protobuf::Message* pb, const char* name)
        {
            const auto* field = Field(pb, name);
            return field ? pb->GetReflection()->GetUInt32(*pb, field) : 0;
        }

        std::string GetBytes(const google::protobuf::Message* pb, const char* name)
        {
            const auto* field = Field(pb, name);
            return field ? pb->GetReflection()->GetString(*pb, field) : std::string();
        }
    }

    /* =========================
    Source2ModSound: identity and source
    ========================= */

    void Source2ModSound::SetName(const char* name)
    {
        m_name = name ? name : "";
        m_nameHash = soundsManager.HashSoundName(m_name.c_str());
        m_bIdentityDirty = true;
    }

    const char* Source2ModSound::GetName() const
    {
        return m_name.c_str();
    }

    void Source2ModSound::SetNameHash(uint32_t hash)
    {
        m_nameHash = hash;

        const char* name = soundsManager.LookupSoundName(hash);
        m_name = name ? name : "";
        m_bIdentityDirty = true;
    }

    void Source2ModSound::SetSourceEntityIndex(int index)
    {
        m_sourceEntityIndex = index;
        m_bIdentityDirty = true;
    }

    void Source2ModSound::SetSourceEntity(CEntityInstance* entity)
    {
        SetSourceEntityIndex(entity ? entity->GetEntityIndex().Get() : SOUND_SOURCE_LISTENER);
    }

    void Source2ModSound::SetPosition(const Vector& position)
    {
        SetSourceEntityIndex(SOUND_SOURCE_WORLD);
        SetFloat3(PARAM_POSITION, position);
    }

    void Source2ModSound::SetVolume(float volume)
    {
        SetFloat(PARAM_VOLUME, volume);
    }

    float Source2ModSound::GetVolume() const
    {
        return GetFloat(PARAM_VOLUME, 1.0f);
    }

    void Source2ModSound::SetPitch(float pitch)
    {
        SetFloat(PARAM_PITCH, pitch);
    }

    float Source2ModSound::GetPitch() const
    {
        return GetFloat(PARAM_PITCH, 1.0f);
    }

    void Source2ModSound::SetChannel(const char* channel)
    {
        m_channel = channel ? channel : "";
    }

    /* =========================
    Source2ModSound: parameters
    ========================= */

    const SoundParam* Source2ModSound::FindParam(const char* name, SoundParamType type) const
    {
        const uint32_t hash = HashParamName(name);

        for (const SoundParam& param : m_params)
        {
            if (param.hash != hash)
                continue;

            if (type != SoundParamType::Invalid && (param.type != static_cast<uint8_t>(type) || param.data.size() != ParamTypeSize(type)))
                return nullptr;

            return &param;
        }

        return nullptr;
    }

    void Source2ModSound::SetParam(const char* name, SoundParamType type, const void* data, size_t size)
    {
        if (!name || !name[0])
            return;

        // The first write gives up on a blob that could not be parsed: from
        // here on the parameters are whatever this list says.
        m_bRawParams = false;
        m_bParamsDirty = true;

        const uint32_t hash = HashParamName(name);
        const auto* bytes = static_cast<const uint8_t*>(data);

        for (SoundParam& param : m_params)
        {
            if (param.hash == hash)
            {
                param.type = static_cast<uint8_t>(type);
                param.data.assign(bytes, bytes + size);
                return;
            }
        }

        m_params.push_back({ hash, static_cast<uint8_t>(type), std::vector<uint8_t>(bytes, bytes + size) });
    }

    bool Source2ModSound::HasParam(const char* name) const
    {
        return FindParam(name, SoundParamType::Invalid) != nullptr;
    }

    SoundParamType Source2ModSound::GetParamType(const char* name) const
    {
        const SoundParam* param = FindParam(name, SoundParamType::Invalid);
        if (!param)
            return SoundParamType::Invalid;

        const auto type = static_cast<SoundParamType>(param->type);
        return ParamTypeSize(type) == param->data.size() ? type : SoundParamType::Invalid;
    }

    void Source2ModSound::RemoveParam(const char* name)
    {
        const uint32_t hash = HashParamName(name);

        if (std::erase_if(m_params, [hash](const SoundParam& param) { return param.hash == hash; }))
            m_bParamsDirty = true;
    }

    void Source2ModSound::ClearParams()
    {
        m_params.clear();
        m_bRawParams = false;
        m_bParamsDirty = true;
    }

    void Source2ModSound::SetBool(const char* name, bool value)             { SetParam(name, SoundParamType::Bool, &value, sizeof(value)); }
    void Source2ModSound::SetInt32(const char* name, int32_t value)         { SetParam(name, SoundParamType::Int32, &value, sizeof(value)); }
    void Source2ModSound::SetUInt32(const char* name, uint32_t value)       { SetParam(name, SoundParamType::UInt32, &value, sizeof(value)); }
    void Source2ModSound::SetUInt64(const char* name, uint64_t value)       { SetParam(name, SoundParamType::UInt64, &value, sizeof(value)); }
    void Source2ModSound::SetFloat(const char* name, float value)           { SetParam(name, SoundParamType::Float, &value, sizeof(value)); }

    void Source2ModSound::SetFloat3(const char* name, const Vector& value)
    {
        const float xyz[3] = { value.x, value.y, value.z };
        SetParam(name, SoundParamType::Float3, xyz, sizeof(xyz));
    }

    template <typename T>
    static T ReadParam(const SoundParam* param, T def)
    {
        if (!param)
            return def;

        T value;
        std::memcpy(&value, param->data.data(), sizeof(T));
        return value;
    }

    bool Source2ModSound::GetBool(const char* name, bool def) const             { return ReadParam<bool>(FindParam(name, SoundParamType::Bool), def); }
    int32_t Source2ModSound::GetInt32(const char* name, int32_t def) const      { return ReadParam<int32_t>(FindParam(name, SoundParamType::Int32), def); }
    uint32_t Source2ModSound::GetUInt32(const char* name, uint32_t def) const   { return ReadParam<uint32_t>(FindParam(name, SoundParamType::UInt32), def); }
    uint64_t Source2ModSound::GetUInt64(const char* name, uint64_t def) const   { return ReadParam<uint64_t>(FindParam(name, SoundParamType::UInt64), def); }
    float Source2ModSound::GetFloat(const char* name, float def) const          { return ReadParam<float>(FindParam(name, SoundParamType::Float), def); }

    Vector Source2ModSound::GetFloat3(const char* name, const Vector& def) const
    {
        const SoundParam* param = FindParam(name, SoundParamType::Float3);
        if (!param)
            return def;

        float xyz[3];
        std::memcpy(xyz, param->data.data(), sizeof(xyz));
        return Vector(xyz[0], xyz[1], xyz[2]);
    }

    bool Source2ModSound::ParseParams(const std::string& packed)
    {
        m_params.clear();
        m_rawParams.clear();
        m_bRawParams = false;

        const auto* bytes = reinterpret_cast<const uint8_t*>(packed.data());
        const size_t length = packed.size();
        size_t pos = 0;

        while (pos < length)
        {
            uint16_t size = 0;

            if (length - pos >= PARAM_HEADER_SIZE)
                std::memcpy(&size, bytes + pos + 5, sizeof(size));

            if (length - pos < PARAM_HEADER_SIZE || length - pos - PARAM_HEADER_SIZE < size)
            {
                m_params.clear();
                m_rawParams = packed;
                m_bRawParams = true;
                return false;
            }

            SoundParam param;
            std::memcpy(&param.hash, bytes + pos, sizeof(param.hash));
            param.type = bytes[pos + 4];
            param.data.assign(bytes + pos + PARAM_HEADER_SIZE, bytes + pos + PARAM_HEADER_SIZE + size);
            m_params.push_back(std::move(param));

            pos += PARAM_HEADER_SIZE + size;
        }

        return true;
    }

    std::string Source2ModSound::PackParams() const
    {
        if (m_bRawParams)
            return m_rawParams;

        std::string out;
        for (const SoundParam& param : m_params)
            AppendParam(out, param.hash, param.type, param.data.data(), param.data.size());

        return out;
    }

    std::string Source2ModSound::PackParamsWithVolume(float volume) const
    {
        const uint32_t volumeHash = HashParamName(PARAM_VOLUME);

        std::string out;
        for (const SoundParam& param : m_params)
        {
            if (param.hash != volumeHash)
                AppendParam(out, param.hash, param.type, param.data.data(), param.data.size());
        }

        AppendParam(out, volumeHash, static_cast<uint8_t>(SoundParamType::Float), reinterpret_cast<const uint8_t*>(&volume), sizeof(volume));
        return out;
    }

    /* =========================
    Source2ModSound: recipients
    ========================= */

    void Source2ModSound::AddRecipient(CPlayerSlot slot)
    {
        m_recipients |= SoundRecipientOf(slot);
        m_bRecipientsDirty = true;
    }

    void Source2ModSound::RemoveRecipient(CPlayerSlot slot)
    {
        m_recipients &= ~SoundRecipientOf(slot);
        m_bRecipientsDirty = true;
    }

    bool Source2ModSound::HasRecipient(CPlayerSlot slot) const
    {
        return (m_recipients & SoundRecipientOf(slot)) != 0;
    }

    void Source2ModSound::AddAllRecipients()
    {
        m_recipients |= SoundsManager::AllPlayers();
        m_bRecipientsDirty = true;
    }

    void Source2ModSound::ClearRecipients()
    {
        m_recipients = SOUND_RECIPIENTS_NONE;
        m_bRecipientsDirty = true;
    }

    void Source2ModSound::SetRecipients(SoundRecipients mask)
    {
        m_recipients = mask;
        m_bRecipientsDirty = true;
    }

    SoundGuid Source2ModSound::Emit()
    {
        // The hooked view is of a message the engine is in the middle of
        // sending; it goes out when the handler returns.
        if (m_bHooked)
            return 0;

        return soundsManager.Emit(*this);
    }

    /* =========================
    Setup
    ========================= */

    void SoundsManager::Init()
    {
        m_nTakeGuidOffset = shared::g_pGameConfig ? shared::g_pGameConfig->GetOffset("CSoundSystem::TakeGuid") : -1;

        if (!g_pSoundSystem || m_nTakeGuidOffset < 0)
            FP_WARN("Sounds: {} not available, sound guids come from a private counter", !g_pSoundSystem ? SOUNDSYSTEM_INTERFACE_VERSION : "CSoundSystem::TakeGuid");
    }

    SoundGuid SoundsManager::TakeGuid()
    {
        if (!g_pSoundSystem || m_nTakeGuidOffset < 0)
        {
            // High enough that the engine's counter, which starts at 1 every
            // map, does not get there within one.
            if (++m_nFallbackGuid >= 0x7FFFFFFF)
                m_nFallbackGuid = 0x40000001;

            return m_nFallbackGuid;
        }

#ifdef _WIN32
        // Returned in a struct, which MSVC passes back through a hidden pointer.
        uint32_t guid = 0;
        CALL_VIRTUAL(void, m_nTakeGuidOffset, g_pSoundSystem, &guid);
        return guid;
#else
        return CALL_VIRTUAL(uint32_t, m_nTakeGuidOffset, g_pSoundSystem);
#endif
    }

    SoundRecipients SoundsManager::AllPlayers()
    {
        SoundRecipients mask = SOUND_RECIPIENTS_NONE;

        CGlobalVars* globals = shared::getGlobalVars();
        const int maxClients = globals ? std::min(globals->maxClients, 64) : 0;

        // A net channel is what tells a player from a bot: a sound message has
        // nowhere to go without one.
        for (int i = 0; i < maxClients; i++)
        {
            if (g_pEngineServer->GetPlayerNetInfo(CPlayerSlot(i)))
                mask |= SoundRecipients(1) << i;
        }

        return mask;
    }

    /* =========================
    Sound objects
    ========================= */

    ISource2ModSound* SoundsManager::CreateSound(PluginId owner, const char* name)
    {
        auto* sound = new Source2ModSound(owner);
        if (name && name[0])
            sound->SetName(name);

        m_sounds.push_back(sound);
        return sound;
    }

    void SoundsManager::DestroySound(ISource2ModSound* sound)
    {
        // Same thing a plugin's own `delete sound` does; the destructor unlists it.
        delete static_cast<Source2ModSound*>(sound);
    }

    void SoundsManager::Forget(Source2ModSound* sound)
    {
        std::erase(m_sounds, sound);
    }

    Source2ModSound::~Source2ModSound()
    {
        soundsManager.Forget(this);
    }

    /* =========================
    Sending
    ========================= */

    void SoundsManager::SendStart(const Source2ModSound& sound, SoundGuid guid, const std::string& packedParams, SoundRecipients recipients)
    {
        SoundMessage msg(GE_SosStartSoundEvent);
        if (!msg || !recipients)
            return;

        SetUInt32(msg.pb, "soundevent_hash", sound.GetNameHash());
        SetInt32(msg.pb, "source_entity_index", sound.GetSourceEntityIndex());
        SetInt32(msg.pb, "soundevent_guid", static_cast<int32_t>(guid));
        SetInt32(msg.pb, "seed", static_cast<int32_t>(guid));
        SetBytes(msg.pb, "packed_params", packedParams);

        CRecipientFilter filter;
        filter.SetFromBitmask(recipients);

        m_bSending = true;
        shared::g_pGameEventSystem->PostEventAbstract(-1, false, &filter, msg.info, msg.net, 0);
        m_bSending = false;
    }

    void SoundsManager::SendStop(SoundGuid guid, SoundRecipients recipients)
    {
        SoundMessage msg(GE_SosStopSoundEvent);
        if (!msg || !recipients)
            return;

        SetInt32(msg.pb, "soundevent_guid", static_cast<int32_t>(guid));

        CRecipientFilter filter;
        filter.SetFromBitmask(recipients);

        m_bSending = true;
        shared::g_pGameEventSystem->PostEventAbstract(-1, false, &filter, msg.info, msg.net, 0);
        m_bSending = false;
    }

    void SoundsManager::SendParams(SoundGuid guid, const std::string& packedParams, SoundRecipients recipients)
    {
        SoundMessage msg(GE_SosSetSoundEventParams);
        if (!msg || !recipients || packedParams.empty())
            return;

        SetInt32(msg.pb, "soundevent_guid", static_cast<int32_t>(guid));
        SetBytes(msg.pb, "packed_params", packedParams);

        CRecipientFilter filter;
        filter.SetFromBitmask(recipients);

        m_bSending = true;
        shared::g_pGameEventSystem->PostEventAbstract(-1, false, &filter, msg.info, msg.net, 0);
        m_bSending = false;
    }

    void SoundsManager::SendVolume(SoundGuid guid, float volume, SoundRecipients recipients)
    {
        std::string packed;
        AppendParam(packed, HashParamName(PARAM_VOLUME), static_cast<uint8_t>(SoundParamType::Float), reinterpret_cast<const uint8_t*>(&volume), sizeof(volume));

        SendParams(guid, packed, recipients);
    }

    float SoundsManager::ChannelVolume(int slot, const std::string& channel) const
    {
        if (slot < 0 || slot >= 64 || channel.empty())
            return 1.0f;

        const auto it = m_volumes[slot].find(channel);
        return it != m_volumes[slot].end() ? it->second : 1.0f;
    }

    ActiveSound* SoundsManager::FindActive(SoundGuid guid)
    {
        for (ActiveSound& sound : m_active)
        {
            if (sound.guid == guid)
                return &sound;
        }

        return nullptr;
    }

    SoundGuid SoundsManager::Emit(Source2ModSound& sound)
    {
        if (!sound.GetNameHash() || !sound.GetRecipients() || !shared::g_pGameEventSystem)
            return 0;

        const std::string channel = sound.GetChannel();
        const float volume = sound.GetVolume();

        // No channel: one message for everybody. On a channel every recipient
        // has a volume of their own, so the sound goes out once per distinct
        // volume -- same guid, since no client ever sees more than one copy.
        std::map<float, SoundRecipients> groups;
        SoundRecipients sent = SOUND_RECIPIENTS_NONE;

        if (!channel.empty())
        {
            for (int i = 0; i < 64; i++)
            {
                const SoundRecipients bit = SoundRecipients(1) << i;
                if (!(sound.GetRecipients() & bit))
                    continue;

                const float playerVolume = ChannelVolume(i, channel);
                if (playerVolume <= 0.0f)
                    continue;

                groups[volume * playerVolume] |= bit;
                sent |= bit;
            }

            if (!sent)
                return 0;
        }
        else
        {
            sent = sound.GetRecipients();
        }

        const SoundGuid guid = TakeGuid();

        if (channel.empty())
        {
            SendStart(sound, guid, sound.PackParams(), sent);
        }
        else
        {
            for (const auto& [groupVolume, recipients] : groups)
                SendStart(sound, guid, sound.PackParamsWithVolume(groupVolume), recipients);
        }

        m_active.push_back({ guid, channel, volume, sent });
        if (m_active.size() > MAX_ACTIVE_SOUNDS)
            m_active.pop_front();

        sound.m_guid = guid;
        return guid;
    }

    /* =========================
    One-call emit
    ========================= */

    SoundGuid SoundsManager::EmitSoundToPlayer(CPlayerSlot slot, const char* name, float volume, float pitch, const char* channel)
    {
        return EmitSoundToPlayers(SoundRecipientOf(slot), name, volume, pitch, channel);
    }

    SoundGuid SoundsManager::EmitSoundToPlayers(SoundRecipients recipients, const char* name, float volume, float pitch, const char* channel)
    {
        Source2ModSound sound(0);
        sound.SetName(name);
        sound.SetVolume(volume);
        sound.SetPitch(pitch);
        sound.SetChannel(channel);
        sound.SetRecipients(recipients);

        return Emit(sound);
    }

    SoundGuid SoundsManager::EmitSoundToAll(const char* name, float volume, float pitch, const char* channel)
    {
        return EmitSoundToPlayers(AllPlayers(), name, volume, pitch, channel);
    }

    SoundGuid SoundsManager::EmitSoundFromEntity(CEntityInstance* entity, const char* name, float volume, float pitch, SoundRecipients recipients, const char* channel)
    {
        if (!entity)
            return 0;

        Source2ModSound sound(0);
        sound.SetName(name);
        sound.SetSourceEntity(entity);
        sound.SetVolume(volume);
        sound.SetPitch(pitch);
        sound.SetChannel(channel);
        sound.SetRecipients(recipients & AllPlayers());

        return Emit(sound);
    }

    /* =========================
    Stopping and changing a playing sound
    ========================= */

    void SoundsManager::StopSound(SoundGuid guid)
    {
        if (!guid)
            return;

        // A guid that has dropped out of the list is stopped for everybody;
        // a client that never had it ignores the message.
        const ActiveSound* active = FindActive(guid);
        SendStop(guid, active ? active->recipients : AllPlayers());

        std::erase_if(m_active, [guid](const ActiveSound& sound) { return sound.guid == guid; });
    }

    void SoundsManager::StopSoundForPlayers(SoundGuid guid, SoundRecipients recipients)
    {
        if (!guid || !recipients)
            return;

        SendStop(guid, recipients);

        if (ActiveSound* active = FindActive(guid))
        {
            active->recipients &= ~recipients;

            if (!active->recipients)
                std::erase_if(m_active, [guid](const ActiveSound& sound) { return sound.guid == guid; });
        }
    }

    void SoundsManager::StopSoundByName(const char* name, int sourceEntityIndex, SoundRecipients recipients)
    {
        if (!name || !name[0])
            return;

        SoundMessage msg(GE_SosStopSoundEventHash);
        recipients &= AllPlayers();

        if (!msg || !recipients)
            return;

        SetUInt32(msg.pb, "soundevent_hash", HashSoundName(name));
        SetInt32(msg.pb, "source_entity_index", sourceEntityIndex);

        CRecipientFilter filter;
        filter.SetFromBitmask(recipients);

        m_bSending = true;
        shared::g_pGameEventSystem->PostEventAbstract(-1, false, &filter, msg.info, msg.net, 0);
        m_bSending = false;
    }

    void SoundsManager::SetSoundVolume(SoundGuid guid, float volume)
    {
        if (!guid)
            return;

        ActiveSound* active = FindActive(guid);
        if (!active)
        {
            SendVolume(guid, volume, AllPlayers());
            return;
        }

        active->volume = volume;

        if (active->channel.empty())
        {
            SendVolume(guid, volume, active->recipients);
            return;
        }

        std::map<float, SoundRecipients> groups;
        for (int i = 0; i < 64; i++)
        {
            const SoundRecipients bit = SoundRecipients(1) << i;
            if (active->recipients & bit)
                groups[volume * ChannelVolume(i, active->channel)] |= bit;
        }

        for (const auto& [groupVolume, recipients] : groups)
            SendVolume(guid, groupVolume, recipients);
    }

    void SoundsManager::UpdateSoundParams(SoundGuid guid, const ISource2ModSound* params, SoundRecipients recipients)
    {
        if (!guid || !params)
            return;

        if (!recipients)
        {
            const ActiveSound* active = FindActive(guid);
            recipients = active ? active->recipients : AllPlayers();
        }

        SendParams(guid, static_cast<const Source2ModSound*>(params)->PackParams(), recipients);
    }

    /* =========================
    Volume channels
    ========================= */

    void SoundsManager::SetPlayerVolume(CPlayerSlot slot, const char* channel, float volume)
    {
        const int i = slot.Get();
        if (i < 0 || i >= 64 || !channel || !channel[0])
            return;

        volume = std::max(volume, 0.0f);
        m_volumes[i][channel] = volume;

        // Whatever this player is hearing on the channel right now follows.
        const SoundRecipients bit = SoundRecipients(1) << i;

        for (ActiveSound& active : m_active)
        {
            if (!(active.recipients & bit) || active.channel != channel)
                continue;

            if (volume <= 0.0f)
            {
                SendStop(active.guid, bit);
                active.recipients &= ~bit;
            }
            else
            {
                SendVolume(active.guid, active.volume * volume, bit);
            }
        }

        std::erase_if(m_active, [](const ActiveSound& sound) { return !sound.recipients; });
    }

    float SoundsManager::GetPlayerVolume(CPlayerSlot slot, const char* channel)
    {
        return ChannelVolume(slot.Get(), channel ? channel : "");
    }

    void SoundsManager::ResetPlayerVolumes(CPlayerSlot slot)
    {
        const int i = slot.Get();
        if (i < 0 || i >= 64)
            return;

        // Through SetPlayerVolume() so the sounds still playing come back up too.
        const auto volumes = m_volumes[i];
        for (const auto& [channel, volume] : volumes)
        {
            if (volume != 1.0f)
                SetPlayerVolume(slot, channel.c_str(), 1.0f);
        }

        m_volumes[i].clear();
    }

    /* =========================
    Names and hashes
    ========================= */

    uint32_t SoundsManager::HashSoundName(const char* name)
    {
        if (!name || !name[0])
            return 0;

        // Remembered, because this is the only direction the hash works in: a
        // hooked sound can only be named if somebody has asked for it by name.
        const uint32_t hash = MurmurHash2LowerCase(name, SOUNDEVENT_NAME_HASH_SEED);
        m_names.try_emplace(hash, name);

        return hash;
    }

    const char* SoundsManager::LookupSoundName(uint32_t hash)
    {
        const auto it = m_names.find(hash);
        return it != m_names.end() ? it->second.c_str() : nullptr;
    }

    /* =========================
    Hooks
    ========================= */

    Source2ModHookId SoundsManager::HookSound(PluginId owner, SoundHook handler)
    {
        return m_hooks.Add(owner, std::move(handler));
    }

    bool SoundsManager::UnhookSound(PluginId owner, const SoundHook& handler)
    {
        return m_hooks.RemoveHandler(owner, handler);
    }

    bool SoundsManager::UnhookSoundId(Source2ModHookId id)
    {
        return m_hooks.RemoveId(id);
    }

    Action SoundsManager::DispatchSoundHook(uint64_t* clients, void* msg)
    {
        if (m_bSending || m_hooks.empty() || !msg)
            return Action::Ignore;

        google::protobuf::Message* pb = static_cast<CNetMessage*>(msg)->ToPB<google::protobuf::Message>();

        Source2ModSound sound(0);
        sound.SetNameHash(GetUInt32(pb, "soundevent_hash"));
        sound.SetSourceEntityIndex(GetInt32(pb, "source_entity_index", SOUND_SOURCE_LISTENER));
        sound.ParseParams(GetBytes(pb, "packed_params"));
        sound.SetRecipients(clients ? *clients : SOUND_RECIPIENTS_NONE);
        sound.m_guid = static_cast<SoundGuid>(GetInt32(pb, "soundevent_guid", 0));

        sound.m_bHooked = true;
        sound.m_bIdentityDirty = false;
        sound.m_bParamsDirty = false;
        sound.m_bRecipientsDirty = false;

        // Copied first: a handler is free to unhook itself, or to load a
        // plugin that hooks.
        std::vector<SoundHook> handlers = m_hooks.Snapshot();

        Action result = Action::Ignore;
        for (SoundHook& handler : handlers)
        {
            const Action action = handler(&sound);
            if (action > result)
                result = action;
        }

        if (result == Action::Supersede)
            return result;

        if (result == Action::Override)
        {
            if (sound.m_bIdentityDirty)
            {
                SetUInt32(pb, "soundevent_hash", sound.GetNameHash());
                SetInt32(pb, "source_entity_index", sound.GetSourceEntityIndex());
            }

            if (sound.m_bParamsDirty)
                SetBytes(pb, "packed_params", sound.PackParams());

            if (sound.m_bRecipientsDirty && clients)
                *clients = sound.GetRecipients();

            // Nobody left to send it to is the same as blocking it.
            if (clients && !*clients)
                return Action::Supersede;
        }

        return result;
    }

    /* =========================
    Housekeeping
    ========================= */

    void SoundsManager::OnClientDisconnect(CPlayerSlot slot)
    {
        const int i = slot.Get();
        if (i < 0 || i >= 64)
            return;

        // The next player in this slot starts from their own settings, and
        // must not be sent a stop or a volume for a sound they never had.
        m_volumes[i].clear();

        const SoundRecipients bit = SoundRecipients(1) << i;
        for (ActiveSound& active : m_active)
            active.recipients &= ~bit;

        std::erase_if(m_active, [](const ActiveSound& sound) { return !sound.recipients; });
    }

    void SoundsManager::RemoveAllForPlugin(PluginId id)
    {
        // The handler is a std::function holding code inside the plugin's
        // library, so it has to go before that library is closed.
        m_hooks.RemoveOwner(id);

        // Copied first: each delete takes the sound off m_sounds.
        std::vector<Source2ModSound*> owned;
        for (Source2ModSound* sound : m_sounds)
        {
            if (sound->m_owner == id)
                owned.push_back(sound);
        }

        for (Source2ModSound* sound : owned)
            delete sound;
    }

    void SoundsManager::Clear()
    {
        m_active.clear();
    }
}
