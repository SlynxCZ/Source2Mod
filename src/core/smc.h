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
 * SourceMod's SMC text format (textparse.inc): KeyValues-style sections and
 * key/value pairs, quoted or bare tokens, // and block comments. Used by the
 * SMCParser natives, translations and KeyValues.
 */
#pragma once

#include <string>

namespace smc
{
    // enum SMCResult (textparse.inc)
    enum class Result
    {
        Continue,
        Halt,
        HaltFail,
    };

    // enum SMCError (textparse.inc)
    enum class Error
    {
        Okay = 0,
        StreamOpen,
        StreamError,
        Custom,
        InvalidSection1,   // (not produced)
        InvalidSection2,   // { without a name before it
        InvalidSection3,   // a lone token right before }
        InvalidSection4,   // } with no section open
        InvalidSection5,   // sections still open at the end
        InvalidTokens,     // a quoted string not closed on its line, a lone token at the end
        TokenOverflow,     // (not produced)
        InvalidProperty1,  // a key/value pair outside any section
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void Start() {}
        virtual Result EnterSection(const char* name, bool quoted) { return Result::Continue; }
        virtual Result KeyValue(const char* key, const char* value, bool keyQuoted, bool valueQuoted) { return Result::Continue; }
        virtual Result LeaveSection() { return Result::Continue; }
        virtual Result RawLine(const char* line, int lineNo) { return Result::Continue; }
        virtual void End(bool halted, bool failed) {}
    };

    struct States
    {
        int line = 0;
        int col = 0;
    };

    /// escapes: the \n \r \t \" \\ sequences in quoted strings are read
    /// (KeyValues can turn that off).
    Error ParseString(const std::string& text, Listener& listener, States* states = nullptr, bool escapes = true);
    Error ParseFile(const char* path, Listener& listener, States* states = nullptr, bool escapes = true);

    /// SourceMod's wording; nullptr for a value outside the enum.
    const char* ErrorString(Error error);
}
