#include "funds.hpp"
#include "general/errors.hpp"
#include "general/params.hpp"
#include "general/reader.hpp"
#include "general/try_parse.hpp"
#include <cassert>
#include <cstring>
#include <limits>

TokenDecimals::TokenDecimals(Reader& r)
    : TokenDecimals(from_number_throw(r.uint8()))
{
}

Result<FundsDecimal> FundsDecimal::try_parse(std::string_view s)
{
    constexpr auto err { Error(EINV_FUNDS) };
    constexpr const size_t N { 20 }; // max uint64_t has 20 digits max
    char buf[N];
    size_t i { 0 };
    uint8_t decimals { 0 }; // number of decimal places
    bool dotfound { false };
    for (auto c : s) {
        if (c >= '0' && c <= '9') {
            if (i >= N)
                return err; // too many digits
            buf[i++] = c;
            if (dotfound)
                decimals += 1;
        } else if (c == '.') {
            if (dotfound)
                return err; // two dots
            dotfound = true;
        } else {
            return err; // neither dot nor digit
        }
    }
    auto v { ::try_parse<uint64_t>({ buf, buf + i }) };
    auto td { TokenDecimals::from_number(decimals) };
    if (v && td)
        return FundsDecimal { Funds_uint64 { *v }, *td };
    return err;
}

bool FundsDecimal::adjust_decimals(TokenDecimals newDecimals)
{
    size_t vd { decimals.value() };
    size_t newvd { newDecimals.value() };
    if (vd < newvd) {
        size_t zeros { newvd - vd };
        auto v { mantissa().value() };

        for (size_t i { 0 }; i < zeros; ++i) {
            if (std::numeric_limits<uint64_t>::max() / 10 < v)
                return {};
            v *= 10;
        }
        funds = Funds_uint64(v);
        decimals = newDecimals;
    } else if (vd > newvd) {
        size_t zeros { vd - newvd };
        uint64_t pow10 { 10 };
        for (size_t i { 1 }; i < zeros; ++i)
            pow10 *= 10;

        const auto v { mantissa().value() };
        auto q { v / pow10 };
        auto mod { v % pow10 };
        if (mod != 0) { // would lose precision
            return false;
        }

        funds = Funds_uint64(q);
        decimals = newDecimals;
    }
    return true;
}

FundsDecimal::FundsDecimal(std::string_view s)
    : FundsDecimal([&s]() {
        if (auto p { try_parse(s) })
            return *p;
        throw Error(EINV_FUNDS);
    }())
{
}

Funds_uint64 Funds_uint64::parse_throw(std::string_view s, TokenDecimals d)
{
    if (auto o { Funds_uint64::parse(s, d) }; o.has_value()) {
        return *o;
    }
    throw Error(EINV_FUNDS);
}

std::string FundsDecimal::to_string() const
{
    const size_t d { decimals() };
    auto v { funds.value() };
    if (v == 0)
        return "0";
    static_assert(COINUNIT == 100000000);
    std::string s { std::to_string(v) };
    size_t p = s.size();
    if (d == 0) {
        return s;
    } else if (p > d) {
        s.resize(p + 1);
        for (size_t i = 0; i < d; ++i)
            s[p - i] = s[p - i - 1];
        s[p - d] = '.';
        return s;
    } else {
        size_t z = d - p;
        std::string tmp;
        tmp.resize(2 + d);
        tmp[0] = '0';
        tmp[1] = '.';
        for (size_t i = 0; i < z; ++i)
            tmp[2 + i] = '0';
        memcpy(&tmp[2 + z], s.data(), p);
        return tmp;
    }
}

AssetSupply::AssetSupply(Reader& r)
    : AssetSupply(from_funds_decimal(FundsDecimal { r }).value_or_throw())
{
}

Result<AssetSupply> AssetSupply::from_funds_decimal(FundsDecimal f)
{
    uint64_t prod = 1;
    for (size_t i { 0 }; i < size_t(f.decimals.value()); ++i) {
        prod *= 10;
    }
    if (prod > f.funds.value())
        return Error(EINV_SUPPLY);
    return AssetSupply(std::move(f));
}

Result<AssetSupply> AssetSupply::try_parse(std::string_view s)
{
    if (auto p { FundsDecimal::try_parse(s) }; p.has_value()) {
        return from_funds_decimal(*p);
    }
    return Error(EINV_SUPPLY);
}

Funds_uint64::Funds_uint64(Reader& r)
    : FundsBase<Funds_uint64>(from_value_throw(r))
{
}

std::optional<Funds_uint64> Funds_uint64::parse(std::string_view s, TokenDecimals digits)
{
    if (auto fd { FundsDecimal::try_parse(s) })
        return parse(*fd, digits);
    return {};
}

std::optional<Funds_uint64> Funds_uint64::parse(FundsDecimal fd, TokenDecimals digits)
{
    if (fd.decimals.value() > digits.value() || !fd.adjust_decimals(digits))
        return {};
    return fd.mantissa();
}

Result<Wart> Wart::try_parse(std::string_view s)
{
    auto p { FundsDecimal::try_parse(s) };
    if (p)
        return try_parse(*p);
    return Error(EINV_WART);
}

Result<Wart> Wart::try_parse(FundsDecimal fd)
{
    auto p { Funds_uint64::parse(fd, TokenDecimals::WART) };
    if (p)
        return Wart::from_value(p->value());
    return Error(EINV_WART);
}

Wart Wart::parse_throw(std::string_view s)
{
    if (auto o { try_parse(s) }; o.has_value()) {
        return *o;
    }
    throw Error(EINV_FUNDS);
}

std::string Wart::to_string() const
{
    return FundsDecimal { val, Wart::decimals }.to_string();
}

Wart::Wart(Reader& r)
    : FundsBase<Wart>(from_value_throw(r))
{
}
