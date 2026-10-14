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

#ifndef _INCLUDE_SOURCE2MOD_CORE_JSON_H
#define _INCLUDE_SOURCE2MOD_CORE_JSON_H

#pragma once

#include <nlohmann/json.hpp>
#include <source2mod/ISource2ModJSON.h>

#include <deque>
#include <string>

namespace json
{
    class CJSONDocument;

    /// One node, backed by a reference into the document's parsed tree.
    ///
    /// Nodes are created on demand and kept alive by the document, so the
    /// pointers a plugin gets stay valid until Release(). Every accessor is
    /// total: a wrong-type read gives the caller's default.
    class CJSONValue final : public ISource2ModJSONValue
    {
    public:
        CJSONValue(CJSONDocument* pDoc, const nlohmann::json* pNode)
            : m_pDoc(pDoc), m_pNode(pNode) {}

        bool IsNull() const override;
        bool IsObject() const override;
        bool IsArray() const override;
        bool IsString() const override;
        bool IsNumber() const override;
        bool IsBool() const override;

        bool Has(const char* pszKey) const override;
        const ISource2ModJSONValue* Get(const char* pszKey) const override;
        int Count() const override;
        const ISource2ModJSONValue* At(int nIndex) const override;
        const char* KeyAt(int nIndex) const override;

        const char* AsString(const char* pszDefault = "") const override;
        int64_t AsInt64(int64_t nDefault = 0) const override;
        int AsInt(int nDefault = 0) const override;
        double AsDouble(double flDefault = 0.0) const override;
        float AsFloat(float flDefault = 0.0f) const override;
        bool AsBool(bool bDefault = false) const override;

        const char* GetString(const char* pszKey, const char* pszDefault = "") const override;
        int64_t GetInt64(const char* pszKey, int64_t nDefault = 0) const override;
        int GetInt(const char* pszKey, int nDefault = 0) const override;
        double GetDouble(const char* pszKey, double flDefault = 0.0) const override;
        float GetFloat(const char* pszKey, float flDefault = 0.0f) const override;
        bool GetBool(const char* pszKey, bool bDefault = false) const override;

        const char* Dump(int nIndent = -1) const override;

    private:
        CJSONDocument* m_pDoc;
        const nlohmann::json* m_pNode;
    };

    class CJSONDocument final : public ISource2ModJSONDocument
    {
    public:
        CJSONDocument(const char* pszText, int nLength);

        bool IsValid() const override { return m_bValid; }
        const char* GetError() const override { return m_sError.c_str(); }
        const ISource2ModJSONValue* Root() const override { return m_pRoot; }
        void Release() override { delete this; }

        /// Wraps a node, reusing the wrapper if this node was handed out
        /// before. The deque is what keeps every wrapper alive for the
        /// document's lifetime, and never reallocates its elements.
        const ISource2ModJSONValue* Wrap(const nlohmann::json* pNode);

        /// Strings handed out as const char* have to outlive the call.
        const char* Intern(std::string s);

    private:
        nlohmann::json m_Root;
        bool m_bValid = false;
        std::string m_sError;

        std::deque<CJSONValue> m_Values;
        std::deque<std::string> m_Strings;
        const ISource2ModJSONValue* m_pRoot = nullptr;
    };

    class JSONManager final : public ISource2ModJSON
    {
    public:
        ISource2ModJSONDocument* Parse(const char* pszText, int nLength = -1) override;
    };

    extern JSONManager jsonManager;
}

#endif //_INCLUDE_SOURCE2MOD_CORE_JSON_H
