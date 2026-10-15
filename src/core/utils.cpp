/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Text, time, regex and file logging helpers (see utils.h).
 *
 * Project: Source2Mod
 */
#include "utils.h"

#include "files.h"
#include "pluginmanager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <type_traits>

#if defined(_WIN32)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

namespace utils
{
    UtilsManager utilsManager;

    namespace
    {
        std::tm LocalTime(std::time_t t)
        {
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            return tm;
        }

        size_t FindIn(const std::string& text, const std::string& search, size_t from, bool caseSensitive)
        {
            if (caseSensitive)
                return text.find(search, from);
            for (size_t i = from; i + search.size() <= text.size(); i++)
                if (strncasecmp(text.c_str() + i, search.c_str(), search.size()) == 0)
                    return i;
            return std::string::npos;
        }

        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        // std::regex::multiline is C++17 but missing from older libstdc++.
        template <typename R, typename = void>
        struct HasMultiline : std::false_type {};
        template <typename R>
        struct HasMultiline<R, std::void_t<decltype(R::multiline)>> : std::true_type {};

        template <typename R>
        void AddMultiline(typename R::flag_type& syntax)
        {
            if constexpr (HasMultiline<R>::value)
                syntax |= R::multiline;
        }

        /// MULTILINE without std::regex::multiline: an unescaped $ outside a
        /// class becomes "before a line break or at the end" (a lookahead).
        /// ^ is handled by Regex::Search, which starts a search at each line.
        std::string LineEnds(const std::string& pattern)
        {
            std::string out;
            bool inClass = false;
            for (size_t i = 0; i < pattern.size(); i++)
            {
                const char c = pattern[i];
                if (c == '\\' && i + 1 < pattern.size())
                {
                    out += c;
                    out += pattern[++i];
                    continue;
                }
                if (c == '[')
                    inClass = true;
                else if (c == ']')
                    inClass = false;
                out += (c == '$' && !inClass) ? std::string("(?=\\n|$)") : std::string(1, c);
            }
            return out;
        }

        /// DOTALL: an unescaped . outside a class matches newlines too.
        std::string DotAll(const std::string& pattern)
        {
            std::string out;
            bool inClass = false;
            for (size_t i = 0; i < pattern.size(); i++)
            {
                const char c = pattern[i];
                if (c == '\\' && i + 1 < pattern.size())
                {
                    out += c;
                    out += pattern[++i];
                    continue;
                }
                if (c == '[')
                    inClass = true;
                else if (c == ']')
                    inClass = false;
                out += (c == '.' && !inClass) ? std::string("[\\s\\S]") : std::string(1, c);
            }
            return out;
        }

        /// The usual pattern mistakes, found without compiling: std::regex
        /// reports them by throwing, and in the game process that exception
        /// comes out of the game's own (newer) libstdc++, whose regex_error a
        /// catch built against ours does not always match -- so a bad pattern
        /// from a plugin aborted the server. Empty when nothing is wrong.
        std::string CheckPattern(const std::string& p)
        {
            int parens = 0;
            bool inClass = false;
            bool canRepeat = false;   // something before a quantifier
            for (size_t i = 0; i < p.size(); i++)
            {
                const char c = p[i];
                if (c == '\\')
                {
                    if (i + 1 >= p.size())
                        return "Pattern ends with a lone backslash";
                    i++;
                    canRepeat = true;
                    continue;
                }
                if (inClass)
                {
                    if (c == ']')
                        inClass = false;
                    continue;
                }
                switch (c)
                {
                case '[':
                    inClass = true;
                    if (i + 1 < p.size() && p[i + 1] == '^')
                        i++;
                    if (i + 1 < p.size() && p[i + 1] == ']')
                        i++;
                    canRepeat = true;
                    break;
                case '(':
                    parens++;
                    if (i + 2 < p.size() && p[i + 1] == '?')
                    {
                        const char k = p[i + 2];
                        if (k != ':' && k != '=' && k != '!')
                            return "Unknown group kind \"(?" + std::string(1, k) + "\"";
                        i += 2;
                    }
                    canRepeat = false;
                    break;
                case ')':
                    if (--parens < 0)
                        return "Unmatched ')'";
                    canRepeat = true;
                    break;
                case '|':
                    canRepeat = false;
                    break;
                case '*':
                case '+':
                case '?':
                    if (!canRepeat)
                        return std::string("Nothing to repeat before '") + c + "'";
                    if (i + 1 < p.size() && p[i + 1] == '?')
                        i++;
                    canRepeat = false;
                    break;
                case '{':
                {
                    const size_t close = p.find('}', i);
                    if (close == std::string::npos)
                        return "Unmatched '{'";
                    const std::string body = p.substr(i + 1, close - i - 1);
                    if (body.empty() || body.find_first_not_of("0123456789,") != std::string::npos || body[0] == ',')
                        return "Bad repetition {" + body + "}";
                    if (!canRepeat)
                        return "Nothing to repeat before '{'";
                    i = close;
                    canRepeat = false;
                    break;
                }
                case '}':
                case ']':
                    // A lone } or ] is a literal in ECMAScript.
                    canRepeat = true;
                    break;
                default:
                    canRepeat = true;
                    break;
                }
            }
            if (inClass)
                return "Unmatched '['";
            if (parens > 0)
                return "Unmatched '('";
            return {};
        }
    }

    // ---- regex ----------------------------------------------------------------------

    bool Regex::Search(const std::string& subject, size_t from, std::smatch& m, size_t& base) const
    {
        const auto flagsAt = [](size_t pos)
        {
            // ^ and \b see the character before the range unless it is the start.
            return pos > 0 ? std::regex_constants::match_prev_avail : std::regex_constants::match_default;
        };

        if (!m_lineAnchors)
        {
            base = from;
            return std::regex_search(subject.begin() + from, subject.end(), m, m_re, flagsAt(from));
        }

        // A search started at a line's start lets ^ match there (no prev_avail),
        // and a match that begins on that line is the leftmost one there is.
        // One that begins on a later line is found again, correctly anchored,
        // by the search started at that line.
        size_t pos = from;
        while (true)
        {
            const bool lineStart = pos == 0 || subject[pos - 1] == '\n';
            const auto flags = lineStart ? std::regex_constants::match_default : std::regex_constants::match_prev_avail;
            const bool found = std::regex_search(subject.begin() + pos, subject.end(), m, m_re, flags);
            const size_t newline = subject.find('\n', pos);
            if (found && (newline == std::string::npos || pos + m.position(0) <= newline))
            {
                base = pos;
                return true;
            }
            // Nothing that begins on this line: a ^ match may still begin on the next.
            if (newline == std::string::npos)
                return false;
            pos = newline + 1;
        }
    }

    void Regex::Keep(const std::smatch& m, size_t base)
    {
        Found f;
        for (const auto& sub : m)
            f.groups.push_back(sub.str());
        f.end = base + m.position(0) + m.length(0);
        m_matches.push_back(std::move(f));
    }

    int Regex::Match(const char* text, size_t offset)
    {
        m_matches.clear();
        const std::string subject = text ? text : "";
        if (offset > subject.size())
            return 0;
        std::smatch m;
        size_t base = 0;
        try
        {
            if (!Search(subject, offset, m, base))
                return 0;
        }
        catch (...)   // error_complexity / error_stack on a pathological text
        {
            return 0;
        }
        Keep(m, base);
        return static_cast<int>(m.size());
    }

    int Regex::MatchAll(const char* text)
    {
        m_matches.clear();
        const std::string subject = text ? text : "";
        try
        {
            size_t pos = 0;
            std::smatch m;
            size_t base = 0;
            while (pos <= subject.size() && Search(subject, pos, m, base))
            {
                Keep(m, base);
                const size_t start = base + m.position(0);
                const size_t end = start + m.length(0);
                // An empty match moves on by one, or it is found forever.
                pos = end > start ? end : end + 1;
            }
        }
        catch (...)
        {
            m_matches.clear();
        }
        return static_cast<int>(m_matches.size());
    }

    int Regex::CaptureCount(int match)
    {
        return match >= 0 && match < static_cast<int>(m_matches.size()) ? static_cast<int>(m_matches[match].groups.size()) : 0;
    }

    const char* Regex::GetSubString(int index, int match)
    {
        if (match < 0 || match >= static_cast<int>(m_matches.size()))
            return nullptr;
        const auto& g = m_matches[match].groups;
        return index >= 0 && index < static_cast<int>(g.size()) ? g[index].c_str() : nullptr;
    }

    size_t Regex::MatchEnd(int match)
    {
        return match >= 0 && match < static_cast<int>(m_matches.size()) ? m_matches[match].end : 0;
    }

    // ---- text -----------------------------------------------------------------------

    std::vector<std::string> UtilsManager::Explode(const char* text, const char* separator, int maxParts)
    {
        std::vector<std::string> parts;
        const std::string s = text ? text : "";
        const std::string sep = separator ? separator : "";
        if (sep.empty())
        {
            parts.push_back(s);
            return parts;
        }
        size_t from = 0;
        while (true)
        {
            if (maxParts > 0 && static_cast<int>(parts.size()) == maxParts - 1)
                break;
            const size_t at = s.find(sep, from);
            if (at == std::string::npos)
                break;
            parts.push_back(s.substr(from, at - from));
            from = at + sep.size();
        }
        parts.push_back(s.substr(from));
        return parts;
    }

    std::string UtilsManager::Implode(const std::vector<std::string>& parts, const char* separator)
    {
        std::string out;
        for (size_t i = 0; i < parts.size(); i++)
        {
            if (i)
                out += separator ? separator : "";
            out += parts[i];
        }
        return out;
    }

    int UtilsManager::Replace(std::string& text, const char* search, const char* replace, bool caseSensitive)
    {
        const std::string what = search ? search : "";
        const std::string with = replace ? replace : "";
        if (what.empty())
            return 0;
        int count = 0;
        size_t pos = 0;
        while ((pos = FindIn(text, what, pos, caseSensitive)) != std::string::npos)
        {
            text.replace(pos, what.size(), with);
            pos += with.size();
            ++count;
        }
        return count;
    }

    std::string UtilsManager::Trim(const char* text)
    {
        std::string s = text ? text : "";
        size_t start = 0, end = s.size();
        while (start < end && IsSpace(s[start]))
            ++start;
        while (end > start && IsSpace(s[end - 1]))
            --end;
        return s.substr(start, end - start);
    }

    int UtilsManager::Find(const char* text, const char* search, bool caseSensitive)
    {
        const size_t at = FindIn(text ? text : "", search ? search : "", 0, caseSensitive);
        return at == std::string::npos ? -1 : static_cast<int>(at);
    }

    bool UtilsManager::StrEqual(const char* a, const char* b, bool caseSensitive)
    {
        a = a ? a : "";
        b = b ? b : "";
        return caseSensitive ? std::strcmp(a, b) == 0 : strcasecmp(a, b) == 0;
    }

    // ---- time -----------------------------------------------------------------------

    int64_t UtilsManager::GetTime()
    {
        return static_cast<int64_t>(std::time(nullptr));
    }

    std::string UtilsManager::FormatTime(const char* format, int64_t stamp)
    {
        const std::tm tm = LocalTime(stamp < 0 ? std::time(nullptr) : static_cast<std::time_t>(stamp));
        char buf[512];
        const size_t n = std::strftime(buf, sizeof(buf), format ? format : "", &tm);
        return std::string(buf, n);
    }

    int64_t UtilsManager::ParseTime(const char* text, const char* format)
    {
        std::tm tm{};
        std::istringstream in(text ? text : "");
        in >> std::get_time(&tm, format ? format : "");
        if (in.fail() || in.peek() != EOF)
            return -1;
        tm.tm_isdst = -1;
        return static_cast<int64_t>(std::mktime(&tm));
    }

    int64_t UtilsManager::GetTickCount()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // ---- regex ----------------------------------------------------------------------

    ISource2ModRegex* UtilsManager::CompileRegex(const char* pattern, int flags, std::string* error)
    {
        std::string p = pattern ? pattern : "";
        auto syntax = std::regex::ECMAScript;
        if (flags & SOURCE2MOD_REGEX_CASELESS)
            syntax |= std::regex::icase;
        if (flags & SOURCE2MOD_REGEX_MULTILINE)
            AddMultiline<std::regex>(syntax);
        const bool lineAnchors = (flags & SOURCE2MOD_REGEX_MULTILINE) && !HasMultiline<std::regex>::value;
        if (const std::string problem = CheckPattern(p); !problem.empty())
        {
            if (error)
                *error = problem;
            return nullptr;
        }
        if (flags & SOURCE2MOD_REGEX_DOTALL)
            p = DotAll(p);
        if (lineAnchors)
            p = LineEnds(p);
        try
        {
            return new Regex(std::regex(p, syntax), lineAnchors);
        }
        catch (const std::regex_error& e)
        {
            if (error)
                *error = e.what();
            return nullptr;
        }
        catch (...)   // see CheckPattern: the typed catch may not match in the game
        {
            if (error)
                *error = "Invalid regular expression";
            return nullptr;
        }
    }

    // ---- logging --------------------------------------------------------------------

    bool UtilsManager::LogToFile(PluginId owner, const char* path, const char* message)
    {
        std::string line = FormatTime("L %m/%d/%Y - %H:%M:%S") + ": ";
        if (owner)
        {
            if (const char* file = pluginManager.NameOf(owner))
                line += std::string("[") + file + "] ";
        }
        line += message ? message : "";
        line += "\n";
        return files::filesManager.WriteFile(path, line.data(), line.size(), true);
    }
}
