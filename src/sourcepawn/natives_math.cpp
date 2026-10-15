/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Source2ModMath (source2mod/math.inc: math, random numbers, vectors) and the
 * float operator natives of source2mod/float.inc (names spcomp emits).
 *
 * The VM maps the operator natives (float(), __FLOAT_GT__ and friends,
 * __float_*) to opcodes when the native is bound normally, and its x64 JIT
 * does not implement those opcodes (visitFLOAT* return false): a function
 * calling one failed to compile with "Unknown error code 0". Bound ephemeral,
 * they stay native calls.
 *
 * Angles are Source's: pitch, yaw, roll in degrees, x forward, y left, z up.
 *
 * Project: Source2Mod
 */
#include "natives.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        float F(const cell_t* params, int i)
        {
            return sp_ctof(params[i]);
        }

        // ---- float.inc ---------------------------------------------------------------

        cell_t N_Float(IPluginContext*, const cell_t* params) { return sp_ftoc(static_cast<float>(params[1])); }
        cell_t N_FloatMul(IPluginContext*, const cell_t* params) { return sp_ftoc(F(params, 1) * F(params, 2)); }
        cell_t N_FloatDiv(IPluginContext*, const cell_t* params) { return sp_ftoc(F(params, 1) / F(params, 2)); }
        cell_t N_FloatAdd(IPluginContext*, const cell_t* params) { return sp_ftoc(F(params, 1) + F(params, 2)); }
        cell_t N_FloatSub(IPluginContext*, const cell_t* params) { return sp_ftoc(F(params, 1) - F(params, 2)); }
        cell_t N_FloatMod(IPluginContext*, const cell_t* params) { return sp_ftoc(std::fmod(F(params, 1), F(params, 2))); }
        cell_t N_FloatAbs(IPluginContext*, const cell_t* params) { return sp_ftoc(std::fabs(F(params, 1))); }

        cell_t N_FloatFraction(IPluginContext*, const cell_t* params)
        {
            const float v = F(params, 1);
            return sp_ftoc(v - std::floor(v));
        }

        cell_t N_RoundToZero(IPluginContext*, const cell_t* params) { return static_cast<cell_t>(std::trunc(F(params, 1))); }
        cell_t N_RoundToCeil(IPluginContext*, const cell_t* params) { return static_cast<cell_t>(std::ceil(F(params, 1))); }
        cell_t N_RoundToFloor(IPluginContext*, const cell_t* params) { return static_cast<cell_t>(std::floor(F(params, 1))); }
        cell_t N_RoundToNearest(IPluginContext*, const cell_t* params) { return static_cast<cell_t>(std::round(F(params, 1))); }


        cell_t N_SquareRoot(IPluginContext*, const cell_t* params) { return sp_ftoc(std::sqrt(F(params, 1))); }
        cell_t N_Pow(IPluginContext*, const cell_t* params) { return sp_ftoc(std::pow(F(params, 1), F(params, 2))); }
        cell_t N_Exponential(IPluginContext*, const cell_t* params) { return sp_ftoc(std::exp(F(params, 1))); }

        cell_t N_Logarithm(IPluginContext* ctx, const cell_t* params)
        {
            const float value = F(params, 1), base = F(params, 2);
            if (value <= 0.0f || base <= 0.0f)
                return ctx->ThrowNativeError("Cannot evaluate the logarithm of zero or a negative number (val:%f base:%f)", value, base);
            if (base == 10.0f)
                return sp_ftoc(std::log10(value));
            return sp_ftoc(std::log(value) / std::log(base));
        }

        cell_t N_Sine(IPluginContext*, const cell_t* params) { return sp_ftoc(std::sin(F(params, 1))); }
        cell_t N_Cosine(IPluginContext*, const cell_t* params) { return sp_ftoc(std::cos(F(params, 1))); }
        cell_t N_Tangent(IPluginContext*, const cell_t* params) { return sp_ftoc(std::tan(F(params, 1))); }
        cell_t N_ArcTangent(IPluginContext*, const cell_t* params) { return sp_ftoc(std::atan(F(params, 1))); }
        cell_t N_ArcCosine(IPluginContext*, const cell_t* params) { return sp_ftoc(std::acos(F(params, 1))); }
        cell_t N_ArcSine(IPluginContext*, const cell_t* params) { return sp_ftoc(std::asin(F(params, 1))); }
        cell_t N_ArcTangent2(IPluginContext*, const cell_t* params) { return sp_ftoc(std::atan2(F(params, 1), F(params, 2))); }

        cell_t N_FloatGt(IPluginContext*, const cell_t* params) { return F(params, 1) > F(params, 2); }
        cell_t N_FloatGe(IPluginContext*, const cell_t* params) { return F(params, 1) >= F(params, 2); }
        cell_t N_FloatLt(IPluginContext*, const cell_t* params) { return F(params, 1) < F(params, 2); }
        cell_t N_FloatLe(IPluginContext*, const cell_t* params) { return F(params, 1) <= F(params, 2); }
        cell_t N_FloatEq(IPluginContext*, const cell_t* params) { return F(params, 1) == F(params, 2); }
        cell_t N_FloatNe(IPluginContext*, const cell_t* params) { return F(params, 1) != F(params, 2); }

        cell_t N_FloatNot(IPluginContext*, const cell_t* params)
        {
            const float v = F(params, 1);
            return std::isnan(v) || v == 0.0f;
        }

        // ---- random numbers ------------------------------------------------------------

        std::mt19937& Rng()
        {
            static std::mt19937 rng{ std::random_device{}() };
            return rng;
        }



        cell_t N_SetURandomSeed(IPluginContext* ctx, const cell_t* params)
        {
            cell_t* seeds;
            ctx->LocalToPhysAddr(params[1], &seeds);
            if (params[2] < 1)
                return ctx->ThrowNativeError("numSeeds must be more than 0 (%d)", params[2]);
            std::vector<std::uint32_t> values(seeds, seeds + params[2]);
            std::seed_seq seq(values.begin(), values.end());
            Rng().seed(seq);
            return 0;
        }

        // GetRandomFloat(min = 0.0, max = 1.0): inclusive.
        cell_t N_GetRandomFloat(IPluginContext*, const cell_t* params)
        {
            float lo = F(params, 1), hi = F(params, 2);
            if (lo > hi)
                std::swap(lo, hi);
            if (lo == hi)
                return sp_ftoc(lo);
            std::uniform_real_distribution<float> dist(lo, std::nextafter(hi, hi + 1.0f));
            return sp_ftoc(std::fmin(dist(Rng()), hi));
        }

        // GetRandomInt(min, max): inclusive.
        cell_t N_GetRandomInt(IPluginContext*, const cell_t* params)
        {
            cell_t lo = params[1], hi = params[2];
            if (lo > hi)
                std::swap(lo, hi);
            std::uniform_int_distribution<cell_t> dist(lo, hi);
            return dist(Rng());
        }

        // ---- vector.inc ----------------------------------------------------------------

        struct Vec
        {
            float x, y, z;
        };

        Vec Read(IPluginContext* ctx, cell_t addr)
        {
            cell_t* v;
            ctx->LocalToPhysAddr(addr, &v);
            return { sp_ctof(v[0]), sp_ctof(v[1]), sp_ctof(v[2]) };
        }

        /// Writes an output vector; NULL_VECTOR is skipped.
        void Write(IPluginContext* ctx, cell_t addr, const Vec& in)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            if (out == ctx->GetNullRef(SP_NULL_VECTOR))
                return;
            out[0] = sp_ftoc(in.x);
            out[1] = sp_ftoc(in.y);
            out[2] = sp_ftoc(in.z);
        }

        float Dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        Vec Cross(const Vec& a, const Vec& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

        float Normalize(Vec& v)
        {
            const float len = std::sqrt(Dot(v, v));
            if (len > 0.0f)
            {
                v.x /= len;
                v.y /= len;
                v.z /= len;
            }
            else
                v = { 0.0f, 0.0f, 0.0f };
            return len;
        }

        // GetVectorLength(vec, squared)
        cell_t N_GetVectorLength(IPluginContext* ctx, const cell_t* params)
        {
            const Vec v = Read(ctx, params[1]);
            const float sq = Dot(v, v);
            return sp_ftoc(params[2] ? sq : std::sqrt(sq));
        }

        // GetVectorDistance(vec1, vec2, squared)
        cell_t N_GetVectorDistance(IPluginContext* ctx, const cell_t* params)
        {
            const Vec a = Read(ctx, params[1]), b = Read(ctx, params[2]);
            const Vec d{ a.x - b.x, a.y - b.y, a.z - b.z };
            const float sq = Dot(d, d);
            return sp_ftoc(params[3] ? sq : std::sqrt(sq));
        }

        cell_t N_GetVectorDotProduct(IPluginContext* ctx, const cell_t* params)
        {
            return sp_ftoc(Dot(Read(ctx, params[1]), Read(ctx, params[2])));
        }

        cell_t N_GetVectorCrossProduct(IPluginContext* ctx, const cell_t* params)
        {
            Write(ctx, params[3], Cross(Read(ctx, params[1]), Read(ctx, params[2])));
            return 0;
        }

        // NormalizeVector(vec, result): the length before.
        cell_t N_NormalizeVector(IPluginContext* ctx, const cell_t* params)
        {
            Vec v = Read(ctx, params[1]);
            const float len = Normalize(v);
            Write(ctx, params[2], v);
            return sp_ftoc(len);
        }

        // GetAngleVectors(angle, fwd, right, up): Source's AngleVectors.
        cell_t N_GetAngleVectors(IPluginContext* ctx, const cell_t* params)
        {
            const Vec a = Read(ctx, params[1]);
            const float pitch = a.x * kPi / 180.0f, yaw = a.y * kPi / 180.0f, roll = a.z * kPi / 180.0f;
            const float sp = std::sin(pitch), cp = std::cos(pitch);
            const float sy = std::sin(yaw), cy = std::cos(yaw);
            const float sr = std::sin(roll), cr = std::cos(roll);
            Write(ctx, params[2], { cp * cy, cp * sy, -sp });
            Write(ctx, params[3], { -sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp });
            Write(ctx, params[4], { cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp });
            return 0;
        }

        // GetVectorAngles(vec, angle): Source's VectorAngles (no roll).
        cell_t N_GetVectorAngles(IPluginContext* ctx, const cell_t* params)
        {
            const Vec v = Read(ctx, params[1]);
            float pitch, yaw;
            if (v.x == 0.0f && v.y == 0.0f)
            {
                yaw = 0.0f;
                pitch = v.z > 0.0f ? 270.0f : 90.0f;
            }
            else
            {
                yaw = std::atan2(v.y, v.x) * 180.0f / kPi;
                if (yaw < 0.0f)
                    yaw += 360.0f;
                pitch = std::atan2(-v.z, std::sqrt(v.x * v.x + v.y * v.y)) * 180.0f / kPi;
                if (pitch < 0.0f)
                    pitch += 360.0f;
            }
            Write(ctx, params[2], { pitch, yaw, 0.0f });
            return 0;
        }

        // GetVectorVectors(vec, right, up): Source's VectorVectors.
        cell_t N_GetVectorVectors(IPluginContext* ctx, const cell_t* params)
        {
            const Vec fwd = Read(ctx, params[1]);
            Vec right, up;
            if (fwd.x == 0.0f && fwd.y == 0.0f)
            {
                right = { 0.0f, -1.0f, 0.0f };
                up = { -fwd.z, 0.0f, 0.0f };
            }
            else
            {
                right = Cross(fwd, { 0.0f, 0.0f, 1.0f });
                Normalize(right);
                up = Cross(right, fwd);
                Normalize(up);
            }
            Write(ctx, params[2], right);
            Write(ctx, params[3], up);
            return 0;
        }

        const sp_nativeinfo_t g_mathNatives[] = {
            // Operators (source2mod/float.inc, names spcomp emits): the VM
            // turns these into opcodes the x64 JIT does not implement, so they
            // are bound ephemeral (IsEphemeralMathNative) and stay native calls.
            { "float", N_Float },
            { "__FLOAT_GT__", N_FloatGt },
            { "__FLOAT_GE__", N_FloatGe },
            { "__FLOAT_LT__", N_FloatLt },
            { "__FLOAT_LE__", N_FloatLe },
            { "__FLOAT_EQ__", N_FloatEq },
            { "__FLOAT_NE__", N_FloatNe },
            { "__FLOAT_NOT__", N_FloatNot },
            { "__float_ctor", N_Float },
            { "__float_mul", N_FloatMul },
            { "__float_div", N_FloatDiv },
            { "__float_add", N_FloatAdd },
            { "__float_sub", N_FloatSub },
            { "__float_mod", N_FloatMod },
            { "__float_gt", N_FloatGt },
            { "__float_ge", N_FloatGe },
            { "__float_lt", N_FloatLt },
            { "__float_le", N_FloatLe },
            { "__float_eq", N_FloatEq },
            { "__float_ne", N_FloatNe },
            { "__float_not", N_FloatNot },
            { "__FLOAT_MUL__", N_FloatMul },
            { "__FLOAT_DIV__", N_FloatDiv },
            { "__FLOAT_ADD__", N_FloatAdd },
            { "__FLOAT_SUB__", N_FloatSub },
            { "__FLOAT_MOD__", N_FloatMod },

            // Source2ModMath (source2mod/math.inc).
            { "Source2ModMath.Sqrt", N_SquareRoot },
            { "Source2ModMath.Pow", N_Pow },
            { "Source2ModMath.Exp", N_Exponential },
            { "Source2ModMath.Log", N_Logarithm },
            { "Source2ModMath.Sin", N_Sine },
            { "Source2ModMath.Cos", N_Cosine },
            { "Source2ModMath.Tan", N_Tangent },
            { "Source2ModMath.Asin", N_ArcSine },
            { "Source2ModMath.Acos", N_ArcCosine },
            { "Source2ModMath.Atan", N_ArcTangent },
            { "Source2ModMath.Atan2", N_ArcTangent2 },
            { "Source2ModMath.Fabs", N_FloatAbs },
            { "Source2ModMath.Fraction", N_FloatFraction },
            { "Source2ModMath.Float2Int", N_RoundToZero },
            { "Source2ModMath.Floor2Int", N_RoundToFloor },
            { "Source2ModMath.Ceil2Int", N_RoundToCeil },
            { "Source2ModMath.RoundFloatToInt", N_RoundToNearest },

            { "Source2ModMath.RandomInt", N_GetRandomInt },
            { "Source2ModMath.RandomFloat", N_GetRandomFloat },
            { "Source2ModMath.RandomSeed", N_SetURandomSeed },

            { "Source2ModMath.VectorLength", N_GetVectorLength },
            { "Source2ModMath.VectorDistance", N_GetVectorDistance },
            { "Source2ModMath.DotProduct", N_GetVectorDotProduct },
            { "Source2ModMath.CrossProduct", N_GetVectorCrossProduct },
            { "Source2ModMath.VectorNormalize", N_NormalizeVector },
            { "Source2ModMath.AngleVectors", N_GetAngleVectors },
            { "Source2ModMath.VectorAngles", N_GetVectorAngles },
            { "Source2ModMath.VectorVectors", N_GetVectorVectors },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetMathNatives()
    {
        return g_mathNatives;
    }

    bool IsEphemeralMathNative(const char* name)
    {
        static const char* const kNames[] = {
            "float",
            "__FLOAT_GT__", "__FLOAT_GE__", "__FLOAT_LT__", "__FLOAT_LE__", "__FLOAT_EQ__", "__FLOAT_NE__", "__FLOAT_NOT__",
            "__float_ctor", "__float_mul", "__float_div", "__float_add", "__float_sub", "__float_mod",
            "__float_gt", "__float_ge", "__float_lt", "__float_le", "__float_eq", "__float_ne", "__float_not",
        };
        for (const char* n : kNames)
            if (std::strcmp(n, name) == 0)
                return true;
        return false;
    }
}
