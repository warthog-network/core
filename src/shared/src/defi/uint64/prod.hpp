#pragma once
#include "general/funds.hpp"
#include "nonzero.hpp"
#include "wrt/optional.hpp"
#include <bit>
#include <cassert>
#include <cstdint>
class Prod192;
inline uint64_t shiftl(uint64_t upper, uint64_t lower, unsigned int i)
{
    return (upper << i) + (lower >> (64 - i));
}
class Prod128 {
public:
    Prod128(Funds_uint64 a, Funds_uint64 b)
        : Prod128(a.value(), b.value())
    {
    }
    Prod128(uint64_t a, uint64_t b)
    { // OK
        const uint64_t a0 { a >> 32 };
        const uint64_t a1 { a & 0xFFFFFFFFul };
        const uint64_t b0 { b >> 32 };
        const uint64_t b1 { b & 0xFFFFFFFFul };

        const uint64_t m00 { (a0 * b0) };
        const uint64_t m01 { (a0 * b1) };
        const uint64_t m10 { (a1 * b0) };
        const uint64_t m11 { (a1 * b1) };

        const uint64_t overlap3 = ((m01 & 0xFFFFFFFFull) + (m10 & 0xFFFFFFFFul) + (m11 >> 32));

        upper = m00 + ((m01 >> 32) + (m10 >> 32) + (overlap3 >> 32));
        lower = (m11 & 0xFFFFFFFFul) + (overlap3 << 32);
    }
    Prod192 operator*(uint64_t);
    auto operator<=>(const Prod128&) const = default;
    Prod128& operator<<=(unsigned int i)
    {
        switch (i >> 6) {
        case 0:
            upper = shiftl(upper, lower, i);
            lower <<= i;
            break;
        case 1:
            i = i & 63;
            upper = lower << i;
            lower = 0;
            break;
        default:
            upper = 0;
            lower = 0;
        }
        return *this;
    }

    size_t countl_zero() const
    {
        auto z { std::countl_zero(upper) };
        if (upper == 0) {
            z += std::countl_zero(lower);
        }
        return z;
    }
    bool is_zero() const { return upper == 0 && lower == 0; }
    wrt::optional<uint64_t> pow2_64(int shiftExp, bool ceil) const
    { // OK
        if (is_zero())
            return 0;
        if (shiftExp >= 0) {
            if (shiftExp >= 64 || upper != 0)
                return {}; // overflow
            if (shiftExp == 0)
                return lower;
            if ((lower >> (64 - shiftExp)) != 0)
                return {}; // overflow
            return lower << shiftExp;
        } else { // (shiftExp < 0)
            bool inexact = false;
            shiftExp = -shiftExp;
            if (shiftExp >= 64) {
                if (lower != 0)
                    inexact = true;
                shiftExp -= 64;
                if (shiftExp >= 64)
                    return (lower != 0 || upper != 0 ? 1 : 0);
                if ((upper << (64 - shiftExp)) != 0)
                    inexact = true;
                return (upper >> shiftExp) + (ceil && inexact);
            } else {
                if ((upper >> shiftExp) != 0)
                    return {}; // overflow
                if ((lower << (64 - shiftExp)) != 0)
                    inexact = true;
                auto res { (upper << (64 - shiftExp)) + (lower >> shiftExp) + (ceil && inexact) };
                if (res == 0)
                    return {}; // overflow because of ceiling
                return res;
            }
        }
    }

    [[nodiscard]] uint64_t sqrt() const
    {
        int shift { upper != 0 ? std::countl_zero(upper)
                               : 64 + std::countl_zero(lower) };

        auto digits { 128 - shift };
        if (digits == 0)
            return 0;
        size_t pos = ((digits + 1) / 2) - 1;
        uint64_t u { upper };
        uint64_t l { lower };
        uint64_t tmp { 0 };
        while (true) {
            uint64_t tmp0 { u };
            uint64_t tmp1 { l };
            tmp0 -= tmp >> (63 - pos);
            uint64_t subtract1 = tmp << (pos + 1);
            if (pos < 32)
                subtract1 += uint64_t(1) << (2 * pos);
            else
                tmp0 -= uint64_t(1) << (2 * pos - 64);

            tmp0 -= (tmp1 < subtract1); // carry bit
            tmp1 -= subtract1;
            if (tmp0 <= u) { // can set the pos bit in tmp
                u = tmp0;
                l = tmp1;
                tmp |= uint64_t(1) << pos;
            }
            if (pos == 0)
                return tmp;
            pos -= 1;
        }
    }
    // returns wrt::nullopt on overflow
    [[nodiscard]] wrt::optional<uint64_t> divide_floor(Nonzero_uint64 v) const
    {
        return div(v, false);
    }
    // returns wrt::nullopt on overflow
    [[nodiscard]] wrt::optional<uint64_t> divide_ceil(uint64_t v) const
    {
        return div(v, true);
    }
    auto v0() const { return upper; }
    auto v1() const { return lower; }

private:
    [[nodiscard]] wrt::optional<uint64_t> div(uint64_t v, bool ceil) const
    {
        assert(v != 0);
        if (upper == 0)
            return (lower / v) + (ceil && (lower % v != 0) ? 1 : 0);
        auto shift { std::countl_zero(upper) };
        uint64_t t0 { (upper << shift) + (lower >> (64 - shift)) };
        uint64_t t1 { lower << shift };
        assert((t0 & 0x8000000000000000ull) != 0);
        bool carry { false };
        uint64_t tmp { 0 };
        const size_t I = (64 - shift) + 1;
        for (size_t i { 0 }; i < I; ++i) {
            if (tmp & 0x8000000000000000ull) {
                return {}; // overflow
            }
            tmp <<= 1;
            if (carry) {
                carry = false;
                t0 -= v;
                tmp += 1;
            } else if (t0 >= v) {
                uint64_t c { t0 / v };
                t0 -= c * v;
                tmp += c;
            } else {
            }

            // shift to left
            if (t0 & 0x8000000000000000ull) {
                carry = true;
            }
            t0 <<= 1;
            if (t1 & 0x8000000000000000ull) {
                t0 += 1;
            }
            t1 <<= 1;
        }
        if (ceil && t0 > 0) {
            tmp += 1;
        }
        return tmp;
    }

protected:
    uint64_t upper;
    uint64_t lower;
};

struct Ratio128 {
    Prod128 numerator, denominator;
};

class Prod192 : protected Prod128 {
public:
    auto operator<=>(const Prod192&) const = default;
    auto operator<<=(unsigned int i)
    {
        auto s { i & 63 };
        switch (i >> 6) {
        case 0:
            upper = shiftl(upper, lower, s);
            lower = shiftl(lower, lowest, s);
            lowest = lowest << s;
            break;
        case 1:
            upper = shiftl(lower, lowest, s);
            lower = lowest << s;
            lowest = 0;
            break;
        case 2:
            upper = lowest << s;
            lower = 0;
            lowest = 0;
            break;
        default:;
            upper = 0;
            lower = 0;
            lowest = 0;
        }
        return *this;
    }
    Prod192(Prod128 uppermid, uint64_t lowest)
        : Prod128(std::move(uppermid))
        , lowest(lowest)
    {
    }
    size_t countl_zero() const
    {
        auto z { Prod128::countl_zero() };
        if (z == 128)
            z += std::countl_zero(lowest);
        return z;
    }

private:
    uint64_t lowest;
};

inline Prod192 Prod128::operator*(uint64_t a)
{
    Prod128 pl(lower, a);
    Prod128 pu(upper, a);
    pu.lower += pl.upper;
    pu.upper += (pu.lower < pl.upper);
    return { pu, pl.lower };
};
