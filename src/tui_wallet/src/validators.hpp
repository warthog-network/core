#pragma once
#include "block/body/nonce.hpp"
#include "defi/token/asset.hpp"
#include "general/compact_uint.hpp"
#include "general/funds.hpp"
bool validate_wart(std::string_view);
bool validate_nonzerowart(std::string_view);
bool validate_fee(std::string_view);
bool validate_address(std::string_view);
bool validate_nonce_id(std::string_view);
bool validate_asset_supply(std::string_view);
std::optional<FundsDecimal> parse_asset_supply(std::string_view);
bool validate_asset_name(std::string_view);
Result<AssetName> parse_asset_name(std::string_view);

template <typename T>
struct Validator {
private:
    using ret_t = std::decay_t<decltype(T::try_parse(std::declval<std::string_view>()))>;
    std::optional<ret_t> parsed;

public:
    auto& value() const
    {
        return parsed.value().value();
    }

    bool validate(std::string_view s)
    {
        parsed = T::try_parse(s);
        return operator bool();
    }
    bool has_value() const
    {
        return parsed.has_value() && parsed->has_value();
    }
    auto validator()
    {
        return [this](std::string_view s) { return validate(s); };
    }
    operator bool() const { return has_value(); }
};
using AssetNameValidator = Validator<AssetName>;
using WartValidator = Validator<Wart>;
using NonzeroWartValidator = Validator<NonzeroWart>;
using FeeValidator = Validator<CompactUInt>;
using NonceValidator = Validator<NonceId>;
using AssetSupplyValidator = Validator<AssetSupply>;

class FundsValidator {
private:
    TokenDecimals prec;

    std::optional<Funds_uint64> parsed;

public:
    void set_prec(TokenDecimals prec) { this->prec = prec; }
    constexpr FundsValidator(TokenDecimals prec)
        : prec(prec)
    {
    }
    bool validate(std::string_view s)
    {
        parsed = Funds_uint64::parse(s, prec);
        return has_value();
    }

    auto& value() const
    {
        return parsed.value();
    }

    bool has_value() const
    {
        return parsed.has_value();
    }
    auto validator()
    {
        return [this](std::string_view s) { return validate(s); };
    }
    operator bool() const { return has_value(); }
};

class NonzeroFundsValidator {
private:
    TokenDecimals prec;

    std::optional<NonzeroFunds_uint64> parsed;

public:
    void set_prec(TokenDecimals prec) { this->prec = prec; }
    constexpr NonzeroFundsValidator(TokenDecimals prec)
        : prec(prec)
    {
    }
    bool validate(std::string_view s)
    {
        if (auto p { Funds_uint64::parse(s, prec) }) {
            parsed = p->nonzero();
        } else {
            parsed.reset();
        }
        return has_value();
    }

    auto& value() const
    {
        return parsed.value();
    }

    bool has_value() const
    {
        return parsed.has_value();
    }
    auto validator()
    {
        return [this](std::string_view s) { return validate(s); };
    }
    operator bool() const { return has_value(); }
};

class LimitValidator {
private:
    TokenDecimals basePrec;
    bool ceil;

public:
    LimitValidator(TokenDecimals basePrec, bool ceil)
        : basePrec(basePrec)
        , ceil(ceil)
    {
    }
    bool operator()(std::string_view s) const;
};
