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

#include "json.h"

namespace json
{
    JSONManager jsonManager;

    /* =========================
       CJSONDocument
       ========================= */

    CJSONDocument::CJSONDocument(const char* pszText, int nLength)
    {
        const std::string sText = (nLength < 0)
            ? std::string(pszText ? pszText : "")
            : std::string(pszText ? pszText : "", static_cast<size_t>(nLength));

        // No exceptions: a bad body is an ordinary outcome for an HTTP reply,
        // not something a callback should have to guard against.
        m_Root = nlohmann::json::parse(sText, nullptr, false);

        if (m_Root.is_discarded())
        {
            m_Root = nlohmann::json();       // reads as null from here on
            m_bValid = false;
            m_sError = "failed to parse JSON";
        }
        else
        {
            m_bValid = true;
        }

        m_pRoot = Wrap(&m_Root);
    }

    const ISource2ModJSONValue* CJSONDocument::Wrap(const nlohmann::json* pNode)
    {
        if (!pNode)
            return nullptr;

        // A deque never moves the elements already in it, so every wrapper
        // handed out stays put for the document's lifetime.
        m_Values.emplace_back(this, pNode);
        return &m_Values.back();
    }

    const char* CJSONDocument::Intern(std::string s)
    {
        m_Strings.emplace_back(std::move(s));
        return m_Strings.back().c_str();
    }

    /* =========================
       CJSONValue
       ========================= */

    bool CJSONValue::IsNull()   const { return !m_pNode || m_pNode->is_null(); }
    bool CJSONValue::IsObject() const { return m_pNode && m_pNode->is_object(); }
    bool CJSONValue::IsArray()  const { return m_pNode && m_pNode->is_array(); }
    bool CJSONValue::IsString() const { return m_pNode && m_pNode->is_string(); }
    bool CJSONValue::IsNumber() const { return m_pNode && m_pNode->is_number(); }
    bool CJSONValue::IsBool()   const { return m_pNode && m_pNode->is_boolean(); }

    bool CJSONValue::Has(const char* pszKey) const
    {
        return pszKey && IsObject() && m_pNode->contains(pszKey);
    }

    const ISource2ModJSONValue* CJSONValue::Get(const char* pszKey) const
    {
        if (!Has(pszKey))
            return nullptr;

        return m_pDoc->Wrap(&m_pNode->at(pszKey));
    }

    int CJSONValue::Count() const
    {
        if (!m_pNode || (!m_pNode->is_array() && !m_pNode->is_object()))
            return 0;

        return static_cast<int>(m_pNode->size());
    }

    const ISource2ModJSONValue* CJSONValue::At(int nIndex) const
    {
        if (!IsArray() || nIndex < 0 || nIndex >= Count())
            return nullptr;

        return m_pDoc->Wrap(&m_pNode->at(static_cast<size_t>(nIndex)));
    }

    const char* CJSONValue::KeyAt(int nIndex) const
    {
        if (!IsObject() || nIndex < 0 || nIndex >= Count())
            return nullptr;

        auto it = m_pNode->begin();
        std::advance(it, nIndex);
        return m_pDoc->Intern(it.key());
    }

    const char* CJSONValue::AsString(const char* pszDefault) const
    {
        if (!IsString())
            return pszDefault;

        return m_pDoc->Intern(m_pNode->get<std::string>());
    }

    int64_t CJSONValue::AsInt64(int64_t nDefault) const
    {
        return IsNumber() ? m_pNode->get<int64_t>() : nDefault;
    }

    int CJSONValue::AsInt(int nDefault) const
    {
        return IsNumber() ? m_pNode->get<int>() : nDefault;
    }

    double CJSONValue::AsDouble(double flDefault) const
    {
        return IsNumber() ? m_pNode->get<double>() : flDefault;
    }

    float CJSONValue::AsFloat(float flDefault) const
    {
        return IsNumber() ? m_pNode->get<float>() : flDefault;
    }

    bool CJSONValue::AsBool(bool bDefault) const
    {
        return IsBool() ? m_pNode->get<bool>() : bDefault;
    }

    const char* CJSONValue::GetString(const char* pszKey, const char* pszDefault) const
    {
        const ISource2ModJSONValue* pChild = Get(pszKey);
        return pChild ? pChild->AsString(pszDefault) : pszDefault;
    }

    int64_t CJSONValue::GetInt64(const char* pszKey, int64_t nDefault) const
    {
        const ISource2ModJSONValue* pChild = Get(pszKey);
        return pChild ? pChild->AsInt64(nDefault) : nDefault;
    }

    int CJSONValue::GetInt(const char* pszKey, int nDefault) const
    {
        const ISource2ModJSONValue* pChild = Get(pszKey);
        return pChild ? pChild->AsInt(nDefault) : nDefault;
    }

    double CJSONValue::GetDouble(const char* pszKey, double flDefault) const
    {
        const ISource2ModJSONValue* pChild = Get(pszKey);
        return pChild ? pChild->AsDouble(flDefault) : flDefault;
    }

    float CJSONValue::GetFloat(const char* pszKey, float flDefault) const
    {
        const ISource2ModJSONValue* pChild = Get(pszKey);
        return pChild ? pChild->AsFloat(flDefault) : flDefault;
    }

    bool CJSONValue::GetBool(const char* pszKey, bool bDefault) const
    {
        const ISource2ModJSONValue* pChild = Get(pszKey);
        return pChild ? pChild->AsBool(bDefault) : bDefault;
    }

    const char* CJSONValue::Dump(int nIndent) const
    {
        if (!m_pNode)
            return "";

        return m_pDoc->Intern(m_pNode->dump(nIndent));
    }

    /* =========================
       JSONManager
       ========================= */

    ISource2ModJSONDocument* JSONManager::Parse(const char* pszText, int nLength)
    {
        return new CJSONDocument(pszText, nLength);
    }
}
