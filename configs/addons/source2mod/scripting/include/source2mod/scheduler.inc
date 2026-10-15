/**
 * Source2Mod SourcePawn API: the scheduler (ISource2ModScheduler).
 *
 * Next frame, once after a delay, or repeating; all on the main thread, all
 * gone with the plugin.
 */

#if defined _source2mod_scheduler_included
 #endinput
#endif
#define _source2mod_scheduler_included

#include <source2mod/types>

#if !defined TIMER_FLAG_REPEAT
 #define TIMER_FLAG_REPEAT          (1<<0)  /**< Repeats until killed */
#endif
#if !defined TIMER_FLAG_NO_MAPCHANGE
 #define TIMER_FLAG_NO_MAPCHANGE    (1<<1)  /**< Killed at a map change */
#endif

typedef Source2ModTask = function void (any data);

/**
 * A running timer. Good until it ends: a one-shot after it fires, a
 * repeating one when it is killed (or at a map change with
 * TIMER_FLAG_NO_MAPCHANGE). Killing one that has ended is harmless.
 */
methodmap Source2ModTimer
{
	/** Seconds between runs. 0.0 once the timer has ended (as every getter's zero). */
	property float Interval
	{
		public native get();
	}

	/** The engine time of the next run. */
	property float ExecTime
	{
		public native get();
	}

	/** TIMER_FLAG_* */
	property int Flags
	{
		public native get();
	}

	/** True while its callback runs. */
	property bool InExec
	{
		public native get();
	}

	/** True when it is marked for deletion -- or has ended already. */
	property bool KillMe
	{
		public native get();
	}
}

methodmap Source2ModScheduler
{
	public static native void NextFrame(Source2ModTask task, any data = 0);

	public static native Source2ModTimer AddTimer(float interval, Source2ModTask callback, any data = 0, int flags = 0);

	/** A timer may kill itself from inside its own callback. */
	public static native void KillTimer(Source2ModTimer timer);
}
