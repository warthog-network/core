#pragma once
#include "general/errors.hpp"
#include "general/result.hpp"
#include "general/serializer_fwd.hxx"
#include "general/with_uint64.hpp"
#include <cassert>

class TokenDecimals { // number of decimal places
private:
    uint8_t val;

    struct Creator { }; // used to prevent construction from outside
    constexpr TokenDecimals(uint8_t v, Creator)
        : val(v)
    {
    }

    static constexpr const uint8_t max { 18 };

public:
    static constexpr size_t byte_size() { return 1; }
    auto value() const { return val; }
    constexpr TokenDecimals(size_t v)
        : TokenDecimals(uint8_t(v), Creator())
    {
        if (v > max)
            throw Error(EBADASSETDECIMALS);
    }
    TokenDecimals(Reader& r);
    void serialize(RawSerializer auto&& s) const
    {
        s << val;
    }
    [[nodiscard]] std::string to_string() const
    {
        return std::to_string(int(val));
    }
    static const TokenDecimals zero;
    static const TokenDecimals WART;
    static const TokenDecimals LIQUIDITY;
    constexpr auto operator()() const { return val; }
    static constexpr Result<TokenDecimals> from_number(uint8_t v)
    {
        if (v > max)
            return Error(ETOKENDECIMALS);
        return TokenDecimals { v, Creator() };
    }
    static constexpr TokenDecimals from_number_throw(uint8_t v)
    {
        return from_number(v).value_or_throw();
    }
};

constexpr const TokenDecimals TokenDecimals::zero { 0 };
constexpr const TokenDecimals TokenDecimals::WART { 8 };
constexpr const TokenDecimals TokenDecimals::LIQUIDITY { 8 };

template <typename R>
class FundsBase : public IsUint64 {
public:
    constexpr FundsBase(uint64_t val)
        : IsUint64(val) { };
    static constexpr R zero() { return { 0 }; }
    static constexpr std::optional<R> from_value(uint64_t val) { return R(val); }
    static constexpr R from_value_throw(uint64_t val) { return R(val); }
    auto operator<=>(const FundsBase<R>&) const = default;

    constexpr bool is_zero() const { return val == 0; }
    // std::string format(std::string_view unit) const;

    void add_throw(R add)
    {
        *this = sum_throw(*this, add);
    }
    void add_assert(R add)
    {
        *this = sum_assert(*this, add);
    }

    static constexpr std::optional<R> sum(FundsBase<R> a, FundsBase<R> b)
    {
        auto s { a.val + b.val };
        if (s < a.val)
            return {};
        return from_value(s);
    }

    template <typename... T>
    static constexpr std::optional<R> sum(FundsBase<R> a, FundsBase<R> b, T&&... t)
    {
        auto s { sum(a, b) };
        if (!s.has_value())
            return {};
        return sum(*s, std::forward<T>(t)...);
    }

    template <typename... T>
    friend constexpr R sum_throw(FundsBase<R> a, T&&... t)
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
    friend constexpr std::optional<R> diff(FundsBase<R> a, FundsBase<R> b)
    {
        if (a.val < b.val)
            return {};
        return from_value(a.val - b.val);
    }
    [[nodiscard]] friend constexpr R diff_assert(FundsBase<R> a, FundsBase<R> b)
    {
        auto d { diff(a, b) };
        assert(d.has_value());
        return *d;
    }
    [[nodiscard]] friend constexpr R diff_throw(FundsBase<R> a, FundsBase<R> b)
    {
        auto d { diff(a, b) };
        if (!d.has_value())
            throw Error(EBALANCE);
        return *d;
    }
};

struct FundsDecimal;
class NonzeroFunds_uint64;
class NonzeroWart;
class Wart;
class Funds_uint64 : public FundsBase<Funds_uint64> {
public:
    using FundsBase<Funds_uint64>::FundsBase;
    Funds_uint64(Reader& r);
    std::string to_string() const = delete;
    auto operator<=>(const Funds_uint64&) const = default;
    [[nodiscard]] static std::optional<Funds_uint64> parse(std::string_view, TokenDecimals);
    [[nodiscard]] static std::optional<Funds_uint64> parse(FundsDecimal, TokenDecimals);
    static Funds_uint64 parse_throw(std::string_view, TokenDecimals);
    constexpr uint64_t u64() const { return val; };
    FundsDecimal to_decimal(TokenDecimals d) const;
    Wart as_wart() const;
    std::optional<NonzeroFunds_uint64> nonzero() const;
};
class NonzeroFunds_uint64 : public Funds_uint64 {
public:
    NonzeroWart as_wart() const;
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
    TokenDecimals decimals;
    constexpr static size_t byte_size() { return Funds_uint64::byte_size() + TokenDecimals::byte_size(); }

    [[nodiscard]] static Result<FundsDecimal> try_parse(std::string_view);
    FundsDecimal(std::string_view);
    auto mantissa() const { return funds; };

    bool adjust_decimals(TokenDecimals);
    void serialize(RawSerializer auto&& s) const
    {
        s << funds << decimals;
    }
    FundsDecimal(Reader& r)
        : FundsDecimal { r, r }
    {
    }
    FundsDecimal(Funds_uint64 funds, TokenDecimals decimals)
        : funds(std::move(funds))
        , decimals(std::move(decimals))
    {
    }
    static FundsDecimal zero() { return { 0, 0 }; }

    double to_double() const
    {
        double d(funds.value());
        for (size_t i = 0; i < size_t(decimals.value()); ++i)
            d *= 0.1;
        return d;
    }

    std::string to_string() const;
};

class AssetSupply : public FundsDecimal {
private:
    AssetSupply(FundsDecimal fd)
        : FundsDecimal(std::move(fd))
    {
    }

public:
    static Result<AssetSupply> from_funds_decimal(FundsDecimal);
    AssetSupply(Reader& r);
    static Result<AssetSupply> try_parse(std::string_view s);
};

inline FundsDecimal Funds_uint64::to_decimal(TokenDecimals d) const
{
    return { value(), d };
}
inline std::optional<NonzeroFunds_uint64> Funds_uint64::nonzero() const
{
    if (is_zero())
        return {};
    return *this;
}

struct Supply : public FundsDecimal {
    nlohmann::json to_json() const;
};

class Wart : public FundsBase<Wart> {
public:
    using FundsBase<Wart>::FundsBase;
    static constexpr TokenDecimals decimals { TokenDecimals::WART };
    Wart(Reader& r);
    [[nodiscard]] static constexpr Wart from_funds(Funds_uint64 f)
    {
        return Wart(f.value());
    }
    auto operator<=>(const Wart&) const = default;
    [[nodiscard]] static Result<Wart> try_parse(std::string_view);
    [[nodiscard]] static Result<Wart> try_parse(FundsDecimal);
    static Wart parse_throw(std::string_view);
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] constexpr uint64_t E8() const { return val; };
    [[nodiscard]] Result<NonzeroWart> nonzero() const;
    [[nodiscard]] NonzeroWart nonzero_throw() const;
    [[nodiscard]] NonzeroWart nonzero_assert() const;
    double to_double() const { return E8() * 0.00000001; }
    operator Funds_uint64() const
    {
        return Funds_uint64(E8());
    }

protected:
    // we use the more meaningful E8 instead
    uint64_t value() const = delete;
};

class NonzeroWart : public Wart {
    friend class Wart;
    explicit constexpr NonzeroWart(Wart f)
        : Wart(f)
    {
        assert(f != 0);
    }

public:
    static NonzeroWart assert_nonzero(Wart w)
    {
        return NonzeroWart(w);
    }
    NonzeroWart(Reader& r)
        : NonzeroWart(Wart(r).nonzero_throw())
    {
    }
    [[nodiscard]] static Result<NonzeroWart> try_parse(std::string_view s)
    {
        auto w { Wart::try_parse(s) };
        if (w)
            return w->nonzero();
        return w.error();
    }

    bool is_zero() const = delete;
    operator NonzeroFunds_uint64() const
    {
        return NonzeroFunds_uint64(Funds_uint64(*this));
    }
    auto operator<=>(const NonzeroWart&) const = default;
};

inline Wart Funds_uint64::as_wart() const
{
    return Wart::from_funds(*this);
}

inline NonzeroWart NonzeroFunds_uint64::as_wart() const
{
    return Wart::from_funds(*this).nonzero_assert();
};

inline NonzeroWart Wart::nonzero_assert() const
{
    return NonzeroWart(Wart(*this));
}

inline Result<NonzeroWart> Wart::nonzero() const
{
    if (val == 0)
        return Error(EZEROWART);
    return nonzero_assert();
}

inline NonzeroWart Wart::nonzero_throw() const
{
    return nonzero().value_or_throw();
}
