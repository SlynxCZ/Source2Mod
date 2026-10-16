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
 * Text, time, regex and file logging helpers (ISource2ModUtils).
 */
#pragma once

#include <source2mod/ISource2ModUtils.h>

#include <regex>
#include <string>
#include <vector>

namespace utils
{
    class Regex final : public ISource2ModRegex
    {
    public:
        /// @p lineAnchors: ^ and $ also match at line breaks (MULTILINE where
        /// the library has no std::regex::multiline).
        Regex(std::regex re, bool lineAnchors) : m_re(std::move(re)), m_lineAnchors(lineAnchors) {}

        int Match(const char* text, size_t offset = 0) override;
        int MatchAll(const char* text) override;
        int MatchCount() override { return static_cast<int>(m_matches.size()); }
        int CaptureCount(int match = 0) override;
        const char* GetSubString(int index, int match = 0) override;
        size_t MatchEnd(int match = 0) override;
        void Release() override { delete this; }

    private:
        struct Found
        {
            std::vector<std::string> groups;   // whole match + captures
            size_t end = 0;
        };

        /// The leftmost match at or after @p from; @p base is where m's positions count from.
        bool Search(const std::string& subject, size_t from, std::smatch& m, size_t& base) const;
        void Keep(const std::smatch& m, size_t base);

        std::regex m_re;
        bool m_lineAnchors = false;
        std::vector<Found> m_matches;
    };

    class UtilsManager final : public ISource2ModUtils
    {
    public:
        std::vector<std::string> Explode(const char* text, const char* separator, int maxParts = 0) override;
        std::string Implode(const std::vector<std::string>& parts, const char* separator) override;
        int Replace(std::string& text, const char* search, const char* replace, bool caseSensitive = true) override;
        std::string Trim(const char* text) override;
        int Find(const char* text, const char* search, bool caseSensitive = true) override;
        bool StrEqual(const char* a, const char* b, bool caseSensitive = true) override;

        int64_t GetTime() override;
        std::string FormatTime(const char* format, int64_t stamp = -1) override;
        int64_t ParseTime(const char* text, const char* format) override;
        int64_t GetTickCount() override;

        ISource2ModRegex* CompileRegex(const char* pattern, int flags = 0, std::string* error = nullptr) override;

        bool LogToFile(PluginId owner, const char* path, const char* message) override;
    };

    extern UtilsManager utilsManager;
}
