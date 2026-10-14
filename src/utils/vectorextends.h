/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 * gives you permission to link the code of this program
 * (as well as its derivative works) to "Counter-Strike 2," "Source 2,"
 * "Steam," and any Game MODs or server software running on software by
 * Valve Corporation. You must obey the GNU General Public License in all
 * respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once

#include <vector.h>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#undef min
#undef max
#endif

namespace vectorextends
{
    inline const Vector VectorZero{0.0f, 0.0f, 0.0f};
    inline const QAngle RotationZero{0.0f, 0.0f, 0.0f};
    inline const Vector StaticTeleportVector{9999999.0f, 8888888.0f, 777777.0f};

    inline Vector Clone(const Vector& vector)
    {
        return Vector{vector.x, vector.y, vector.z};
    }

    inline QAngle QClone(const QAngle& angle)
    {
        return QAngle{angle.x, angle.y, angle.z};
    }

    inline Vector Adds(const Vector& vector, const Vector& other)
    {
        return Vector{vector.x + other.x, vector.y + other.y, vector.z + other.z};
    }

    inline Vector Scale(const Vector& vector, float scale)
    {
        return Vector{vector.x * scale, vector.y * scale, vector.z * scale};
    }

    inline Vector Normalize(const Vector& vector)
    {
        float length = vector.Length();
        return Vector{vector.x / length, vector.y / length, vector.z / length};
    }

    inline float Distance(const Vector& vector, const Vector& other)
    {
        return std::sqrt(
            std::pow(vector.x - other.x, 2.0f) +
            std::pow(vector.y - other.y, 2.0f) +
            std::pow(vector.z - other.z, 2.0f)
        );
    }

    inline float DistanceSquared(const Vector& vector, const Vector& other)
    {
        return
            std::pow(vector.x - other.x, 2.0f) +
            std::pow(vector.y - other.y, 2.0f) +
            std::pow(vector.z - other.z, 2.0f);
    }

    inline Vector MakeVectorFromPoints(const Vector& pt1, const Vector& pt2)
    {
        return Vector{pt2.x - pt1.x, pt2.y - pt1.y, pt2.z - pt1.z};
    }

    inline Vector SubtractVectors(const Vector& vec1, const Vector& vec2)
    {
        return Vector{vec1.x - vec2.x, vec1.y - vec2.y, vec1.z - vec2.z};
    }

    inline Vector NegateVector(const Vector& vec)
    {
        return Vector{-vec.x, -vec.y, -vec.z};
    }

    inline float DegToRad(float degrees)
    {
        return degrees * (static_cast<float>(M_PI) / 180.0f);
    }

    inline float RadToDeg(float radians)
    {
        return radians * (180.0f / static_cast<float>(M_PI));
    }

    inline bool IsInsideBox(const Vector& playerVector, const Vector& corner1, const Vector& corner2, float height = 0.0f)
    {
        float minX = std::min(corner1.x, corner2.x);
        float minY = std::min(corner1.y, corner2.y);
        float minZ = std::min(corner1.z, corner2.z);

        float maxX = std::max(corner1.x, corner2.x);
        float maxY = std::max(corner1.y, corner2.y);
        float maxZ = std::max(corner1.z, corner2.z);

        return playerVector.x >= minX && playerVector.x <= maxX &&
               playerVector.y >= minY && playerVector.y <= maxY &&
               playerVector.z >= minZ && playerVector.z <= maxZ + height;
    }

    inline bool IsInsideField(const Vector& playerVector, const Vector& corner1, const Vector& corner2)
    {
        float minX = std::min(corner1.x, corner2.x);
        float minY = std::min(corner1.y, corner2.y);

        float maxX = std::max(corner1.x, corner2.x);
        float maxY = std::max(corner1.y, corner2.y);

        return playerVector.x >= minX && playerVector.x <= maxX &&
               playerVector.y >= minY && playerVector.y <= maxY;
    }

    inline float ArcCosine(float value)
    {
        return std::acos(value);
    }

    inline float GetVectorDotProduct(const Vector& vec1, const Vector& vec2)
    {
        return vec1.x * vec2.x + vec1.y * vec2.y + vec1.z * vec2.z;
    }

    inline float Dot(const Vector& vec1, const Vector& vec2)
    {
        return vec1.x * vec2.x + vec1.y * vec2.y + vec1.z * vec2.z;
    }
}