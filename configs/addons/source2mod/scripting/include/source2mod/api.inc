/**
 * Source2Mod SourcePawn API: everything, for #include <source2mod/api>.
 *
 * Next to <source2mod> (the SourceMod core API: Format, handles, ADT,
 * ConVar, forwards, timers, ...), which it pulls in too.
 */

#if defined _source2mod_api_included
 #endinput
#endif
#define _source2mod_api_included

#include <source2mod>
#include <source2mod/types>
#include <source2mod/schema>
#include <source2mod/convars>
#include <source2mod/commands>
#include <source2mod/events>
#include <source2mod/entities>
#include <source2mod/listener>
#include <source2mod/netmessages>
#include <source2mod/sounds>
#include <source2mod/gamehooks>
#include <source2mod/gameconfig>
#include <source2mod/hooks>
#include <source2mod/scheduler>
#include <source2mod/transmit>
#include <source2mod/paths>
#include <source2mod/permissions>
#include <source2mod/trace>
#include <source2mod/gamesystems>
#include <source2mod/addresses>
#include <source2mod/json>
#include <source2mod/http>
#include <source2mod/sql>
#include <source2mod/menus>
#include <source2mod/customhud>
#include <source2mod/scripts>
#include <source2mod/translations>
#include <source2mod/files>
#include <source2mod/utils>
#include <source2mod/targeting>
#include <source2mod/clients>
#include <source2mod/features>
#include <source2mod/maps>
#include <source2mod/geoip>
#include <source2mod/cookies>

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
 * Source2Mod* methodmaps of the includes above; Log / ConPrint are
 * LogMessage / PrintToServer, Format is Format.
 */
methodmap Source2ModApi
{
	public static CGlobalVars GetGlobalVars()
	{
		return view_as<CGlobalVars>(0);
	}
}
