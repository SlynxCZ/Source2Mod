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
 * Source2Mod: math, random numbers and vectors (Source2ModMath). The functions
 * of SourceMod's float.inc, vector.inc and GetRandomInt/GetRandomFloat under
 * Source2Mod names, same arguments and semantics.
 *
 * Angles are Source's: pitch, yaw, roll in degrees, x forward, y left, z up.
 * Trigonometry works in radians.
 */

#if defined _source2mod_math_included
 #endinput
#endif
#define _source2mod_math_included

#include <source2mod/float>

methodmap Source2ModMath
{
	// ---- float -------------------------------------------------------------

	/** @return Square root of value */
	public static native float Sqrt(float value);

	/** @return value^exponent */
	public static native float Pow(float value, float exponent);

	/** @return e^value */
	public static native float Exp(float value);

	/**
	 * Logarithm of any base. Errors on value or base <= 0.
	 *
	 * @return log(value)/log(base)
	 */
	public static native float Log(float value, float base = 10.0);

	/** @param value Radians */
	public static native float Sin(float value);

	/** @param value Radians */
	public static native float Cos(float value);

	/** @param value Radians */
	public static native float Tan(float value);

	/** @return Radians */
	public static native float Asin(float value);

	/** @return Radians */
	public static native float Acos(float value);

	/** @return Radians */
	public static native float Atan(float value);

	/** @return atan2(y, x) in radians */
	public static native float Atan2(float y, float x);

	/** @return Absolute value */
	public static native float Fabs(float value);

	/** @return value - floor(value) */
	public static native float Fraction(float value);

	/** Rounds toward zero. */
	public static native int Float2Int(float value);

	/** Rounds down. */
	public static native int Floor2Int(float value);

	/** Rounds up. */
	public static native int Ceil2Int(float value);

	/** Rounds to the nearest integer (halfway away from zero). */
	public static native int RoundFloatToInt(float value);

	/** Degrees to radians. */
	public static float DegToRad(float angle)
	{
		return (angle*FLOAT_PI)/180;
	}

	/** Radians to degrees. */
	public static float RadToDeg(float angle)
	{
		return (angle*180)/FLOAT_PI;
	}

	// ---- random numbers ----------------------------------------------------

	/** @return Random integer in [min, max], both inclusive */
	public static native int RandomInt(int min, int max);

	/** @return Random float in [min, max], both inclusive */
	public static native float RandomFloat(float min = 0.0, float max = 1.0);

	/**
	 * Seeds the random number stream. It is seeded automatically, so this is
	 * normally unnecessary.
	 *
	 * @param seeds         Seeding data.
	 * @param numSeeds      Number of values in seeds (> 0).
	 */
	public static native void RandomSeed(const int[] seeds, int numSeeds);

	// ---- vectors -----------------------------------------------------------

	/** @param squared If true, the squared length (faster) */
	public static native float VectorLength(const float vec[3], bool squared = false);

	/** @param squared If true, the squared distance (faster) */
	public static native float VectorDistance(const float vec1[3], const float vec2[3], bool squared = false);

	public static native float DotProduct(const float vec1[3], const float vec2[3]);

	/** result = vec1 x vec2 (result must not be vec1 or vec2) */
	public static native void CrossProduct(const float vec1[3], const float vec2[3], float result[3]);

	/**
	 * result = vec normalized (may be vec itself).
	 *
	 * @return Length of vec before normalizing
	 */
	public static native float VectorNormalize(const float vec[3], float result[3]);

	/** Direction vectors of an angle; NULL_VECTOR for any not wanted. */
	public static native void AngleVectors(const float angle[3], float fwd[3], float right[3], float up[3]);

	/** Angles (pitch, yaw, roll = 0) of a direction vector. */
	public static native void VectorAngles(const float vec[3], float angle[3]);

	/** Right and up vectors of a forward vector. */
	public static native void VectorVectors(const float vec[3], float right[3], float up[3]);

	/** result = vec1 + vec2 */
	public static void VectorAdd(const float vec1[3], const float vec2[3], float result[3])
	{
		result[0] = vec1[0] + vec2[0];
		result[1] = vec1[1] + vec2[1];
		result[2] = vec1[2] + vec2[2];
	}

	/** result = vec1 - vec2 (the vector from point vec2 to point vec1) */
	public static void VectorSubtract(const float vec1[3], const float vec2[3], float result[3])
	{
		result[0] = vec1[0] - vec2[0];
		result[1] = vec1[1] - vec2[1];
		result[2] = vec1[2] - vec2[2];
	}

	/** vec *= scale */
	public static void VectorScale(float vec[3], float scale)
	{
		vec[0] *= scale;
		vec[1] *= scale;
		vec[2] *= scale;
	}

	/** vec = -vec */
	public static void VectorNegate(float vec[3])
	{
		vec[0] = -vec[0];
		vec[1] = -vec[1];
		vec[2] = -vec[2];
	}
}
