/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for translations (source2mod/translations.inc):
 * Source2ModTranslations on ISource2ModTranslations, the calling script's
 * phrases. Format's %t / %T are in natives.cpp (TranslateInto).
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "scriptplugin.h"

#include "core/translations.h"

#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        translations::TranslationsManager& Tr()
        {
            return translations::translationsManager;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        cell_t N_GetLanguageCount(IPluginContext*, const cell_t*)
        {
            return Tr().GetLanguageCount();
        }

        // GetLanguageInfo(language, code, codeLen, name, nameLen)
        cell_t N_GetLanguageInfo(IPluginContext* ctx, const cell_t* params)
        {
            const char* code = nullptr;
            const char* name = nullptr;
            if (!Tr().GetLanguageInfo(params[1], &code, &name))
                return 0;
            ToBuffer(ctx, params[2], params[3], code);
            if (params[0] >= 5)
                ToBuffer(ctx, params[4], params[5], name);
            return 1;
        }

        cell_t N_FindLanguage(IPluginContext* ctx, const cell_t* params)
        {
            return Tr().FindLanguage(LocalString(ctx, params[1]));
        }

        cell_t N_AddLanguage(IPluginContext* ctx, const cell_t* params)
        {
            return Tr().AddLanguage(LocalString(ctx, params[1]), LocalString(ctx, params[2]));
        }

        cell_t N_GetServerLanguage(IPluginContext*, const cell_t*)
        {
            return Tr().GetServerLanguage();
        }

        cell_t N_GetPlayerLanguage(IPluginContext*, const cell_t* params)
        {
            return Tr().GetPlayerLanguage(CPlayerSlot(params[1]));
        }

        cell_t N_SetPlayerLanguage(IPluginContext*, const cell_t* params)
        {
            Tr().SetPlayerLanguage(CPlayerSlot(params[1]), params[2]);
            return 0;
        }

        // LoadPhraseFile(file, error, maxlen) / LoadPhrasesJson(json, error, maxlen)
        template <bool kJson>
        cell_t LoadPhrases(IPluginContext* ctx, const cell_t* params)
        {
            char error[256] = "";
            const bool ok = kJson ? Tr().LoadPhrasesJson(CallerId(ctx), LocalString(ctx, params[1]), error, sizeof(error))
                                  : Tr().LoadPhraseFile(CallerId(ctx), LocalString(ctx, params[1]), error, sizeof(error));
            if (params[0] >= 3)
                ToBuffer(ctx, params[2], params[3], error);
            return ok;
        }

        // AddPhrase(phrase, language, text, format)
        cell_t N_AddPhrase(IPluginContext* ctx, const cell_t* params)
        {
            const char* format = params[0] >= 4 ? LocalString(ctx, params[4]) : "";
            Tr().AddPhrase(CallerId(ctx), LocalString(ctx, params[1]), LocalString(ctx, params[2]), LocalString(ctx, params[3]),
                           *format ? format : nullptr);
            return 0;
        }

        cell_t N_RemovePhrase(IPluginContext* ctx, const cell_t* params)
        {
            return Tr().RemovePhrase(CallerId(ctx), LocalString(ctx, params[1]), params[0] >= 2 ? LocalString(ctx, params[2]) : "");
        }

        cell_t N_ClearPhrases(IPluginContext* ctx, const cell_t*)
        {
            Tr().ClearPhrases(CallerId(ctx));
            return 0;
        }

        cell_t N_PhraseExists(IPluginContext* ctx, const cell_t* params)
        {
            return Tr().PhraseExists(CallerId(ctx), LocalString(ctx, params[1]));
        }

        cell_t N_IsTranslatedFor(IPluginContext* ctx, const cell_t* params)
        {
            return Tr().IsTranslatedFor(CallerId(ctx), LocalString(ctx, params[1]), params[2]);
        }

        // Translate(phrase, target, buffer, maxlen)
        cell_t N_Translate(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<char> buf(std::max<cell_t>(params[4], 1));
            Tr().Translate(CallerId(ctx), LocalString(ctx, params[1]), CPlayerSlot(params[2]), buf.data(), buf.size());
            return ToBuffer(ctx, params[3], params[4], buf.data());
        }

        // FormatPhrase(buffer, maxlen, target, phrase, any ...)
        cell_t N_FormatPhrase(IPluginContext* ctx, const cell_t* params)
        {
            std::string out;
            int arg = 5;
            if (!TranslateInto(ctx, params, arg, params[3], LocalString(ctx, params[4]), out))
                return 0;
            return ToBuffer(ctx, params[1], params[2], out.c_str());
        }

        cell_t N_SetFormatTarget(IPluginContext*, const cell_t* params)
        {
            SetFormatTarget(params[1]);
            return 0;
        }

        const sp_nativeinfo_t g_translationNatives[] = {
            { "Source2ModTranslations.GetLanguageCount", N_GetLanguageCount },
            { "Source2ModTranslations.GetLanguageInfo", N_GetLanguageInfo },
            { "Source2ModTranslations.FindLanguage", N_FindLanguage },
            { "Source2ModTranslations.AddLanguage", N_AddLanguage },
            { "Source2ModTranslations.GetServerLanguage", N_GetServerLanguage },
            { "Source2ModTranslations.GetPlayerLanguage", N_GetPlayerLanguage },
            { "Source2ModTranslations.SetPlayerLanguage", N_SetPlayerLanguage },
            { "Source2ModTranslations.LoadPhraseFile", LoadPhrases<false> },
            { "Source2ModTranslations.LoadPhrasesJson", LoadPhrases<true> },
            { "Source2ModTranslations.AddPhrase", N_AddPhrase },
            { "Source2ModTranslations.RemovePhrase", N_RemovePhrase },
            { "Source2ModTranslations.ClearPhrases", N_ClearPhrases },
            { "Source2ModTranslations.PhraseExists", N_PhraseExists },
            { "Source2ModTranslations.IsTranslatedFor", N_IsTranslatedFor },
            { "Source2ModTranslations.Translate", N_Translate },
            { "Source2ModTranslations.FormatPhrase", N_FormatPhrase },
            { "Source2ModTranslations.SetFormatTarget", N_SetFormatTarget },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetTranslationNatives()
    {
        return g_translationNatives;
    }
}
