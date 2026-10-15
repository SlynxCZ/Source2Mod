/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Translations (see translations.h).
 *
 * Project: Source2Mod
 */
#include "translations.h"

#include "coreconfig.h"
#include "paths.h"
#include "shared.h"
#include "smc.h"
#include "utils/log.h"

#include <interfaces/interfaces.h>
#include <nlohmann/json.hpp>
#include <source2mod/schema/entity/classes/CCSPlayerController.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

namespace translations
{
    TranslationsManager translationsManager;

    namespace
    {
        size_t Copy(char* buffer, size_t maxlen, const std::string& text)
        {
            if (!buffer || maxlen == 0)
                return 0;
            const size_t n = std::min(text.size(), maxlen - 1);
            std::memcpy(buffer, text.data(), n);
            buffer[n] = '\0';
            return n;
        }

        void SetError(char* error, size_t maxlen, const std::string& text)
        {
            Copy(error, maxlen, text);
        }

        /// cl_language values whose name is not the languages.cfg name.
        const char* ClientLanguageCode(const char* value)
        {
            static const struct { const char* value; const char* code; } kMap[] = {
                { "koreana", "ko" }, { "korean", "ko" }, { "schinese", "chi" }, { "tchinese", "zho" },
                { "brazilian", "pt" }, { "portuguese", "pt_p" }, { "latam", "es" }, { "czech", "cze" },
                { "japanese", "jp" }, { "ukrainian", "ua" }, { "vietnamese", "vi" }, { "bulgarian", "bg" },
            };
            for (const auto& m : kMap)
                if (strcasecmp(m.value, value) == 0)
                    return m.code;
            return nullptr;
        }

        class LanguageReader : public smc::Listener
        {
        public:
            explicit LanguageReader(std::vector<std::pair<std::string, std::string>>& out) : m_out(out) {}

            smc::Result EnterSection(const char* name, bool) override
            {
                ++m_depth;
                m_in = m_depth == 1 && strcasecmp(name, "Languages") == 0;
                return smc::Result::Continue;
            }
            smc::Result KeyValue(const char* key, const char* value, bool, bool) override
            {
                if (m_in)
                    m_out.emplace_back(key, value);
                return smc::Result::Continue;
            }
            smc::Result LeaveSection() override
            {
                --m_depth;
                return smc::Result::Continue;
            }

        private:
            std::vector<std::pair<std::string, std::string>>& m_out;
            int m_depth = 0;
            bool m_in = false;
        };

        /// "Phrases" { "<phrase>" { "#format" "{1:s}" "<code>" "<text>" } }
        class PhraseReader : public smc::Listener
        {
        public:
            PhraseReader(TranslationsManager& tr, PluginId owner) : m_tr(tr), m_owner(owner) {}

            smc::Result EnterSection(const char* name, bool) override
            {
                if (++m_depth == 2)
                    m_phrase = name;
                return smc::Result::Continue;
            }
            smc::Result KeyValue(const char* key, const char* value, bool, bool) override
            {
                if (m_depth != 2)
                    return smc::Result::Continue;
                if (std::strcmp(key, "#format") == 0)
                    m_tr.AddPhrase(m_owner, m_phrase.c_str(), nullptr, nullptr, value);
                else
                    m_tr.AddPhrase(m_owner, m_phrase.c_str(), key, value);
                return smc::Result::Continue;
            }
            smc::Result LeaveSection() override
            {
                --m_depth;
                return smc::Result::Continue;
            }

        private:
            TranslationsManager& m_tr;
            PluginId m_owner;
            std::string m_phrase;
            int m_depth = 0;
        };
    }

    // ---- languages ------------------------------------------------------------------

    void TranslationsManager::EnsureLanguages()
    {
        if (m_languagesLoaded)
            return;
        m_languagesLoaded = true;

        std::vector<std::pair<std::string, std::string>> entries;
        LanguageReader reader(entries);
        const std::string path = std::string(paths::pathsManager.RootDirectory()) + "/configs/languages.cfg";
        smc::States states;
        const smc::Error err = smc::ParseFile(path.c_str(), reader, &states);
        if (err != smc::Error::Okay)
            FP_WARN("languages.cfg: {} (line {}); only English is known", smc::ErrorString(err), states.line);

        if (std::none_of(entries.begin(), entries.end(), [](const auto& e) { return strcasecmp(e.first.c_str(), "en") == 0; }))
            m_languages.push_back({ "en", "English" });
        for (const auto& [code, name] : entries)
            if (LanguageByCode(code.c_str()) < 0)
                m_languages.push_back({ code, name });

        const std::string serverLang = shared::g_pCoreConfig ? shared::g_pCoreConfig->ServerLang : std::string("en");
        m_serverLanguage = std::max(0, LanguageByCode(serverLang.c_str()));
    }

    int TranslationsManager::LanguageByCode(const char* code) const
    {
        for (size_t i = 0; i < m_languages.size(); i++)
            if (strcasecmp(m_languages[i].code.c_str(), code) == 0)
                return static_cast<int>(i);
        return -1;
    }

    int TranslationsManager::GetLanguageCount()
    {
        EnsureLanguages();
        return static_cast<int>(m_languages.size());
    }

    bool TranslationsManager::GetLanguageInfo(int language, const char** code, const char** name)
    {
        EnsureLanguages();
        if (language < 0 || language >= static_cast<int>(m_languages.size()))
            return false;
        if (code)
            *code = m_languages[language].code.c_str();
        if (name)
            *name = m_languages[language].name.c_str();
        return true;
    }

    int TranslationsManager::FindLanguage(const char* codeOrName)
    {
        EnsureLanguages();
        if (!codeOrName)
            return -1;
        const int byCode = LanguageByCode(codeOrName);
        if (byCode >= 0)
            return byCode;
        for (size_t i = 0; i < m_languages.size(); i++)
            if (strcasecmp(m_languages[i].name.c_str(), codeOrName) == 0)
                return static_cast<int>(i);
        return -1;
    }

    int TranslationsManager::AddLanguage(const char* code, const char* name)
    {
        EnsureLanguages();
        if (!code || !*code)
            return -1;
        const int known = LanguageByCode(code);
        if (known >= 0)
            return known;
        m_languages.push_back({ code, name && *name ? name : code });
        return static_cast<int>(m_languages.size() - 1);
    }

    int TranslationsManager::GetServerLanguage()
    {
        EnsureLanguages();
        return m_serverLanguage;
    }

    int TranslationsManager::GetPlayerLanguage(CPlayerSlot slot)
    {
        EnsureLanguages();
        const int index = slot.Get();
        if (index < 0 || index >= static_cast<int>(m_players.size()))
            return m_serverLanguage;
        CCSPlayerController* player = CCSPlayerController::FromSlot(slot);
        if (!player)
            return m_serverLanguage;

        // An override lasts as long as the player it was set for.
        const PlayerLanguage& set = m_players[index];
        if (set.language >= 0 && set.userId == player->GetUserID())
            return set.language;

        if (g_pEngineServer && !player->IsBot())
        {
            const char* value = g_pEngineServer->GetClientConVarValue(slot, "cl_language");
            if (value && *value)
            {
                if (const char* code = ClientLanguageCode(value))
                {
                    const int lang = LanguageByCode(code);
                    if (lang >= 0)
                        return lang;
                }
                const int lang = FindLanguage(value);
                if (lang >= 0)
                    return lang;
            }
        }
        return m_serverLanguage;
    }

    void TranslationsManager::SetPlayerLanguage(CPlayerSlot slot, int language)
    {
        EnsureLanguages();
        const int index = slot.Get();
        if (index < 0 || index >= static_cast<int>(m_players.size()))
            return;
        CCSPlayerController* player = CCSPlayerController::FromSlot(slot);
        m_players[index] = { player ? player->GetUserID() : -1, language };
    }

    // ---- phrases --------------------------------------------------------------------

    void TranslationsManager::ParseFormat(Phrase& phrase, const char* format)
    {
        phrase.format.assign(1, std::string());
        for (const char* p = format; (p = std::strchr(p, '{')); )
        {
            const char* close = std::strchr(p, '}');
            const char* colon = std::strchr(p, ':');
            if (!close || !colon || colon > close)
                break;
            const int index = std::atoi(p + 1);
            if (index > 0 && index < 64)
            {
                if (static_cast<int>(phrase.format.size()) <= index)
                    phrase.format.resize(index + 1);
                phrase.format[index].assign(colon + 1, close);
            }
            p = close + 1;
        }
    }

    Phrase& TranslationsManager::Slot(PluginId owner, const char* phrase)
    {
        return m_phrases[owner][phrase];
    }

    void TranslationsManager::AddPhrase(PluginId owner, const char* phrase, const char* language, const char* text, const char* format)
    {
        EnsureLanguages();
        if (!phrase || !*phrase)
            return;
        Phrase& p = Slot(owner, phrase);
        if (format)
            ParseFormat(p, format);
        if (language && *language && text)
            p.text[AddLanguage(language, nullptr)] = text;
    }

    bool TranslationsManager::RemovePhrase(PluginId owner, const char* phrase, const char* language)
    {
        auto plugin = m_phrases.find(owner);
        if (plugin == m_phrases.end() || !phrase)
            return false;
        auto it = plugin->second.find(phrase);
        if (it == plugin->second.end())
            return false;
        if (!language || !*language)
        {
            plugin->second.erase(it);
            return true;
        }
        const int lang = LanguageByCode(language);
        return lang >= 0 && it->second.text.erase(lang) > 0;
    }

    void TranslationsManager::ClearPhrases(PluginId owner)
    {
        m_phrases.erase(owner);
    }

    void TranslationsManager::RemoveAllForPlugin(PluginId owner)
    {
        m_phrases.erase(owner);
    }

    bool TranslationsManager::LoadPhraseFile(PluginId owner, const char* file, char* error, size_t maxlen)
    {
        EnsureLanguages();
        std::string name = file ? file : "";
        if (name.size() < 4 || strcasecmp(name.c_str() + name.size() - 4, ".txt") != 0)
            name += ".txt";

        const std::string root = std::string(paths::pathsManager.RootDirectory()) + "/translations/";
        if (!std::filesystem::exists(root + name))
        {
            SetError(error, maxlen, "Could not find translation file \"" + name + "\"");
            return false;
        }

        PhraseReader reader(*this, owner);
        smc::States states;
        const smc::Error err = smc::ParseFile((root + name).c_str(), reader, &states);
        if (err != smc::Error::Okay)
        {
            SetError(error, maxlen, "\"" + name + "\" line " + std::to_string(states.line) + ": " + smc::ErrorString(err));
            return false;
        }

        // translations/<code>/<file>.txt for every other language.
        for (const Language& lang : std::vector<Language>(m_languages))
        {
            const std::string path = root + lang.code + "/" + name;
            if (!std::filesystem::exists(path))
                continue;
            PhraseReader extra(*this, owner);
            const smc::Error e = smc::ParseFile(path.c_str(), extra, &states);
            if (e != smc::Error::Okay)
                FP_WARN("Translation file \"{}/{}\": {} (line {})", lang.code, name, smc::ErrorString(e), states.line);
        }
        return true;
    }

    bool TranslationsManager::LoadPhrasesJson(PluginId owner, const char* json, char* error, size_t maxlen)
    {
        EnsureLanguages();
        nlohmann::json doc = nlohmann::json::parse(json ? json : "", nullptr, false);
        if (doc.is_discarded() || !doc.is_object())
        {
            SetError(error, maxlen, "Phrases must be a JSON object of phrases");
            return false;
        }
        for (auto& [phrase, entry] : doc.items())
        {
            if (!entry.is_object())
                continue;
            if (auto f = entry.find("#format"); f != entry.end() && f->is_string())
                AddPhrase(owner, phrase.c_str(), nullptr, nullptr, f->get_ref<const std::string&>().c_str());
            for (auto& [code, text] : entry.items())
                if (code != "#format" && text.is_string())
                    AddPhrase(owner, phrase.c_str(), code.c_str(), text.get_ref<const std::string&>().c_str());
        }
        return true;
    }

    const Phrase* TranslationsManager::Find(PluginId owner, const char* phrase)
    {
        auto plugin = m_phrases.find(owner);
        if (plugin == m_phrases.end() || !phrase)
            return nullptr;
        auto it = plugin->second.find(phrase);
        return it != plugin->second.end() ? &it->second : nullptr;
    }

    bool TranslationsManager::PhraseExists(PluginId owner, const char* phrase)
    {
        return Find(owner, phrase) != nullptr;
    }

    bool TranslationsManager::IsTranslatedFor(PluginId owner, const char* phrase, int language)
    {
        const Phrase* p = Find(owner, phrase);
        return p && p->text.count(language) != 0;
    }

    const std::string* TranslationsManager::TextFor(const Phrase& phrase, CPlayerSlot target)
    {
        EnsureLanguages();
        const int langs[] = { target.Get() < 0 ? m_serverLanguage : GetPlayerLanguage(target), m_serverLanguage,
                              std::max(0, LanguageByCode("en")) };
        for (int lang : langs)
        {
            auto it = phrase.text.find(lang);
            if (it != phrase.text.end())
                return &it->second;
        }
        return nullptr;
    }

    size_t TranslationsManager::Translate(PluginId owner, const char* phrase, CPlayerSlot target, char* buffer, size_t maxlen)
    {
        const Phrase* p = Find(owner, phrase);
        const std::string* text = p ? TextFor(*p, target) : nullptr;
        return Copy(buffer, maxlen, text ? *text : std::string(phrase ? phrase : ""));
    }

    size_t TranslationsManager::FormatPhrase(PluginId owner, const char* phrase, CPlayerSlot target,
                                             const char* const* args, int numArgs, char* buffer, size_t maxlen)
    {
        const Phrase* p = Find(owner, phrase);
        const std::string* text = p ? TextFor(*p, target) : nullptr;
        if (!text)
            return Copy(buffer, maxlen, phrase ? phrase : "");

        std::string out;
        for (const char* c = text->c_str(); *c; ++c)
        {
            if (*c == '{' && c[1] >= '0' && c[1] <= '9')
            {
                char* end = nullptr;
                const long index = std::strtol(c + 1, &end, 10);
                if (*end == '}' && index >= 1)
                {
                    if (index <= numArgs && args && args[index - 1])
                        out += args[index - 1];
                    c = end;
                    continue;
                }
            }
            out += *c;
        }
        return Copy(buffer, maxlen, out);
    }
}
