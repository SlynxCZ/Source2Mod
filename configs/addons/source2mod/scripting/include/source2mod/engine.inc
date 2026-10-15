/**
 * Source2Mod SourcePawn API: the core and the engine -- ISource2ModAPI
 * (logging, the console, formatting, the global variables) and the engine
 * server (IVEngineServer2: server commands, engine time).
 *
 * Format / FormatArgs take SourceMod's format: %d %i %u %b %f %x %X %s %c
 * %t %T %N %L %%, with -, 0, width and precision.
 */

#if defined _source2mod_engine_included
 #endinput
#endif
#define _source2mod_engine_included

#include <source2mod/core>

/**
 * The engine's global variables (CGlobalVars): times, ticks, the map. Read
 * fresh on every access; Source2ModApi.GetGlobalVars() gives the one there
 * is.
 */
methodmap CGlobalVars
{
	property float realtime
	{
		public native get();
	}
	property int framecount
	{
		public native get();
	}
	property float absoluteframetime
	{
		public native get();
	}
	property float absoluteframetime_unbounded
	{
		public native get();
	}
	property int maxClients
	{
		public native get();
	}
	property int m_nCurrentTickThisFrame
	{
		public native get();
	}
	property int m_nTotalTicksThisFrame
	{
		public native get();
	}
	property float curtime
	{
		public native get();
	}
	property float frametime
	{
		public native get();
	}
	property float interpolation_amount
	{
		public native get();
	}
	property bool m_bInSimulation
	{
		public native get();
	}
	property int tickcount
	{
		public native get();
	}
	property float m_flSubtickFraction
	{
		public native get();
	}
	/** MapLoadType_t */
	property int eLoadType
	{
		public native get();
	}
	property bool mp_teamplay
	{
		public native get();
	}
	property int maxEntities
	{
		public native get();
	}
	property int serverCount
	{
		public native get();
	}
	public native int GetMapName(char[] buffer, int maxlen);
	public native int GetStartSpot(char[] buffer, int maxlen);
}

/**
 * ISource2ModAPI. Its subsystem accessors (Commands(), Events(), ...) are the
 * Source2Mod* methodmaps of the other includes.
 */
methodmap Source2ModApi
{
	/** The engine's global variables. */
	public static CGlobalVars GetGlobalVars()
	{
		return view_as<CGlobalVars>(0);
	}

	/**
	 * Logs a line: on the console as "[Plugin name] text", in Source2Mod.log
	 * with the time and the plugin's file (C++ ISource2ModAPI::Log).
	 */
	public static native void Log(const char[] format, any ...);

	/** The same as an error. */
	public static native void LogError(const char[] format, any ...);

	/** Prints to the server console as it is (no newline added). */
	public static native void ConPrint(const char[] message);

	/** Prints a formatted line to the server console. */
	public static native void ConPrintf(const char[] format, any ...);

	/** Formats into buffer. @return Bytes written */
	public static native int Format(char[] buffer, int maxlen, const char[] format, any ...);

	/**
	 * Formats with the calling function's own variadic arguments, starting
	 * at its parameter varpos (1-based) (C++ FormatArgs, SourceMod's VFormat).
	 * @return Bytes written
	 */
	public static native int FormatArgs(char[] buffer, int maxlen, const char[] format, int varpos);
}

/** The engine server (IVEngineServer2). */
methodmap Source2ModEngine
{
	/** Queues a server console command (formatted); it runs on the next frame. */
	public static native void ServerCommand(const char[] format, any ...);

	/** Seconds since the server started, not scaled or paused (Plat_FloatTime). */
	public static native float GetEngineTime();
}
