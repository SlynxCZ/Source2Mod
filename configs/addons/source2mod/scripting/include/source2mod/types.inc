/**
 * Source2Mod SourcePawn API: the types every area shares.
 *
 * The source2mod/ includes mirror Source2Mod's C++ API (ISource2Mod*): an
 * interface is a methodmap of static methods named after it without the "I"
 * and the version (ISource2ModCommands003 -> Source2ModCommands), with the
 * C++ method names and argument order. Hook* returns an id Unhook* takes; a
 * plugin's hooks, listeners and timers go when it unloads.
 */

#if defined _source2mod_types_included
 #endinput
#endif
#define _source2mod_types_included

/**
 * No entity: an empty handle, a failed lookup, an entity argument left out.
 * Entity methodmap values are entity handles (source2mod/schema.inc); -1 is
 * the empty one.
 */
#define NULL_ENTITY view_as<any>(-1)

/**
 * What a hook answers, as KHook::Action in C++.
 */
enum Source2ModAction
{
	Action_Ignore = 0,      /**< Did nothing; the original runs */
	Action_Override,        /**< Changed something (the return value, a field); the original still runs */
	Action_Supersede        /**< The original does not run (pre hooks) */
};

/**
 * C++'s HookChain: whether an SDK helper's call into the game runs the hooks
 * on it (Run) or goes past them to the original (Bypass) -- what a handler
 * that calls the function it hooks needs, and what an admin action uses to
 * stay out of other plugins' way. Default is what the C++ helper defaults to
 * (Run for most, Bypass for TakeDamage and TerminateRound).
 */
enum Source2ModHookChain
{
	HookChain_Default = -1,
	HookChain_Run = 0,
	HookChain_Bypass
};

/**
 * A player's slot, 0..63; -1 is "no player" (the server console).
 * The client index of the SourceMod-style natives is slot + 1.
 */
methodmap CPlayerSlot
{
	public CPlayerSlot(int slot)
	{
		return view_as<CPlayerSlot>(slot);
	}

	public int Get()
	{
		return view_as<int>(this);
	}

	public bool IsValid()
	{
		int slot = view_as<int>(this);
		return slot >= 0 && slot < 64;
	}
}

/**
 * C++'s uint64_t player masks (one bit per slot) as two cells:
 * mask[0] holds slots 0..31, mask[1] slots 32..63.
 */
stock void PlayerMask_Clear(int mask[2])
{
	mask[0] = 0;
	mask[1] = 0;
}

stock void PlayerMask_All(int mask[2])
{
	mask[0] = -1;
	mask[1] = -1;
}

stock void PlayerMask_Add(int mask[2], int slot)
{
	mask[slot >> 5] |= (1 << (slot & 31));
}

stock void PlayerMask_Remove(int mask[2], int slot)
{
	mask[slot >> 5] &= ~(1 << (slot & 31));
}

stock bool PlayerMask_Has(const int mask[2], int slot)
{
	return (mask[slot >> 5] & (1 << (slot & 31))) != 0;
}
