/**
 * Source2Mod SourcePawn API: JSON (ISource2ModJSON, ISource2ModJSONDocument,
 * ISource2ModJSONValue). Read-only: parse text, walk the values.
 *
 * 64-bit integers are two cells (value[0] low, value[1] high bits); doubles
 * come back as float.
 */

#if defined _source2mod_json_included
 #endinput
#endif
#define _source2mod_json_included

/**
 * One value inside a document. Values belong to their document: they need
 * no delete and are gone with it. Get/At return null when there is none.
 */
methodmap ISource2ModJSONValue < Handle
{
	public native bool IsNull();
	public native bool IsObject();
	public native bool IsArray();
	public native bool IsString();
	public native bool IsNumber();
	public native bool IsBool();

	/** Objects: by key. */
	public native bool Has(const char[] key);
	public native ISource2ModJSONValue Get(const char[] key);

	/** Objects and arrays: the number of members, by position. */
	public native int Count();
	public native ISource2ModJSONValue At(int index);

	/** Objects: the key at a position. @return Bytes written */
	public native int KeyAt(int index, char[] buffer, int maxlen);

	/** @return Bytes written */
	public native int AsString(char[] buffer, int maxlen, const char[] def = "");
	public native void AsInt64(int value[2], const int def[2] = {0, 0});
	public native int AsInt(int def = 0);
	public native float AsDouble(float def = 0.0);
	public native float AsFloat(float def = 0.0);
	public native bool AsBool(bool def = false);

	/** Objects: a member's value, or def when missing or of another type. */
	public native int GetString(const char[] key, char[] buffer, int maxlen, const char[] def = "");
	public native void GetInt64(const char[] key, int value[2], const int def[2] = {0, 0});
	public native int GetInt(const char[] key, int def = 0);
	public native float GetDouble(const char[] key, float def = 0.0);
	public native float GetFloat(const char[] key, float def = 0.0);
	public native bool GetBool(const char[] key, bool def = false);

	/** As JSON text; indent -1 = compact. @return Bytes written */
	public native int Dump(char[] buffer, int maxlen, int indent = -1);
}

/** A parsed text. delete releases it and its values. */
methodmap ISource2ModJSONDocument < Handle
{
	public native bool IsValid();

	/** The parse error when not valid. @return Bytes written */
	public native int GetError(char[] buffer, int maxlen);

	/** The top value; null when not valid. */
	public native ISource2ModJSONValue Root();
}

methodmap Source2ModJSON
{
	/** Never null: check IsValid(). length -1 = up to the terminator. */
	public static native ISource2ModJSONDocument Parse(const char[] text, int length = -1);
}
