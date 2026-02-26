#pragma once
#include "prod.hpp"
#include "tools/try_parse.hpp"
#include "wrt/optional.hpp"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
struct Price_uint64 {
private:
    Price_uint64(uint16_t m, uint8_t e)
        : _e(e)
        , _m(m)
    {
    }

    [[nodiscard]] static wrt::optional<Price_uint64> compose(auto mantissa, int e)
    {
        if (!is_exponent(e) || !is_mantissa(mantissa))
            return {};
        return Price_uint64(mantissa, e);
    }

public:
    static constexpr size_t byte_size() { return 3; }
    static auto from_uint32(uint32_t data)
    {
        return compose(data & 0x0000FFFFu, data >> 16);
    }
    static Price_uint64 from_bytes(std::span<const uint8_t, 3> s);
    static Price_uint64 from_hex(std::string_view s);
    Price_uint64(Reader& r);
    void serialize(RawSerializer auto& s) const
    {
        s << _m << _e;
    }
    uint32_t to_uint32() const
    {
        return uint32_t(_m) + (uint32_t(_e) << 16);
    }
    [[nodiscard]] static Price_uint64 zero() { return Price_uint64 { 0, 0 }; }
    [[nodiscard]] static Price_uint64 max() { return Price_uint64 { 0xFFFFu, 127 }; }
    [[nodiscard]] static bool is_mantissa(uint32_t m)
    {
        return m <= mantissaMask && ((m >> (mantissaPrecision - 1)) != 0);
    }
    [[nodiscard]] static bool is_exponent(auto e) { return e >= 0 && e < 128; }
    static constexpr auto mantissaPrecision { 16 };
    static constexpr auto mantissaMask { (uint32_t(1) << mantissaPrecision) - 1 };
    uint16_t mantissa_16bit() const { return _m; }

private:
    // base 2 exponent if mantissa would denote a number between 0 and 1
    int exponent_base2() const { return _e - 63; }

public:
    // real exponent for mantissa as integer
    int mantissa_exponent2() const { return exponent_base2() - 16; }

    // compute double price based on uint64 quotient
    double to_double_raw() const
    {
        auto m { mantissa_16bit() };
        auto e2 { mantissa_exponent2() };
        return std::ldexp(m, e2);
    }

    int base10_precision_exponent(TokenPrecision prec) const
    {
        // - real limit price is quote/base
        // - limit price variable is quoteU64/baseU64
        //   and does not respect precision
        // => We must take precision difference into account for real limit price
        return int(TokenPrecision::WART.value()) - int(prec.value());
    }

    // compute double price respecting the asset precision
    double to_double_adjusted(TokenPrecision prec) const
    {
        auto b10e { base10_precision_exponent(prec) };
        return to_double_raw() * std::pow(10.0, -b10e);
    }
    [[nodiscard]] static wrt::optional<Price_uint64>
    from_mantissa_exponent(uint32_t mantissa, int exponent)
    {
        exponent += 63;
        return compose(mantissa, exponent);
    }

    wrt::optional<Price_uint64> prev_step() const
    {
        auto m { _m - 1 };
        if (is_mantissa(m))
            return Price_uint64(m, _e);
        m = (_m << 1) - 1;
        assert(is_mantissa(m));
        auto e { _e - 1 };
        if (!is_exponent(e))
            return {};
        return Price_uint64(m, e);
    }
    wrt::optional<Price_uint64> next_step() const
    {
        auto m { _m + 1 };
        if (is_mantissa(m))
            return Price_uint64(m, _e);
        m >>= 1;
        auto e { _e + 1 };
        if (!is_exponent(e)) // cannot represent with 8 bits exponent
            return {};
        return Price_uint64(m, e);
    }

    auto operator<=>(const Price_uint64&) const = default;

    static wrt::optional<Price_uint64> from_double_adjusted(double d, TokenPrecision basePrec, bool ceil = false)
    {
        return from_double(d * std::pow(10.0, 8 - int(basePrec.value())), ceil);
    }

    static wrt::optional<Price_uint64> from_double(double d, bool ceil = false)
    {
        if (d <= 0.0 || !std::isnormal(d))
            return {};
        int exp;
        double mantissa { std::frexp(d, &exp) };
        uint32_t mantissa32(mantissa * (1 << 16));
        bool exact { (mantissa * (1 << 16)) == double(mantissa32) };
        if (ceil && !exact) {
            mantissa32 += 1;
            if (mantissa32 >= 1 << 16) { // carry
                mantissa32 >>= 1;
                exp += 1;
            }
        }
        return from_mantissa_exponent(mantissa32, exp);
    }

    static wrt::optional<Price_uint64> from_string(std::string_view s, bool ceil = false)
    {
        return try_parse<double>(s)
            .and_then([&](double d) { return from_double(d, ceil); });
    }
    static wrt::optional<Price_uint64> from_string_adjusted(std::string_view s, TokenPrecision prec, bool ceil = false)
    {
        return try_parse<double>(s)
            .and_then([&](double d) { return from_double_adjusted(d, prec, ceil); });
    }

private:
    uint8_t _e; // exponent
    uint16_t _m; // mantissa
};

struct PriceRelative_uint64 { // gives details relative to price grid
    PriceRelative_uint64(Price_uint64 price, bool exact = true)
        : price(std::move(price))
        , exact(std::move(exact))
    {
    }
    auto operator<=>(Price_uint64 p2) const
    {
        if (!exact && price == p2)
            return std::strong_ordering::greater;
        return price.operator<=>(p2);
    }
    const Price_uint64& floor() const { return price; }
    wrt::optional<Price_uint64> ceil() const
    {
        if (exact) {
            return price;
        }
        return price.next_step();
    }
    auto& operator=(Price_uint64 p) { return *this = PriceRelative_uint64 { p }; }
    auto operator<=>(PriceRelative_uint64 p2) const
    {
        auto rel { price.operator<=>(p2.price) };
        if (rel == std::strong_ordering::equal) {
            if (exact && !p2.exact)
                return std::strong_ordering::less;
            if (!exact && p2.exact)
                return std::strong_ordering::greater;
        }
        return rel;
    }
    [[nodiscard]] static wrt::optional<PriceRelative_uint64> from_fraction(uint64_t numerator,
        uint64_t denominator)
    { // OK
        if (numerator == 0) {
            if (denominator == 0)
                return {}; // no price for degenerate pool (no liquidity at all)
            return PriceRelative_uint64 { Price_uint64::zero(), true };
        } else if (denominator == 0)
            return PriceRelative_uint64 { Price_uint64::max(), false };

        int e { 0 };
        { // shift numerator
            auto z { std::countl_zero(numerator) };
            e -= z;
            numerator <<= z;
        }
        { // shift denominator
            auto z { std::countl_zero(denominator) };
            denominator <<= z;
            e += z;
        }

        constexpr uint64_t shiftr { (Price_uint64::mantissaPrecision) };
        constexpr uint64_t mask { (1 << shiftr) - 1 };
        auto denominatorRest = denominator & mask;
        denominator >>= shiftr;

        uint64_t d { numerator / denominator };
        const auto rest { numerator - d * denominator };
        const auto dr { d * denominatorRest };
        bool exact { (dr & mask) == 0 };
        const auto subtract { dr >> shiftr };
        if (rest < subtract || (rest == subtract && !exact)) {
            d -= 1;
        }
        if (rest != subtract)
            exact = false;
        auto r { 64 - std::countl_zero(d) };
        if (r != Price_uint64::mantissaPrecision) {
            if ((d & 1) != 0)
                exact = false;
            d >>= 1;
            e += 1;
            r -= 1;
        }
        assert(r == Price_uint64::mantissaPrecision);

        auto p { Price_uint64::from_mantissa_exponent(d, e) };
        assert(p);
        return PriceRelative_uint64 { *p, exact };
    }
    bool operator==(Price_uint64 p2) const { return exact && price == p2; }

private:
    Price_uint64 price;
    bool exact;
};

inline wrt::optional<uint64_t> divide(uint64_t a, Price_uint64 p, bool ceil)
{ // OK
    if (a == 0)
        return 0ull;
    auto z1 { std::countl_zero(a) };
    a <<= z1;
    uint64_t d { a / p.mantissa_16bit() };
    assert(d != 0);
    uint64_t prod { (d * p.mantissa_16bit()) };
    const auto z2 { std::countl_zero(d) };
    uint64_t rest { (a - prod) << z2 };
    auto d2 { rest / p.mantissa_16bit() };
    bool inexact = d2 * p.mantissa_16bit() != rest;
    d = (d << z2) + d2;
    auto shift { -(p.mantissa_exponent2() + z1 + z2) };
    if (shift > 0)
        return {}; // overflow
    shift = -shift;
    if (shift >= 64) {
        if (d != 0)
            inexact = true;
        return 0 + (ceil && inexact);
    }
    if ((d << (64 - shift)) != 0) {
        inexact = true;
    }
    auto res { (d >> shift) + (ceil && inexact) };
    if (res == 0)
        return {}; // overflow
    return res;
}

[[nodiscard]] inline wrt::optional<uint64_t> divide_floor(uint64_t a, Price_uint64 p)
{
    return divide(a, p, false);
}
[[nodiscard]] inline wrt::optional<uint64_t> divide_ceil(uint64_t a, Price_uint64 p)
{
    return divide(a, p, true);
}
inline wrt::optional<uint64_t> multiply_floor(uint64_t a, Price_uint64 p)
{
    return Prod128(p.mantissa_16bit(), a).pow2_64(p.mantissa_exponent2(), false);
}

inline wrt::optional<uint64_t> multiply_ceil(uint64_t a, Price_uint64 p)
{
    return Prod128(p.mantissa_16bit(), a).pow2_64(p.mantissa_exponent2(), true);
}
inline std::strong_ordering compare_fraction(Ratio128 ratio, Price_uint64 p)
{ // compares ratio with p
    auto a { ratio.numerator };
    auto b { ratio.denominator };
    auto z { -p.mantissa_exponent2() };
    auto pb { b * p.mantissa_16bit() };
    auto za { a.countl_zero() };
    auto zb { pb.countl_zero() };
    z -= za;
    z += zb - 64;
    if (z < 0)
        return std::strong_ordering::less;
    if (z > 0)
        return std::strong_ordering::greater;
    a <<= za;
    pb <<= zb;
    Prod192 pa { a, 0ull };
    return pa <=> pb;
}
