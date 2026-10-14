/**
 * Source2Mod SourcePawn API: sounds (ISource2ModSounds, ISource2ModSound).
 *
 * Sound events go to the players picked for them and come back as a guid
 * (an int) that stops or changes them while they play.
 */

#if defined _source2mod_sounds_included
 #endinput
#endif
#define _source2mod_sounds_included

#include <source2mod/types>

/** Sound sources: at each listener (default), or the world. */
#define SOUND_SOURCE_LISTENER   -1
#define SOUND_SOURCE_WORLD      0

enum SoundParamType
{
	SoundParam_Invalid = 0,
	SoundParam_Bool = 1,
	SoundParam_Int32 = 2,
	SoundParam_UInt32 = 3,
	SoundParam_UInt64 = 4,
	SoundParam_Float = 8,
	SoundParam_Float3 = 10
};

/**
 * One sound event: what, from where, how, for whom. New() makes one, delete
 * releases it; Emit() may be called again and again, each a new guid. The
 * one a sound hook is handed is valid inside that hook.
 */
methodmap ISource2ModSound < Handle
{
	public static native ISource2ModSound New(const char[] name = "");

	public native void SetName(const char[] name);
	public native int GetName(char[] buffer, int maxlen);
	public native void SetNameHash(int hash);
	public native int GetNameHash();
	public native int GetGuid();

	public native void SetSourceEntityIndex(int index);
	public native int GetSourceEntityIndex();
	public native void SetSourceEntity(CEntityInstance entity);
	public native void SetPosition(const float position[3]);

	public native void SetVolume(float volume);
	public native float GetVolume();
	public native void SetPitch(float pitch);
	public native float GetPitch();
	public native void SetChannel(const char[] channel);
	public native int GetChannel(char[] buffer, int maxlen);

	/** Parameters the sound event declares ("public.my_param"). */
	public native bool HasParam(const char[] name);
	public native SoundParamType GetParamType(const char[] name);
	public native void RemoveParam(const char[] name);
	public native void ClearParams();
	public native void SetBool(const char[] name, bool value);
	public native void SetInt32(const char[] name, int value);
	public native void SetUInt32(const char[] name, int value);
	public native void SetFloat(const char[] name, float value);
	public native void SetFloat3(const char[] name, const float value[3]);
	public native bool GetBool(const char[] name, bool def = false);
	public native int GetInt32(const char[] name, int def = 0);
	public native int GetUInt32(const char[] name, int def = 0);
	public native float GetFloat(const char[] name, float def = 0.0);
	public native void GetFloat3(const char[] name, float value[3]);

	public native void AddRecipient(CPlayerSlot slot);
	public native void RemoveRecipient(CPlayerSlot slot);
	public native bool HasRecipient(CPlayerSlot slot);
	public native void AddAllRecipients();
	public native void ClearRecipients();
	public native void SetRecipients(const int players[2]);
	public native void GetRecipients(int players[2]);

	/** @return The guid of the sound now playing */
	public native int Emit();
}

/** The game's own sounds on their way out: Action_Override sends the changes, Action_Supersede drops it. */
typedef Source2ModSoundHook = function Source2ModAction (ISource2ModSound sound);

methodmap Source2ModSounds
{
	/** @return The guid */
	public static native int EmitSoundToPlayer(CPlayerSlot slot, const char[] name, float volume = 1.0, float pitch = 1.0, const char[] channel = "");
	public static native int EmitSoundToPlayers(const int players[2], const char[] name, float volume = 1.0, float pitch = 1.0, const char[] channel = "");
	public static native int EmitSoundToAll(const char[] name, float volume = 1.0, float pitch = 1.0, const char[] channel = "");

	public static native void StopSound(int guid);
	public static native void StopSoundForPlayers(int guid, const int players[2]);
	public static native void StopSoundByName(const char[] name, int sourceEntityIndex = SOUND_SOURCE_LISTENER);
	public static native void SetSoundVolume(int guid, float volume);
	public static native void UpdateSoundParams(int guid, ISource2ModSound params);

	/** @return The id UnhookSound takes */
	public static native int HookSound(Source2ModSoundHook handler);
	public static native bool UnhookSound(int id);

	/** What sounds travel as; hash the names you care about once. */
	public static native int HashSoundName(const char[] name);
}
