/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Translations (ISource2ModTranslations): languages from configs/languages.cfg
 * and added at runtime, phrases per plugin from phrase files, JSON or calls.
 *
 * Project: Source2Mod
 */
#pragma once

#include <source2mod/ISource2ModTranslations.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace translations
{
    struct Phrase
    {
        /// The #format spec of each placeholder by number (1-based; [0] unused).
        std::vector<std::string> format;
        /// The text per language index.
        std::unordered_map<int, std::string> text;
    };

    class TranslationsManager final : public ISource2ModTranslations
    {
    public:
        int GetLanguageCount() override;
        bool GetLanguageInfo(int language, const char** code, const char** name) override;
        int FindLanguage(const char* codeOrName) override;
        int AddLanguage(const char* code, const char* name) override;
        int GetServerLanguage() override;
        int GetPlayerLanguage(CPlayerSlot slot) override;
        void SetPlayerLanguage(CPlayerSlot slot, int language) override;

        bool LoadPhraseFile(PluginId owner, const char* file, char* error, size_t maxlen) override;
        bool LoadPhrasesJson(PluginId owner, const char* json, char* error, size_t maxlen) override;
        void AddPhrase(PluginId owner, const char* phrase, const char* language, const char* text, const char* format = nullptr) override;
        bool RemovePhrase(PluginId owner, const char* phrase, const char* language = nullptr) override;
        void ClearPhrases(PluginId owner) override;
        bool PhraseExists(PluginId owner, const char* phrase) override;
        bool IsTranslatedFor(PluginId owner, const char* phrase, int language) override;

        size_t Translate(PluginId owner, const char* phrase, CPlayerSlot target, char* buffer, size_t maxlen) override;
        size_t FormatPhrase(PluginId owner, const char* phrase, CPlayerSlot target,
                            const char* const* args, int numArgs, char* buffer, size_t maxlen) override;

        // ---- core side ----

        /// A plugin's phrase; nullptr when it has none by that name.
        const Phrase* Find(PluginId owner, const char* phrase);

        /// The text for a target: its language, the server's, English; nullptr when none.
        const std::string* TextFor(const Phrase& phrase, CPlayerSlot target);

        /// Parses "{1:s},{2:d}" into a phrase's format.
        static void ParseFormat(Phrase& phrase, const char* format);

        void RemoveAllForPlugin(PluginId owner);

    private:
        struct Language
        {
            std::string code;
            std::string name;
        };

        struct PlayerLanguage
        {
            int userId = -1;    // whose override this is
            int language = -1;
        };

        void EnsureLanguages();
        int LanguageByCode(const char* code) const;
        Phrase& Slot(PluginId owner, const char* phrase);

        std::vector<Language> m_languages;
        bool m_languagesLoaded = false;
        int m_serverLanguage = 0;
        std::array<PlayerLanguage, 64> m_players{};
        std::unordered_map<PluginId, std::unordered_map<std::string, Phrase>> m_phrases;
    };

    extern TranslationsManager translationsManager;
}
