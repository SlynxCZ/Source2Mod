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
#include "networkmessages.h"

#include <engine2/igameeventsystem.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <networksystem/inetworkmessages.h>
#include <source2mod/schema/netmessages.h>
#include <source2mod/schema/recipientfilter.h>

#include <cstring>

#ifdef GetMessage
#undef GetMessage
#endif

/* =========================
Internal helpers / macros
========================= */

#define GETCHECK_FIELD(return_value)                                                                          \
    if (!msg)                                                                                                  \
        return return_value;                                                                                   \
    const google::protobuf::FieldDescriptor* field = msg->GetDescriptor()->FindFieldByName(fieldName);        \
    if (!field)                                                                                                \
        return return_value;

#define GETCHECK_FIELD_VOID()                                                                                 \
    if (!msg)                                                                                                  \
        return;                                                                                                \
    const google::protobuf::FieldDescriptor* field = msg->GetDescriptor()->FindFieldByName(fieldName);        \
    if (!field)                                                                                                \
        return;

#define CHECK_FIELD_NOT_REPEATED_VOID()                                                                       \
    if (field->label() == google::protobuf::FieldDescriptor::LABEL_REPEATED)                                  \
        return;

#define CHECK_FIELD_NOT_REPEATED(return_value)                                                                \
    if (field->label() == google::protobuf::FieldDescriptor::LABEL_REPEATED)                                  \
        return return_value;

#define CHECK_FIELD_REPEATED_VOID()                                                                           \
    if (field->label() != google::protobuf::FieldDescriptor::LABEL_REPEATED)                                  \
        return;

#define CHECK_FIELD_REPEATED(return_value)                                                                    \
    if (field->label() != google::protobuf::FieldDescriptor::LABEL_REPEATED)                                  \
        return return_value;

#define CHECK_REPEATED_ELEMENT_VOID(idx)                                                                      \
    int elemCount = msg->GetReflection()->FieldSize(*msg, field);                                             \
    if (elemCount == 0 || idx >= elemCount || idx < 0)                                                        \
        return;

#define CHECK_REPEATED_ELEMENT(idx, return_value)                                                             \
    int elemCount = msg->GetReflection()->FieldSize(*msg, field);                                             \
    if (elemCount == 0 || idx >= elemCount || idx < 0)                                                        \
        return return_value;

namespace networkmessages
{
    NetworkMessagesManager networkMessagesManager;

    /* =========================
    Handles
    ========================= */

    // The void* a plugin holds is always the google::protobuf::Message -- what
    // the interface documents, what every accessor below reads, and the only
    // thing a nested message can be. The engine's object is a CNetMessagePB,
    // which has the protobuf as its *second* base, so the two addresses differ
    // and the conversion has to go through the type in both directions; a cast
    // through void* lands 48 bytes short of the message.
    static void* ToHandle(CNetMessage* msg)
    {
        if (!msg)
            return nullptr;

        google::protobuf::Message* pb = msg->ToPB<google::protobuf::Message>();
        return pb;
    }

    static CNetMessagePB<google::protobuf::Message>* FromHandle(void* handle)
    {
        return handle ? static_cast<CNetMessagePB<google::protobuf::Message>*>(static_cast<google::protobuf::Message*>(handle)) : nullptr;
    }

    /* =========================
    Allocation
    ========================= */

    void* NetworkMessagesManager::AllocateNetMessageByID(int msgid)
    {
        auto* netmsg = g_pNetworkMessages->FindNetworkMessageById(msgid);
        if (!netmsg)
            return nullptr;
        return ToHandle(netmsg->AllocateMessage());
    }

    void* NetworkMessagesManager::AllocateNetMessageByPartialName(const char* name)
    {
        auto* netmsg = g_pNetworkMessages->FindNetworkMessagePartial(name);
        if (!netmsg)
            return nullptr;
        return ToHandle(netmsg->AllocateMessage());
    }

    void NetworkMessagesManager::DeallocateNetMessage(void* pmsg)
    {
        if (!pmsg)
            return;
        delete FromHandle(pmsg);
    }

    /* =========================
    Field presence
    ========================= */

    bool NetworkMessagesManager::HasField(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(false);
        CHECK_FIELD_NOT_REPEATED(false);
        return msg->GetReflection()->HasField(*msg, field);
    }

    /* =========================
    int32
    ========================= */

    int NetworkMessagesManager::GetInt32(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_NOT_REPEATED(0);

        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
            return msg->GetReflection()->GetEnum(*msg, field)->number();

        return msg->GetReflection()->GetInt32(*msg, field);
    }

    int NetworkMessagesManager::GetRepeatedInt32(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_REPEATED(0);
        CHECK_REPEATED_ELEMENT(index, 0);

        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
            return msg->GetReflection()->GetRepeatedEnum(*msg, field, index)->number();

        return msg->GetReflection()->GetRepeatedInt32(*msg, field, index);
    }

    void NetworkMessagesManager::SetInt32(void* pmsg, const char* fieldName, int value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();

        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
        {
            const google::protobuf::EnumValueDescriptor* pEnumValue = field->enum_type()->FindValueByNumber(value);
            if (!pEnumValue)
                return;
            msg->GetReflection()->SetEnum(msg, field, pEnumValue);
        }
        else
        {
            msg->GetReflection()->SetInt32(msg, field, value);
        }
    }

    void NetworkMessagesManager::SetRepeatedInt32(void* pmsg, const char* fieldName, int index, int value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);

        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
        {
            const google::protobuf::EnumValueDescriptor* pEnumValue = field->enum_type()->FindValueByNumber(value);
            if (!pEnumValue)
                return;
            msg->GetReflection()->SetRepeatedEnum(msg, field, index, pEnumValue);
        }
        else
        {
            msg->GetReflection()->SetRepeatedInt32(msg, field, index, value);
        }
    }

    void NetworkMessagesManager::AddInt32(void* pmsg, const char* fieldName, int value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();

        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
        {
            const google::protobuf::EnumValueDescriptor* pEnumValue = field->enum_type()->FindValueByNumber(value);
            if (!pEnumValue)
                return;
            msg->GetReflection()->AddEnum(msg, field, pEnumValue);
        }
        else
        {
            msg->GetReflection()->AddInt32(msg, field, value);
        }
    }

    /* =========================
    int64
    ========================= */

    int64_t NetworkMessagesManager::GetInt64(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_NOT_REPEATED(0);
        return msg->GetReflection()->GetInt64(*msg, field);
    }

    int64_t NetworkMessagesManager::GetRepeatedInt64(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_REPEATED(0);
        CHECK_REPEATED_ELEMENT(index, 0);
        return msg->GetReflection()->GetRepeatedInt64(*msg, field, index);
    }

    void NetworkMessagesManager::SetInt64(void* pmsg, const char* fieldName, int64_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();
        msg->GetReflection()->SetInt64(msg, field, value);
    }

    void NetworkMessagesManager::SetRepeatedInt64(void* pmsg, const char* fieldName, int index, int64_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);
        msg->GetReflection()->SetRepeatedInt64(msg, field, index, value);
    }

    void NetworkMessagesManager::AddInt64(void* pmsg, const char* fieldName, int64_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->AddInt64(msg, field, value);
    }

    /* =========================
    uint32
    ========================= */

    uint32_t NetworkMessagesManager::GetUInt32(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_NOT_REPEATED(0);
        return msg->GetReflection()->GetUInt32(*msg, field);
    }

    uint32_t NetworkMessagesManager::GetRepeatedUInt32(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_REPEATED(0);
        CHECK_REPEATED_ELEMENT(index, 0);
        return msg->GetReflection()->GetRepeatedUInt32(*msg, field, index);
    }

    void NetworkMessagesManager::SetUInt32(void* pmsg, const char* fieldName, uint32_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();
        msg->GetReflection()->SetUInt32(msg, field, value);
    }

    void NetworkMessagesManager::SetRepeatedUInt32(void* pmsg, const char* fieldName, int index, uint32_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);
        msg->GetReflection()->SetRepeatedUInt32(msg, field, index, value);
    }

    void NetworkMessagesManager::AddUInt32(void* pmsg, const char* fieldName, uint32_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->AddUInt32(msg, field, value);
    }

    /* =========================
    uint64
    ========================= */

    uint64_t NetworkMessagesManager::GetUInt64(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_NOT_REPEATED(0);
        return msg->GetReflection()->GetUInt64(*msg, field);
    }

    uint64_t NetworkMessagesManager::GetRepeatedUInt64(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_REPEATED(0);
        CHECK_REPEATED_ELEMENT(index, 0);
        return msg->GetReflection()->GetRepeatedUInt64(*msg, field, index);
    }

    void NetworkMessagesManager::SetUInt64(void* pmsg, const char* fieldName, uint64_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();
        msg->GetReflection()->SetUInt64(msg, field, value);
    }

    void NetworkMessagesManager::SetRepeatedUInt64(void* pmsg, const char* fieldName, int index, uint64_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);
        msg->GetReflection()->SetRepeatedUInt64(msg, field, index, value);
    }

    void NetworkMessagesManager::AddUInt64(void* pmsg, const char* fieldName, uint64_t value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->AddUInt64(msg, field, value);
    }

    /* =========================
    bool
    ========================= */

    bool NetworkMessagesManager::GetBool(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(false);
        CHECK_FIELD_NOT_REPEATED(false);
        return msg->GetReflection()->GetBool(*msg, field);
    }

    bool NetworkMessagesManager::GetRepeatedBool(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(false);
        CHECK_FIELD_REPEATED(false);
        CHECK_REPEATED_ELEMENT(index, false);
        return msg->GetReflection()->GetRepeatedBool(*msg, field, index);
    }

    void NetworkMessagesManager::SetBool(void* pmsg, const char* fieldName, bool value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();
        msg->GetReflection()->SetBool(msg, field, value);
    }

    void NetworkMessagesManager::SetRepeatedBool(void* pmsg, const char* fieldName, int index, bool value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);
        msg->GetReflection()->SetRepeatedBool(msg, field, index, value);
    }

    void NetworkMessagesManager::AddBool(void* pmsg, const char* fieldName, bool value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->AddBool(msg, field, value);
    }

    /* =========================
    float
    ========================= */

    float NetworkMessagesManager::GetFloat(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0.0f);
        CHECK_FIELD_NOT_REPEATED(0.0f);
        return msg->GetReflection()->GetFloat(*msg, field);
    }

    float NetworkMessagesManager::GetRepeatedFloat(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0.0f);
        CHECK_FIELD_REPEATED(0.0f);
        CHECK_REPEATED_ELEMENT(index, 0.0f);
        return msg->GetReflection()->GetRepeatedFloat(*msg, field, index);
    }

    void NetworkMessagesManager::SetFloat(void* pmsg, const char* fieldName, float value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();
        msg->GetReflection()->SetFloat(msg, field, value);
    }

    void NetworkMessagesManager::SetRepeatedFloat(void* pmsg, const char* fieldName, int index, float value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);
        msg->GetReflection()->SetRepeatedFloat(msg, field, index, value);
    }

    void NetworkMessagesManager::AddFloat(void* pmsg, const char* fieldName, float value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->AddFloat(msg, field, value);
    }

    /* =========================
    double
    ========================= */

    double NetworkMessagesManager::GetDouble(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0.0);
        CHECK_FIELD_NOT_REPEATED(0.0);
        return msg->GetReflection()->GetDouble(*msg, field);
    }

    double NetworkMessagesManager::GetRepeatedDouble(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0.0);
        CHECK_FIELD_REPEATED(0.0);
        CHECK_REPEATED_ELEMENT(index, 0.0);
        return msg->GetReflection()->GetRepeatedDouble(*msg, field, index);
    }

    void NetworkMessagesManager::SetDouble(void* pmsg, const char* fieldName, double value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();
        msg->GetReflection()->SetDouble(msg, field, value);
    }

    void NetworkMessagesManager::SetRepeatedDouble(void* pmsg, const char* fieldName, int index, double value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);
        msg->GetReflection()->SetRepeatedDouble(msg, field, index, value);
    }

    void NetworkMessagesManager::AddDouble(void* pmsg, const char* fieldName, double value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->AddDouble(msg, field, value);
    }

    /* =========================
    string
    ========================= */

    std::string NetworkMessagesManager::GetString(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD({});
        CHECK_FIELD_NOT_REPEATED({});
        return msg->GetReflection()->GetString(*msg, field);
    }

    std::string NetworkMessagesManager::GetRepeatedString(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD({});
        CHECK_FIELD_REPEATED({});
        CHECK_REPEATED_ELEMENT(index, {});
        return msg->GetReflection()->GetRepeatedString(*msg, field, index);
    }

    void NetworkMessagesManager::SetString(void* pmsg, const char* fieldName, const char* value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();
        msg->GetReflection()->SetString(msg, field, value);
    }

    void NetworkMessagesManager::SetRepeatedString(void* pmsg, const char* fieldName, int index, const char* value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);
        msg->GetReflection()->SetRepeatedString(msg, field, index, value);
    }

    void NetworkMessagesManager::AddString(void* pmsg, const char* fieldName, const char* value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->AddString(msg, field, value);
    }

    /* =========================
    Vector2D
    ========================= */

    Vector2D NetworkMessagesManager::GetVector2D(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        Vector2D vec{0.0f, 0.0f};
        GETCHECK_FIELD(vec);
        CHECK_FIELD_NOT_REPEATED(vec);

        const CMsgVector2D* msgVec2d = (const CMsgVector2D*)&msg->GetReflection()->GetMessage(*msg, field);
        vec.x = msgVec2d->x();
        vec.y = msgVec2d->y();
        return vec;
    }

    Vector2D NetworkMessagesManager::GetRepeatedVector2D(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        Vector2D vec{0.0f, 0.0f};
        GETCHECK_FIELD(vec);
        CHECK_FIELD_REPEATED(vec);
        CHECK_REPEATED_ELEMENT(index, vec);

        const CMsgVector2D* msgVec2d = (const CMsgVector2D*)&msg->GetReflection()->GetRepeatedMessage(
            *msg, field, index);
        vec.x = msgVec2d->x();
        vec.y = msgVec2d->y();
        return vec;
    }

    void NetworkMessagesManager::SetVector2D(void* pmsg, const char* fieldName, Vector2D value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();

        CMsgVector2D* msgVec2d = (CMsgVector2D*)msg->GetReflection()->MutableMessage(msg, field);
        msgVec2d->set_x(value.x);
        msgVec2d->set_y(value.y);
    }

    void NetworkMessagesManager::SetRepeatedVector2D(void* pmsg, const char* fieldName, int index, Vector2D value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);

        CMsgVector2D* msgVec2d = (CMsgVector2D*)msg->GetReflection()->MutableRepeatedMessage(msg, field, index);
        msgVec2d->set_x(value.x);
        msgVec2d->set_y(value.y);
    }

    void NetworkMessagesManager::AddVector2D(void* pmsg, const char* fieldName, Vector2D value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();

        CMsgVector2D* msgVec2d = (CMsgVector2D*)msg->GetReflection()->AddMessage(msg, field);
        msgVec2d->set_x(value.x);
        msgVec2d->set_y(value.y);
    }

    /* =========================
    Vector
    ========================= */

    Vector NetworkMessagesManager::GetVector(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        Vector vec{0.0f, 0.0f, 0.0f};
        GETCHECK_FIELD(vec);
        CHECK_FIELD_NOT_REPEATED(vec);

        const CMsgVector* msgVec = (const CMsgVector*)&msg->GetReflection()->GetMessage(*msg, field);
        vec.x = msgVec->x();
        vec.y = msgVec->y();
        vec.z = msgVec->z();
        return vec;
    }

    Vector NetworkMessagesManager::GetRepeatedVector(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        Vector vec{0.0f, 0.0f, 0.0f};
        GETCHECK_FIELD(vec);
        CHECK_FIELD_REPEATED(vec);
        CHECK_REPEATED_ELEMENT(index, vec);

        const CMsgVector* msgVec = (const CMsgVector*)&msg->GetReflection()->GetRepeatedMessage(*msg, field, index);
        vec.x = msgVec->x();
        vec.y = msgVec->y();
        vec.z = msgVec->z();
        return vec;
    }

    void NetworkMessagesManager::SetVector(void* pmsg, const char* fieldName, Vector value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();

        CMsgVector* msgVec = (CMsgVector*)msg->GetReflection()->MutableMessage(msg, field);
        msgVec->set_x(value.x);
        msgVec->set_y(value.y);
        msgVec->set_z(value.z);
    }

    void NetworkMessagesManager::SetRepeatedVector(void* pmsg, const char* fieldName, int index, Vector value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);

        CMsgVector* msgVec = (CMsgVector*)msg->GetReflection()->MutableRepeatedMessage(msg, field, index);
        msgVec->set_x(value.x);
        msgVec->set_y(value.y);
        msgVec->set_z(value.z);
    }

    void NetworkMessagesManager::AddVector(void* pmsg, const char* fieldName, Vector value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();

        CMsgVector* msgVec = (CMsgVector*)msg->GetReflection()->AddMessage(msg, field);
        msgVec->set_x(value.x);
        msgVec->set_y(value.y);
        msgVec->set_z(value.z);
    }

    /* =========================
    Color
    ========================= */

    Color NetworkMessagesManager::GetColor(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        Color color{255, 255, 255, 255};
        GETCHECK_FIELD(color);
        CHECK_FIELD_NOT_REPEATED(color);

        const CMsgRGBA* msgColor = (const CMsgRGBA*)&msg->GetReflection()->GetMessage(*msg, field);
        color.SetColor(msgColor->r(), msgColor->g(), msgColor->b(), msgColor->a());
        return color;
    }

    Color NetworkMessagesManager::GetRepeatedColor(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        Color color{255, 255, 255, 255};
        GETCHECK_FIELD(color);
        CHECK_FIELD_REPEATED(color);
        CHECK_REPEATED_ELEMENT(index, color);

        const CMsgRGBA* msgColor = (const CMsgRGBA*)&msg->GetReflection()->GetRepeatedMessage(*msg, field, index);
        color.SetColor(msgColor->r(), msgColor->g(), msgColor->b(), msgColor->a());
        return color;
    }

    void NetworkMessagesManager::SetColor(void* pmsg, const char* fieldName, Color value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();

        CMsgRGBA* msgColor = (CMsgRGBA*)msg->GetReflection()->MutableMessage(msg, field);
        msgColor->set_r(value.r());
        msgColor->set_g(value.g());
        msgColor->set_b(value.b());
        msgColor->set_a(value.a());
    }

    void NetworkMessagesManager::SetRepeatedColor(void* pmsg, const char* fieldName, int index, Color value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);

        CMsgRGBA* msgColor = (CMsgRGBA*)msg->GetReflection()->MutableRepeatedMessage(msg, field, index);
        msgColor->set_r(value.r());
        msgColor->set_g(value.g());
        msgColor->set_b(value.b());
        msgColor->set_a(value.a());
    }

    void NetworkMessagesManager::AddColor(void* pmsg, const char* fieldName, Color value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();

        CMsgRGBA* msgColor = (CMsgRGBA*)msg->GetReflection()->AddMessage(msg, field);
        msgColor->set_r(value.r());
        msgColor->set_g(value.g());
        msgColor->set_b(value.b());
        msgColor->set_a(value.a());
    }

    /* =========================
    QAngle
    ========================= */

    QAngle NetworkMessagesManager::GetQAngle(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        QAngle angle{0.0f, 0.0f, 0.0f};
        GETCHECK_FIELD(angle);
        CHECK_FIELD_NOT_REPEATED(angle);

        const CMsgQAngle* msgAngle = (const CMsgQAngle*)&msg->GetReflection()->GetMessage(*msg, field);
        angle.x = msgAngle->x();
        angle.y = msgAngle->y();
        angle.z = msgAngle->z();
        return angle;
    }

    QAngle NetworkMessagesManager::GetRepeatedQAngle(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        QAngle angle{0.0f, 0.0f, 0.0f};
        GETCHECK_FIELD(angle);
        CHECK_FIELD_REPEATED(angle);
        CHECK_REPEATED_ELEMENT(index, angle);

        const CMsgQAngle* msgAngle = (const CMsgQAngle*)&msg->GetReflection()->GetRepeatedMessage(*msg, field, index);
        angle.x = msgAngle->x();
        angle.y = msgAngle->y();
        angle.z = msgAngle->z();
        return angle;
    }

    void NetworkMessagesManager::SetQAngle(void* pmsg, const char* fieldName, QAngle value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();

        CMsgQAngle* msgAngle = (CMsgQAngle*)msg->GetReflection()->MutableMessage(msg, field);
        msgAngle->set_x(value.x);
        msgAngle->set_y(value.y);
        msgAngle->set_z(value.z);
    }

    void NetworkMessagesManager::SetRepeatedQAngle(void* pmsg, const char* fieldName, int index, QAngle value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);

        CMsgQAngle* msgAngle = (CMsgQAngle*)msg->GetReflection()->MutableRepeatedMessage(msg, field, index);
        msgAngle->set_x(value.x);
        msgAngle->set_y(value.y);
        msgAngle->set_z(value.z);
    }

    void NetworkMessagesManager::AddQAngle(void* pmsg, const char* fieldName, QAngle value)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();

        CMsgQAngle* msgAngle = (CMsgQAngle*)msg->GetReflection()->AddMessage(msg, field);
        msgAngle->set_x(value.x);
        msgAngle->set_y(value.y);
        msgAngle->set_z(value.z);
    }

    /* =========================
    bytes
    ========================= */

    int NetworkMessagesManager::GetBytes(uint8_t* out, void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_NOT_REPEATED(0);

        std::string s = msg->GetReflection()->GetString(*msg, field);
        if (out)
            std::memcpy(out, s.data(), s.size());
        return (int)s.size();
    }

    int NetworkMessagesManager::GetRepeatedBytes(uint8_t* out, void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        CHECK_FIELD_REPEATED(0);
        CHECK_REPEATED_ELEMENT(index, 0);

        std::string s = msg->GetReflection()->GetRepeatedString(*msg, field, index);
        if (out)
            std::memcpy(out, s.data(), s.size());
        return (int)s.size();
    }

    void NetworkMessagesManager::SetBytes(void* pmsg, const char* fieldName, const char* value, int valueLength)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_NOT_REPEATED_VOID();
        msg->GetReflection()->SetString(msg, field, std::string(value, (size_t)valueLength));
    }

    void NetworkMessagesManager::SetRepeatedBytes(void* pmsg, const char* fieldName, int index, const char* value,
                                                  int valueLength)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        CHECK_REPEATED_ELEMENT_VOID(index);
        msg->GetReflection()->SetRepeatedString(msg, field, index, std::string(value, (size_t)valueLength));
    }

    void NetworkMessagesManager::AddBytes(void* pmsg, const char* fieldName, const char* value, int valueLength)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->AddString(msg, field, std::string(value, (size_t)valueLength));
    }

    /* =========================
    Nested messages
    ========================= */

    void* NetworkMessagesManager::GetNestedMessage(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(nullptr);
        CHECK_FIELD_NOT_REPEATED(nullptr);
        return (void*)msg->GetReflection()->MutableMessage(msg, field);
    }

    void* NetworkMessagesManager::GetRepeatedNestedMessage(void* pmsg, const char* fieldName, int index)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(nullptr);
        CHECK_FIELD_REPEATED(nullptr);
        CHECK_REPEATED_ELEMENT(index, nullptr);
        return (void*)msg->GetReflection()->MutableRepeatedMessage(msg, field, index);
    }

    void* NetworkMessagesManager::AddNestedMessage(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(nullptr);
        CHECK_FIELD_REPEATED(nullptr);
        return (void*)msg->GetReflection()->AddMessage(msg, field);
    }

    /* =========================
    Repeated field utilities
    ========================= */

    int NetworkMessagesManager::GetRepeatedFieldSize(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD(0);
        return msg->GetReflection()->FieldSize(*msg, field);
    }

    void NetworkMessagesManager::ClearRepeatedField(void* pmsg, const char* fieldName)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        GETCHECK_FIELD_VOID();
        CHECK_FIELD_REPEATED_VOID();
        msg->GetReflection()->ClearField(msg, field);
    }

    void NetworkMessagesManager::Clear(void* pmsg)
    {
        google::protobuf::Message* msg = (google::protobuf::Message*)pmsg;
        if (msg)
            msg->Clear();
    }

    /* =========================
    Sending
    ========================= */

    void NetworkMessagesManager::SendMessage(void* pmsg, int msgid, CPlayerSlot slot)
    {
        CNetMessagePB<google::protobuf::Message>* msg = FromHandle(pmsg);
        if (!msg)
            return;

        auto* netmsg = g_pNetworkMessages->FindNetworkMessageById(msgid);
        if (!netmsg)
            return;

        CSingleRecipientFilter filter(slot);
        shared::g_pGameEventSystem->PostEventAbstract(-1, false, &filter, netmsg, msg, 0);
    }

    void NetworkMessagesManager::SendMessageToPlayers(void* pmsg, int msgid, uint64_t playermask)
    {
        CNetMessagePB<google::protobuf::Message>* msg = FromHandle(pmsg);
        if (!msg)
            return;

        auto* netmsg = g_pNetworkMessages->FindNetworkMessageById(msgid);
        if (!netmsg)
            return;

        CRecipientFilter filter;
        filter.SetFromBitmask(playermask);

        shared::g_pGameEventSystem->PostEventAbstract(-1, false, &filter, netmsg, msg, 0);
    }

    /* =========================
    Hooks
    ========================= */

    Source2ModNetMessageHookId NetworkMessagesManager::HookServerMessage(PluginId owner, NetMessageServerHook handler)
    {
        return m_serverHooks.Add(owner, std::move(handler));
    }

    bool NetworkMessagesManager::UnhookServerMessage(PluginId owner, const NetMessageServerHook& handler)
    {
        return m_serverHooks.RemoveHandler(owner, handler);
    }

    Source2ModNetMessageHookId NetworkMessagesManager::HookClientMessage(PluginId owner, NetMessageClientHook handler)
    {
        return m_clientHooks.Add(owner, std::move(handler));
    }

    bool NetworkMessagesManager::UnhookClientMessage(PluginId owner, const NetMessageClientHook& handler)
    {
        return m_clientHooks.RemoveHandler(owner, handler);
    }

    Source2ModNetMessageHookId NetworkMessagesManager::HookServerInternalMessage(PluginId owner, NetMessageClientHook handler)
    {
        return m_serverInternalHooks.Add(owner, std::move(handler));
    }

    bool NetworkMessagesManager::UnhookServerInternalMessage(PluginId owner, const NetMessageClientHook& handler)
    {
        return m_serverInternalHooks.RemoveHandler(owner, handler);
    }

    bool NetworkMessagesManager::UnhookNetMessage(Source2ModHookId id)
    {
        return m_serverHooks.RemoveId(id) || m_clientHooks.RemoveId(id) || m_serverInternalHooks.RemoveId(id);
    }

    void NetworkMessagesManager::RemoveAllForPlugin(PluginId id)
    {
        // Without this the engine would keep calling a std::function whose
        // target lives in a library that has just been closed.
        m_serverHooks.RemoveOwner(id);
        m_clientHooks.RemoveOwner(id);
        m_serverInternalHooks.RemoveOwner(id);
    }

    /* =========================
    Dispatch helpers (called from hook sites)
    ========================= */

    Action DispatchServerHook(uint64_t* clients, int messageid, CNetMessage* netmsg)
    {
        void* msg = ToHandle(netmsg);

        Action result = Action::Ignore;
        for (auto& cb : networkMessagesManager.m_serverHooks.Snapshot())
        {
            Action a = cb(clients, messageid, msg);
            if (a > result)
                result = a;
        }
        return result;
    }

    Action DispatchClientHook(CPlayerSlot slot, int messageid, CNetMessage* netmsg)
    {
        void* msg = ToHandle(netmsg);

        Action result = Action::Ignore;
        for (auto& cb : networkMessagesManager.m_clientHooks.Snapshot())
        {
            Action a = cb(slot, messageid, msg);
            if (a > result)
                result = a;
        }
        return result;
    }

    Action DispatchServerInternalHook(CPlayerSlot slot, int messageid, CNetMessage* netmsg)
    {
        void* msg = ToHandle(netmsg);

        Action result = Action::Ignore;
        for (auto& cb : networkMessagesManager.m_serverInternalHooks.Snapshot())
        {
            Action a = cb(slot, messageid, msg);
            if (a > result)
                result = a;
        }
        return result;
    }
}
