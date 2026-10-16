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
 * The SMC text format reader (see smc.h).
 */
#include "smc.h"

#include <fstream>
#include <sstream>

namespace smc
{
    namespace
    {
        struct Token
        {
            std::string text;
            bool quoted = false;
        };

        class Parser
        {
        public:
            Parser(Listener& listener, bool escapes) : m_listener(listener), m_escapes(escapes) {}

            Error Run(const std::string& text, States* states)
            {
                m_listener.Start();
                Error err = Error::Okay;
                bool halted = false;

                size_t pos = 0;
                while (pos <= text.size() && err == Error::Okay && !halted)
                {
                    size_t end = text.find('\n', pos);
                    if (end == std::string::npos)
                        end = text.size();
                    std::string line = text.substr(pos, end - pos);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    ++m_line;

                    if (!Handle(m_listener.RawLine(line.c_str(), m_line), err, halted))
                        break;
                    err = Line(line, halted);

                    if (end == text.size())
                        break;
                    pos = end + 1;
                }

                if (err == Error::Okay && !halted)
                {
                    if (m_pending)
                        err = Error::InvalidTokens;
                    else if (m_depth > 0)
                        err = Error::InvalidSection5;
                }

                if (states)
                {
                    states->line = m_line;
                    states->col = m_col;
                }
                m_listener.End(halted, err != Error::Okay);
                return err;
            }

        private:
            /// Applies a listener result; false when parsing stops.
            bool Handle(Result r, Error& err, bool& halted)
            {
                if (r == Result::Continue)
                    return true;
                halted = true;
                if (r == Result::HaltFail)
                    err = Error::Custom;
                return false;
            }

            Error Line(const std::string& line, bool& halted)
            {
                Error err = Error::Okay;
                size_t i = 0;
                const size_t n = line.size();
                while (i < n)
                {
                    m_col = static_cast<int>(i) + 1;
                    if (m_inComment)
                    {
                        const size_t close = line.find("*/", i);
                        if (close == std::string::npos)
                            return Error::Okay;
                        m_inComment = false;
                        i = close + 2;
                        continue;
                    }

                    const char c = line[i];
                    if (c == ' ' || c == '\t' || c == '\v' || c == '\f')
                    {
                        ++i;
                        continue;
                    }
                    if (c == '/' && i + 1 < n && line[i + 1] == '/')
                        return Error::Okay;
                    if (c == '/' && i + 1 < n && line[i + 1] == '*')
                    {
                        m_inComment = true;
                        i += 2;
                        continue;
                    }

                    if (c == '{')
                    {
                        if (!m_pending)
                            return Error::InvalidSection2;
                        m_pending = false;
                        ++m_depth;
                        if (!Handle(m_listener.EnterSection(m_key.text.c_str(), m_key.quoted), err, halted))
                            return err;
                        ++i;
                        continue;
                    }
                    if (c == '}')
                    {
                        if (m_pending)
                            return Error::InvalidSection3;
                        if (m_depth == 0)
                            return Error::InvalidSection4;
                        --m_depth;
                        if (!Handle(m_listener.LeaveSection(), err, halted))
                            return err;
                        ++i;
                        continue;
                    }

                    Token tok;
                    if (c == '"')
                    {
                        tok.quoted = true;
                        ++i;
                        bool closed = false;
                        while (i < n)
                        {
                            char ch = line[i];
                            if (ch == '"')
                            {
                                closed = true;
                                ++i;
                                break;
                            }
                            if (m_escapes && ch == '\\' && i + 1 < n)
                            {
                                const char e = line[i + 1];
                                switch (e)
                                {
                                case 'n': tok.text += '\n'; break;
                                case 'r': tok.text += '\r'; break;
                                case 't': tok.text += '\t'; break;
                                case '\\': tok.text += '\\'; break;
                                case '"': tok.text += '"'; break;
                                default: tok.text += '\\'; tok.text += e; break;
                                }
                                i += 2;
                                continue;
                            }
                            tok.text += ch;
                            ++i;
                        }
                        if (!closed)
                            return Error::InvalidTokens;
                    }
                    else
                    {
                        while (i < n)
                        {
                            const char ch = line[i];
                            if (ch == ' ' || ch == '\t' || ch == '{' || ch == '}' || ch == '"' ||
                                (ch == '/' && i + 1 < n && (line[i + 1] == '/' || line[i + 1] == '*')))
                                break;
                            tok.text += ch;
                            ++i;
                        }
                    }

                    if (m_pending)
                    {
                        m_pending = false;
                        if (m_depth == 0)
                            return Error::InvalidProperty1;
                        if (!Handle(m_listener.KeyValue(m_key.text.c_str(), tok.text.c_str(), m_key.quoted, tok.quoted), err, halted))
                            return err;
                    }
                    else
                    {
                        m_key = std::move(tok);
                        m_pending = true;
                    }
                }
                return Error::Okay;
            }

            Listener& m_listener;
            bool m_escapes;
            Token m_key;
            bool m_pending = false;
            bool m_inComment = false;
            int m_depth = 0;
            int m_line = 0;
            int m_col = 0;
        };
    }

    Error ParseString(const std::string& text, Listener& listener, States* states, bool escapes)
    {
        Parser parser(listener, escapes);
        return parser.Run(text, states);
    }

    Error ParseFile(const char* path, Listener& listener, States* states, bool escapes)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            if (states)
                *states = {};
            return Error::StreamOpen;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        std::string text = ss.str();
        // A UTF-8 byte order mark is not a token.
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF)
            text.erase(0, 3);
        return ParseString(text, listener, states, escapes);
    }

    const char* ErrorString(Error error)
    {
        switch (error)
        {
        case Error::Okay: return "No error";
        case Error::StreamOpen: return "Stream failed to open";
        case Error::StreamError: return "Stream returned read error";
        case Error::Custom: return "A custom handler threw an error";
        case Error::InvalidSection1: return "A section was declared without quotes, and had extra tokens";
        case Error::InvalidSection2: return "A section was declared without any header";
        case Error::InvalidSection3: return "A section ending was declared with too many unknown tokens";
        case Error::InvalidSection4: return "A section ending has no matching beginning";
        case Error::InvalidSection5: return "A section beginning has no matching ending";
        case Error::InvalidTokens: return "There were too many unidentifiable strings on one line";
        case Error::TokenOverflow: return "The token buffer overflowed";
        case Error::InvalidProperty1: return "A property was declared outside of any section";
        }
        return nullptr;
    }
}
