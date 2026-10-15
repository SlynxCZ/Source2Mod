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
    }

    // ---- regex ----------------------------------------------------------------------

    int Regex::Match(const char* text, size_t offset)
    {
        m_matches.clear();
        const std::string subject = text ? text : "";
        if (offset > subject.size())
            return 0;
        std::smatch m;
        if (!std::regex_search(subject.begin() + offset, subject.end(), m, m_re))
            return 0;
        Found f;
        for (const auto& sub : m)
            f.groups.push_back(sub.str());
        f.end = offset + m.position(0) + m.length(0);
        m_matches.push_back(std::move(f));
        return static_cast<int>(m.size());
    }

    int Regex::MatchAll(const char* text)
    {
        m_matches.clear();
        const std::string subject = text ? text : "";
        for (auto it = std::sregex_iterator(subject.begin(), subject.end(), m_re); it != std::sregex_iterator(); ++it)
        {
            Found f;
            for (const auto& sub : *it)
                f.groups.push_back(sub.str());
            f.end = it->position(0) + it->length(0);
            m_matches.push_back(std::move(f));
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
        if (flags & SOURCE2MOD_REGEX_DOTALL)
            p = DotAll(p);
        try
        {
            return new Regex(std::regex(p, syntax));
        }
        catch (const std::regex_error& e)
        {
            if (error)
                *error = e.what();
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
