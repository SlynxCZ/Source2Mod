/**
 * Source2Mod SourcePawn API: commands (ISource2ModCommands).
 *
 * Console commands of the plugin's own (also typed in chat after a trigger:
 * "!name" / "/name"), chat listeners (only in chat), and listeners on
 * commands somebody else owns.
 */

#if defined _source2mod_commands_included
 #endinput
#endif
#define _source2mod_commands_included

#include <source2mod/types>

enum Source2ModCommandSource
{
	CommandSource_Console = 0,      /**< A console -- the server's or the player's own */
	CommandSource_Chat,             /**< Chat with a public trigger ("!kick"); the message shows */
	CommandSource_SilentChat        /**< Chat with a silent trigger ("/kick"); the message is hidden */
};

/**
 * Who ran the command and how. Valid inside the handler it was given to.
 */
methodmap Source2ModCommandContext
{
	/** The caller's slot; invalid (-1) for the server console. */
	public native CPlayerSlot GetPlayerSlot();

	/** The engine's CommandTarget_t. */
	public native int GetCommandTarget();

	public native Source2ModCommandSource GetSource();

	public native bool IsFromChat();
}

/** Source2ModCommandArgs::kMaxArgs: arguments past this many are dropped. */
#define SOURCE2MOD_COMMAND_MAX_ARGS 64

/**
 * The command line. Arg(0) is the command itself. Valid inside the handler
 * it was given to.
 */
methodmap Source2ModCommandArgs
{
	public native int ArgC();

	/** @return Bytes written */
	public native int Arg(int index, char[] buffer, int maxlen);

	/** Everything after the command, as one string. */
	public native int ArgS(char[] buffer, int maxlen);

	/** The whole line, command included. */
	public native int GetCommandString(char[] buffer, int maxlen);
}

/** A command of the plugin's own, or a chat listener. */
typedef Source2ModChatHandler = function void (Source2ModCommandContext context, Source2ModCommandArgs args, bool post);

/** A listener on someone else's command: Action_Supersede (pre) keeps it from running. */
typedef Source2ModCommandHandler = function Source2ModAction (Source2ModCommandContext context, Source2ModCommandArgs args, bool post);

methodmap Source2ModCommands
{
	/**
	 * Fires when the word is typed into chat after a chat trigger; no
	 * console command behind it.
	 *
	 * @param permission    Required permission, "" for none
	 * @return              The id UnregisterChatListener takes
	 */
	public static native int RegisterChatListener(const char[] name, Source2ModChatHandler handler, const char[] permission = "");

	public static native bool UnregisterChatListener(int id);

	/**
	 * A console command, also reachable from chat ("!name", "/name").
	 *
	 * @param permission    Required permission, "" for none
	 * @return              The id UnregisterConCommand takes
	 */
	public static native int RegisterConCommand(const char[] name, Source2ModChatHandler handler, const char[] permission = "");

	public static native bool UnregisterConCommand(int id);

	/**
	 * Listens to a command that is not the plugin's: the game's, another
	 * plugin's. Pre may answer Action_Supersede, post only watches.
	 *
	 * @return The id UnregisterConListener takes
	 */
	public static native int RegisterConListener(const char[] name, Source2ModCommandHandler handler, bool post);

	public static native bool UnregisterConListener(int id);

	/** Answers where the command came from: the caller's console or chat. Formatted. */
	public static native void ReplyToCommand(Source2ModCommandContext context, const char[] format, any ...);
}
