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
    bool allowZero;

public:
    constexpr FundsValidator(TokenPrecision prec, bool allowZero = false)
        : prec(prec)
        , allowZero(allowZero)
    {
    }
    bool operator()(std::string_view s) const;
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
