/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod (C)2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This file is part of the SourceMod/SourcePawn SDK.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

/**
 * Source2Mod: the float plumbing the compiler needs (float(), the float
 * operators and their natives), from SourceMod's float.inc under the same
 * native names, because spcomp emits them. The math functions are in
 * <source2mod/math> (Source2ModMath).
 */

#if defined _source2mod_float_included
 #endinput
#endif
#define _source2mod_float_included

#define FLOAT_PI 3.1415926535897932384626433832795

#if !defined __sourcepawn_intrinsic_float
/**
 * Converts an integer into a floating point value.
 *
 * @param value         Integer to convert.
 * @return              Floating point value.
 */
native float float(int value);

/**
 * User defined operators.
 */

native float __FLOAT_MUL__(float a, float b);
native float __FLOAT_DIV__(float a, float b);
native float __FLOAT_ADD__(float a, float b);
native float __FLOAT_SUB__(float a, float b);
native float __FLOAT_MOD__(float a, float b);

native bool __FLOAT_GT__(float a, float b);
native bool __FLOAT_GE__(float a, float b);
native bool __FLOAT_LT__(float a, float b);
native bool __FLOAT_LE__(float a, float b);
native bool __FLOAT_EQ__(float a, float b);
native bool __FLOAT_NE__(float a, float b);
native bool __FLOAT_NOT__(float a);

stock float operator*(float oper1, float oper2) { return __FLOAT_MUL__(oper1, oper2); }
stock float operator/(float oper1, float oper2) { return __FLOAT_DIV__(oper1, oper2); }
stock float operator+(float oper1, float oper2) { return __FLOAT_ADD__(oper1, oper2); }
stock float operator-(float oper1, float oper2) { return __FLOAT_SUB__(oper1, oper2); }
stock float operator%(float oper1, float oper2) { return __FLOAT_MOD__(oper1, oper2); }
stock bool operator!(float oper1) { return __FLOAT_NOT__(oper1); }
stock bool operator>(float oper1, float oper2) { return __FLOAT_GT__(oper1, oper2); }
stock bool operator>=(float oper1, float oper2) { return __FLOAT_GE__(oper1, oper2); }
stock bool operator<(float oper1, float oper2) { return __FLOAT_LT__(oper1, oper2); }
stock bool operator<=(float oper1, float oper2) { return __FLOAT_LE__(oper1, oper2); }
stock bool operator!=(float oper1, float oper2) { return __FLOAT_NE__(oper1, oper2); }
stock bool operator==(float oper1, float oper2) { return __FLOAT_EQ__(oper1, oper2); }

stock float operator++(float oper)
{
	return oper+1.0;
}

stock float operator--(float oper)
{
	return oper-1.0;
}

stock float operator-(float oper)
{
	return oper^view_as<float>(cellmin);                /* IEEE values are sign/magnitude */
}

// The stocks below are int->float converting versions of the above natives.

stock float operator*(float oper1, int oper2)
{
	return __FLOAT_MUL__(oper1, float(oper2));          /* "*" is commutative */
}

stock float operator/(float oper1, int oper2)
{
	return __FLOAT_DIV__(oper1, float(oper2));
}

stock float operator/(int oper1, float oper2)
{
	return __FLOAT_DIV__(float(oper1), oper2);
}

stock float operator+(float oper1, int oper2)
{
	return __FLOAT_ADD__(oper1, float(oper2));          /* "+" is commutative */
}

stock float operator-(float oper1, int oper2)
{
	return __FLOAT_SUB__(oper1, float(oper2));
}

stock float operator-(int oper1, float oper2)
{
	return __FLOAT_SUB__(float(oper1), oper2);
}

stock bool operator==(float oper1, int oper2)
{
	return __FLOAT_EQ__(oper1, float(oper2));
}

stock bool operator!=(float oper1, int oper2)
{
	return __FLOAT_NE__(oper1, float(oper2));
}

stock bool operator>(float oper1, int oper2)
{
	return __FLOAT_GT__(oper1, float(oper2));
}

stock bool operator>(int oper1, float oper2)
{
	return __FLOAT_GT__(float(oper1), oper2);
}

stock bool operator>=(float oper1, int oper2)
{
	return __FLOAT_GE__(oper1, float(oper2));
}

stock bool operator>=(int oper1, float oper2)
{
	return __FLOAT_GE__(float(oper1), oper2);
}

stock bool operator<(float oper1, int oper2)
{
	return __FLOAT_LT__(oper1, float(oper2));
}

stock bool operator<(int oper1, float oper2)
{
	return __FLOAT_LT__(float(oper1), oper2);
}

stock bool operator<=(float oper1, int oper2)
{
	return __FLOAT_LE__(oper1, float(oper2));
}

stock bool operator<=(int oper1, float oper2)
{
	return __FLOAT_LE__(float(oper1), oper2);
}

stock float operator%(float oper1, int oper2)
{
	return __FLOAT_MOD__(oper1, float(oper2));
}

stock float operator%(int oper1, float oper2)
{
	return __FLOAT_MOD__(float(oper1), oper2);
}
#endif // __sourcepawn_intrinsic_float
