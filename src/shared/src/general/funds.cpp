#include "funds.hpp"
#include "general/errors.hpp"
#include "general/params.hpp"
#include "general/reader.hpp"
#include "nlohmann/json.hpp"
#include "general/try_parse.hpp"
#include <cassert>
#include <charconv>
#include <cstring>
#include <limits>

TokenDecimals::TokenDecimals(Reader& r)
    : TokenDecimals(from_number_throw(r.uint8()))
{
}

Result<ParsedFunds> ParsedFunds::try_parse(std::string_view s)
{
    constexpr auto err { Error(EINV_FUNDS) };
    constexpr const size_t N { 20 }; // max uint64_t has 20 digits max
    char buf[N];
    size_t i { 0 };
    uint8_t digits { 0 };
    bool dotfound { false };
    for (auto c : s) {
        if (c >= '0' && c <= '9') {
            if (i >= N)
                return err; // too many digits
            buf[i++] = c;
            if (dotfound)
                digits += 1;
        } else if (c == '.') {
            if (dotfound)
                return err; // two dots
            dotfound = true;
        } else {
            return err; // neither dot nor digit
        }
    }
    auto v { ::try_parse<uint64_t>({ buf, buf + i }) };
    if (!v)
        return err;
    return ParsedFunds { *v, digits };
}

ParsedFunds::ParsedFunds(std::string_view s)
    : ParsedFunds([&s]() {
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
    }else if (p > d) {
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
    if (auto p { ParsedFunds::try_parse(s) }; p.has_value()) {
        if (auto prec { TokenDecimals::from_number(p->decimalPlaces) })
            return from_funds_decimal({ Funds_uint64::from_value_throw(p->v), *prec });
    }
    return Error(EINV_SUPPLY);
}

Funds_uint64::Funds_uint64(Reader& r)
    : FundsBase<Funds_uint64>(from_value_throw(r))
{
}

std::optional<Funds_uint64> Funds_uint64::parse(std::string_view s, TokenDecimals digits)
{
    auto fd { ParsedFunds::try_parse(s) };
    if (!fd)
        return {};
    return parse(*fd, digits);
}

std::optional<Funds_uint64> Funds_uint64::parse(ParsedFunds fd, TokenDecimals digits)
{
    if (fd.decimalPlaces > digits())
        return {};
    size_t zeros { size_t(digits()) - size_t(fd.decimalPlaces) };
    auto v { fd.v };

    for (size_t i { 0 }; i < zeros; ++i) {
        if (std::numeric_limits<uint64_t>::max() / 10 < v)
            return {};
        v *= 10;
    }
    return Funds_uint64::from_value(v);
}

nlohmann::json Supply::to_json() const
{
    return {
        { "str", to_string() },
        { "u64", this->funds.value() }
    };
}

Result<Wart> Wart::try_parse(std::string_view s)
{
    auto p { ParsedFunds::try_parse(s) };
    if (p)
        return try_parse(*p);
    return Error(EINV_WART);
}

Result<Wart> Wart::try_parse(ParsedFunds fd)
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
