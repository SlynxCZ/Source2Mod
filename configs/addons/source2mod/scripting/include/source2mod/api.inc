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
