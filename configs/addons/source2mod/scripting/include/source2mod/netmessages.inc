/**
 * Source2Mod SourcePawn API: net messages (ISource2ModNetworkMessages).
 *
 * A NetMessage is a protobuf message; fields go by their .proto names.
 * Allocated ones are the plugin's until DeallocateNetMessage (or delete);
 * sending does not consume them. The ones a hook is handed are valid inside
 * that hook only. Message ids are in source2mod/netmessage_ids.
 *
 * 64-bit fields are decimal text; double fields go through float.
 */

#if defined _source2mod_netmessages_included
 #endinput
#endif
#define _source2mod_netmessages_included

#include <source2mod/types>
#include <source2mod/netmessage_ids>

methodmap NetMessage < Handle
{
}

/**
 * Server -> clients: every message the server posts, with the recipients
 * (a player mask) that may be edited, as may the message. Action_Supersede
 * drops it for everybody.
 */
typedef Source2ModServerMessageHook = function Source2ModAction (int clients[2], int messageId, NetMessage msg);

/** Client -> server. Action_Supersede drops it before the server sees it. */
typedef Source2ModClientMessageHook = function Source2ModAction (CPlayerSlot slot, int messageId, NetMessage msg);

methodmap Source2ModNetworkMessages
{
	public static native NetMessage AllocateNetMessageByID(int messageId);
	public static native NetMessage AllocateNetMessageByPartialName(const char[] name);
	public static native void DeallocateNetMessage(NetMessage msg);

	public static native bool HasField(NetMessage msg, const char[] field);

	public static native int GetInt32(NetMessage msg, const char[] field);
	public static native int GetRepeatedInt32(NetMessage msg, const char[] field, int index);
	public static native void SetInt32(NetMessage msg, const char[] field, int value);
	public static native void SetRepeatedInt32(NetMessage msg, const char[] field, int index, int value);
	public static native void AddInt32(NetMessage msg, const char[] field, int value);

	public static native int GetUInt32(NetMessage msg, const char[] field);
	public static native int GetRepeatedUInt32(NetMessage msg, const char[] field, int index);
	public static native void SetUInt32(NetMessage msg, const char[] field, int value);
	public static native void SetRepeatedUInt32(NetMessage msg, const char[] field, int index, int value);
	public static native void AddUInt32(NetMessage msg, const char[] field, int value);

	public static native void GetInt64(NetMessage msg, const char[] field, char[] buffer, int maxlen);
	public static native void SetInt64(NetMessage msg, const char[] field, const char[] value);
	public static native void AddInt64(NetMessage msg, const char[] field, const char[] value);
	public static native void GetRepeatedInt64(NetMessage msg, const char[] field, int index, char[] buffer, int maxlen);
	public static native void SetRepeatedInt64(NetMessage msg, const char[] field, int index, const char[] value);
	public static native void GetUInt64(NetMessage msg, const char[] field, char[] buffer, int maxlen);
	public static native void SetUInt64(NetMessage msg, const char[] field, const char[] value);
	public static native void AddUInt64(NetMessage msg, const char[] field, const char[] value);
	public static native void GetRepeatedUInt64(NetMessage msg, const char[] field, int index, char[] buffer, int maxlen);
	public static native void SetRepeatedUInt64(NetMessage msg, const char[] field, int index, const char[] value);

	public static native bool GetBool(NetMessage msg, const char[] field);
	public static native bool GetRepeatedBool(NetMessage msg, const char[] field, int index);
	public static native void SetBool(NetMessage msg, const char[] field, bool value);
	public static native void SetRepeatedBool(NetMessage msg, const char[] field, int index, bool value);
	public static native void AddBool(NetMessage msg, const char[] field, bool value);

	public static native float GetFloat(NetMessage msg, const char[] field);
	public static native float GetRepeatedFloat(NetMessage msg, const char[] field, int index);
	public static native void SetFloat(NetMessage msg, const char[] field, float value);
	public static native void SetRepeatedFloat(NetMessage msg, const char[] field, int index, float value);
	public static native void AddFloat(NetMessage msg, const char[] field, float value);

	public static native float GetDouble(NetMessage msg, const char[] field);
	public static native float GetRepeatedDouble(NetMessage msg, const char[] field, int index);
	public static native void SetDouble(NetMessage msg, const char[] field, float value);
	public static native void SetRepeatedDouble(NetMessage msg, const char[] field, int index, float value);
	public static native void AddDouble(NetMessage msg, const char[] field, float value);

	public static native int GetString(NetMessage msg, const char[] field, char[] buffer, int maxlen);
	public static native int GetRepeatedString(NetMessage msg, const char[] field, int index, char[] buffer, int maxlen);
	public static native void SetString(NetMessage msg, const char[] field, const char[] value);
	public static native void SetRepeatedString(NetMessage msg, const char[] field, int index, const char[] value);
	public static native void AddString(NetMessage msg, const char[] field, const char[] value);

	public static native void GetVector(NetMessage msg, const char[] field, float vec[3]);
	public static native void GetRepeatedVector(NetMessage msg, const char[] field, int index, float vec[3]);
	public static native void SetVector(NetMessage msg, const char[] field, const float vec[3]);
	public static native void SetRepeatedVector(NetMessage msg, const char[] field, int index, const float vec[3]);
	public static native void AddVector(NetMessage msg, const char[] field, const float vec[3]);
	public static native void GetQAngle(NetMessage msg, const char[] field, float angles[3]);
	public static native void GetRepeatedQAngle(NetMessage msg, const char[] field, int index, float angles[3]);
	public static native void SetQAngle(NetMessage msg, const char[] field, const float angles[3]);
	public static native void SetRepeatedQAngle(NetMessage msg, const char[] field, int index, const float angles[3]);
	public static native void AddQAngle(NetMessage msg, const char[] field, const float angles[3]);
	public static native void GetVector2D(NetMessage msg, const char[] field, float vec[2]);
	public static native void GetRepeatedVector2D(NetMessage msg, const char[] field, int index, float vec[2]);
	public static native void SetVector2D(NetMessage msg, const char[] field, const float vec[2]);
	public static native void SetRepeatedVector2D(NetMessage msg, const char[] field, int index, const float vec[2]);
	public static native void AddVector2D(NetMessage msg, const char[] field, const float vec[2]);
	public static native void GetColor(NetMessage msg, const char[] field, int color[4]);
	public static native void GetRepeatedColor(NetMessage msg, const char[] field, int index, int color[4]);
	public static native void SetColor(NetMessage msg, const char[] field, const int color[4]);
	public static native void SetRepeatedColor(NetMessage msg, const char[] field, int index, const int color[4]);
	public static native void AddColor(NetMessage msg, const char[] field, const int color[4]);

	/** Bytes fields: raw bytes in a char array. @return Bytes read */
	public static native int GetBytes(NetMessage msg, const char[] field, char[] buffer, int maxlen);
	public static native int GetRepeatedBytes(NetMessage msg, const char[] field, int index, char[] buffer, int maxlen);
	public static native void SetBytes(NetMessage msg, const char[] field, const char[] value, int length);
	public static native void SetRepeatedBytes(NetMessage msg, const char[] field, int index, const char[] value, int length);
	public static native void AddBytes(NetMessage msg, const char[] field, const char[] value, int length);

	/** Nested messages: handles valid as long as the message they are in. */
	public static native NetMessage GetNestedMessage(NetMessage msg, const char[] field);
	public static native NetMessage GetRepeatedNestedMessage(NetMessage msg, const char[] field, int index);
	public static native NetMessage AddNestedMessage(NetMessage msg, const char[] field);

	public static native int GetRepeatedFieldSize(NetMessage msg, const char[] field);
	public static native void ClearRepeatedField(NetMessage msg, const char[] field);
	public static native void Clear(NetMessage msg);

	public static native void SendMessage(NetMessage msg, int messageId, CPlayerSlot slot);
	public static native void SendMessageToPlayers(NetMessage msg, int messageId, const int players[2]);

	/**
	 * Unhook* take only an id of their own kind that this plugin got back;
	 * anything else is false.
	 *
	 * @return The id UnhookServerMessage takes
	 */
	public static native int HookServerMessage(Source2ModServerMessageHook handler);
	public static native bool UnhookServerMessage(int id);

	/** @return The id UnhookClientMessage takes */
	public static native int HookClientMessage(Source2ModClientMessageHook handler);
	public static native bool UnhookClientMessage(int id);

	/** Messages the server sends itself (a bot's commands, ...). */
	public static native int HookServerInternalMessage(Source2ModClientMessageHook handler);
	public static native bool UnhookServerInternalMessage(int id);
}
