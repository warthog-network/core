#pragma once
#include "general/funds.hpp"
bool wart_validator(std::string_view);
bool nonzerowart_validator(std::string_view);
bool fee_validator(std::string_view);
bool address_validator(std::string_view);
bool nonce_id_validator(std::string_view);

class FundsValidator {
private:
    TokenPrecision prec;

public:
    constexpr FundsValidator(TokenPrecision prec)
        : prec(prec)
    {
    }
    bool valid(std::string_view s, bool allowsZero) const;
};
class NonzeroFundsValidator : public FundsValidator {
public:
    constexpr NonzeroFundsValidator(TokenPrecision prec)
        : FundsValidator(prec)
    {
    }
    bool operator()(std::string_view s) const
    {
        return valid(s, false);
    }
};
class LimitValidator {
private:
    TokenPrecision basePrec;
    bool ceil;

public:
    LimitValidator(TokenPrecision basePrec, bool ceil)
        : basePrec(basePrec)
        , ceil(ceil)
    {
    }
    bool operator()(std::string_view s) const;
};
