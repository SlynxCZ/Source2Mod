/**
 * Source2Mod SourcePawn API: translations (ISource2ModTranslations).
 *
 * Phrases belong to the calling script. They come from phrase files
 * (translations/<file>.txt, SourceMod's layout), JSON, or are added, changed
 * and removed at runtime -- a script that downloads its texts pushes them
 * when they arrive. Format's %t / %T use them the same way whatever their
 * source.
 *
 * Lookups never fail: a phrase without the target's language falls back to
 * the server's language, then English, then the phrase name itself.
 *
 * Targets are player slots; SOURCE2MOD_LANG_SERVER is the server.
 */

#if defined _source2mod_translations_included
 #endinput
#endif
#define _source2mod_translations_included

#include <source2mod/types>

#define SOURCE2MOD_LANG_SERVER CPlayerSlot(-1)

methodmap Source2ModTranslations
{
	/** Languages known: configs/languages.cfg plus those added at runtime. */
	public static native int GetLanguageCount();

	/** @return false for an index out of range */
	public static native bool GetLanguageInfo(int language, char[] code, int codeLen, char[] name = "", int nameLen = 0);

	/** By code or name; -1 when unknown. */
	public static native int FindLanguage(const char[] codeOrName);

	/** Adds a language (or finds it when the code is known). @return Its index */
	public static native int AddLanguage(const char[] code, const char[] name);

	public static native int GetServerLanguage();

	/** The one set with SetPlayerLanguage, else the client's own, else the server's. */
	public static native int GetPlayerLanguage(CPlayerSlot slot);

	/** -1 goes back to the client's own. */
	public static native void SetPlayerLanguage(CPlayerSlot slot, int language);

	/**
	 * translations/<file>.txt and translations/<code>/<file>.txt.
	 * @return false (and why in error) when the base file is missing or broken
	 */
	public static native bool LoadPhraseFile(const char[] file, char[] error = "", int maxlen = 0);

	/**
	 * {"<phrase>": {"#format": "{1:s}", "<code>": "<text>", ...}, ...};
	 * unknown language codes are added.
	 */
	public static native bool LoadPhrasesJson(const char[] json, char[] error = "", int maxlen = 0);

	/**
	 * Adds or replaces one text of a phrase.
	 *
	 * @param language A language code; an unknown one is added.
	 * @param format   "{1:s},{2:d}"; "" keeps the phrase's format.
	 */
	public static native void AddPhrase(const char[] phrase, const char[] language, const char[] text, const char[] format = "");

	/** One language's text, or the whole phrase for language "". */
	public static native bool RemovePhrase(const char[] phrase, const char[] language = "");

	public static native void ClearPhrases();
	public static native bool PhraseExists(const char[] phrase);
	public static native bool IsTranslatedFor(const char[] phrase, int language);

	/** The raw text for a target ({n} left in). @return Bytes written */
	public static native int Translate(const char[] phrase, CPlayerSlot target, char[] buffer, int maxlen);

	/**
	 * The text for a target with its {1}..{n} filled from the arguments, each
	 * formatted by the phrase's #format spec (as %t does).
	 * @return Bytes written
	 */
	public static native int FormatPhrase(char[] buffer, int maxlen, CPlayerSlot target, const char[] phrase, any ...);

	/** Whom Format's %t translates for (SetGlobalTransTarget). */
	public static native void SetFormatTarget(CPlayerSlot target);
}
