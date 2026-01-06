#pragma once
#include "general/errors.hpp"
#include "general/params.hpp"
#include "general/serializer_fwd.hxx"
#include "general/with_uint64.hpp"
#include "wrt/optional.hpp"
#include "wrt/variant.hpp"
#include <cassert>

class TokenPrecision { // number of decimal places
private:
    uint8_t val;

    struct Creator { }; // used to prevent construction from outside
    constexpr TokenPrecision(uint8_t v, Creator)
        : val(v)
    {
    }

    static constexpr const uint8_t max { 18 };

public:
    static constexpr size_t byte_size() { return 1; }
    auto value() const { return val; }
    constexpr TokenPrecision(size_t v)
        : TokenPrecision(uint8_t(v), Creator())
    {
        if (v > max)
            throw Error(EBADASSETPRECISION);
    }
    TokenPrecision(Reader& r);

    void serialize(RawSerializer auto&& s) const
    {
        s << val;
    }
    static const TokenPrecision zero;
    static constexpr TokenPrecision digits8()
    {
        return 8;
    }
    constexpr auto operator()() const { return val; }
    static constexpr TokenPrecision from_number_throw(uint8_t v)
    {
        if (auto o { from_number(v) })
            return *o;
        throw Error(ETOKENPRECISION);
    }
    static constexpr wrt::optional<TokenPrecision> from_number(uint8_t v)
    {
        if (v > max)
            return {};
        return TokenPrecision { v, Creator() };
    }
};

class AssetPrecision: public TokenPrecision{
    using TokenPrecision::TokenPrecision;
};

constexpr const TokenPrecision TokenPrecision::zero { 0 };

struct ParsedFunds {
    [[nodiscard]] static wrt::optional<ParsedFunds> parse(std::string_view);
    ParsedFunds(std::string_view);
    ParsedFunds(uint64_t v, uint8_t decimalPlaces)
        : v(v)
        , decimalPlaces(decimalPlaces)
    {
    }
    std::string to_string() const;
    auto uint64() const { return v; };
    uint64_t v;
    uint8_t decimalPlaces;
};


template <typename R>
class FundsBase : public IsUint64 {
public:
    constexpr FundsBase(uint64_t val)
        : IsUint64(val) { };
    static constexpr R zero() { return { 0 }; }
    static constexpr wrt::optional<R> from_value(uint64_t val) { return R(val); }
    static constexpr R from_value_throw(uint64_t val) { return R(val); }
    auto operator<=>(const FundsBase<R>&) const = default;

    bool is_zero() const { return val == 0; }
    // std::string format(std::string_view unit) const;

    void add_throw(R add)
    {
        *this = sum_throw(*this, add);
    }
    void add_assert(R add)
    {
        *this = sum_assert(*this, add);
    }

    static wrt::optional<R> sum(FundsBase<R> a, FundsBase<R> b)
    {
        auto s { a.val + b.val };
        if (s < a.val)
            return {};
        return from_value(s);
    }

    template <typename... T>
    static wrt::optional<R> sum(FundsBase<R> a, FundsBase<R> b, T&&... t)
    {
        auto s { sum(a, b) };
        if (!s.has_value())
            return {};
        return sum(*s, std::forward<T>(t)...);
    }

    template <typename... T>
    friend R sum_throw(FundsBase<R> a, T&&... t)
    {
        auto s { sum(a, std::forward<T>(t)...) };
        if (!s.has_value())
            throw Error(EBALANCE);
        return *s;
    }

    template <typename... T>
    friend R sum_assert(FundsBase<R> a, T&&... t)
    {
        auto s { sum(a, std::forward<T>(t)...) };
        assert(s.has_value());
        return *s;
    }

    void subtract_assert(FundsBase<R> f)
    {
        *this = diff_assert(*this, f);
    }
    friend wrt::optional<R> diff(FundsBase<R> a, FundsBase<R> b)
    {
        if (a.val < b.val)
            return {};
        return from_value(a.val - b.val);
    }
    friend R diff_assert(FundsBase<R> a, FundsBase<R> b)
    {
        auto d { diff(a, b) };
        assert(d.has_value());
        return *d;
    }
    friend R diff_throw(FundsBase<R> a, FundsBase<R> b)
    {
        auto d { diff(a, b) };
        if (!d.has_value())
            throw Error(EBALANCE);
        return *d;
    }
};

struct FundsDecimal;
class Funds_uint64 : public FundsBase<Funds_uint64> {
public:
    using FundsBase<Funds_uint64>::FundsBase;
    Funds_uint64(Reader& r);
    auto operator<=>(const Funds_uint64&) const = default;
    [[nodiscard]] static wrt::optional<Funds_uint64> parse(std::string_view, TokenPrecision);
    [[nodiscard]] static wrt::optional<Funds_uint64> parse(ParsedFunds, TokenPrecision);
    static Funds_uint64 parse_throw(std::string_view, TokenPrecision);
    FundsDecimal to_decimal(TokenPrecision d) const;
};
class NonzeroFunds_uint64 : public Funds_uint64 {
public:
    constexpr NonzeroFunds_uint64(Funds_uint64 f)
        : Funds_uint64(f)
    {
        assert(f != 0);
    }
    NonzeroFunds_uint64(Reader& r)
        : Funds_uint64(r)
    {
        if (value() == 0)
            throw Error(EZEROAMOUNT);
    }
    bool is_zero() const = delete;
    auto operator<=>(const NonzeroFunds_uint64&) const = default;
};
struct FundsDecimal {
    Funds_uint64 funds;
    TokenPrecision precision;
    constexpr static size_t byte_size() { return Funds_uint64::byte_size() + TokenPrecision::byte_size(); }

    void serialize(RawSerializer auto&& s) const
    {
        s << funds << precision;
    }
    FundsDecimal(Reader& r)
        : FundsDecimal{r, r}
    {
    }
    FundsDecimal(Funds_uint64 funds, TokenPrecision precision)
        : funds(std::move(funds))
        , precision(std::move(precision))
    {
    }
    static FundsDecimal zero() { return { 0, 0 }; }
    std::string to_string() const;
};

inline FundsDecimal Funds_uint64::to_decimal(TokenPrecision d) const
{
    return { value(), d };
}

struct Supply : public FundsDecimal {
    nlohmann::json to_json() const;
};

class NonzeroWart;
class Wart : public FundsBase<Wart> {
public:
    using FundsBase<Wart>::FundsBase;
    static constexpr TokenPrecision precision { TokenPrecision::digits8() };
    Wart(Reader& r);
    [[nodiscard]] static constexpr Wart from_funds(Funds_uint64 f)
    {
        return Wart(f.value());
    }
    auto operator<=>(const Wart&) const = default;
    static wrt::optional<Wart> parse(std::string_view);
    static wrt::optional<Wart> parse(ParsedFunds);
    static Wart parse_throw(std::string_view);
    std::string to_string() const;
    uint64_t E8() const { return val; };
    NonzeroWart nonzero_throw() const;
    NonzeroWart nonzero_assert() const;
    operator Funds_uint64() const
    {
        return Funds_uint64(E8());
    }

protected:
    // we use the more meaningful E8 instead
    uint64_t value() const = delete;
};

class NonzeroWart : public Wart {
public:
    explicit constexpr NonzeroWart(Wart f)
        : Wart(f)
    {
        assert(f != 0);
    }
    NonzeroWart(Reader& r)
        : Wart(r)
    {
        if (val == 0)
            throw Error(EZEROAMOUNT);
    }

    bool is_zero() const = delete;
    operator NonzeroFunds_uint64() const
    {
        return NonzeroFunds_uint64(Funds_uint64(*this));
    }
    auto operator<=>(const NonzeroWart&) const = default;
};

inline NonzeroWart Wart::nonzero_assert() const
{
    return NonzeroWart(*this);
}

inline NonzeroWart Wart::nonzero_throw() const
{
    if (val == 0)
        throw Error(EZEROWART);
    return nonzero_assert();
}
