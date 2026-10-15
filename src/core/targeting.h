/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Command text to players (ISource2ModTargeting): @ patterns, #userid,
 * SteamIDs, names, filters plugins add, immunity through permissions.
 *
 * Project: Source2Mod
 */
#pragma once

#include <source2mod/ISource2ModTargeting.h>

#include <string>
#include <vector>

namespace targeting
{
    class TargetingManager final : public ISource2ModTargeting
    {
    public:
        int ProcessTargetString(const char* pattern, CPlayerSlot caller, int flags,
                                std::vector<CPlayerSlot>& targets, Source2ModTargetName* name = nullptr) override;
        int FindTarget(const char* pattern, CPlayerSlot caller, int flags, CPlayerSlot& target) override;
        const char* GetTargetErrorPhrase(int error) override;
        const char* GetTargetErrorText(int error) override;
        Source2ModHookId AddTargetFilter(const char* name, Source2ModTargetFilter filter, const char* phrase, bool phraseIsTranslation) override;
        bool RemoveTargetFilter(Source2ModHookId id) override;

        // ---- core side ----

        /// AddTargetFilter for an owner the callback's code does not tell (scripts).
        Source2ModHookId AddTargetFilterFor(PluginId owner, const char* name, Source2ModTargetFilter filter, const char* phrase, bool phraseIsTranslation);
        void RemoveAllForPlugin(PluginId id);

    private:
        struct Filter
        {
            PluginId owner;
            Source2ModHookId id;
            std::string name;     // without the @
            std::string phrase;
            bool phraseIsTranslation;
            Source2ModTargetFilter filter;
        };

        std::vector<Filter> m_filters;
    };

    extern TargetingManager targetingManager;
}
