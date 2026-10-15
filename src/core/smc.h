/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourceMod's SMC text format (textparse.inc): KeyValues-style sections and
 * key/value pairs, quoted or bare tokens, // and block comments. Used by the
 * SMCParser natives, translations and KeyValues.
 *
 * Project: Source2Mod
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
