#include "validators.hpp"
#include "block/body/nonce.hpp"
#include "crypto/address.hpp"
#include "defi/uint64/price.hpp"
#include "general/funds.hpp"

bool wart_validator(std::string_view s)
{
    try {
        Wart::parse_throw(s);
        return true;
    } catch (...) {
        return false;
    }
}
bool nonwart_validator(std::string_view s)
{
    try {
        return !Wart::parse_throw(s).is_zero();
    } catch (...) {
        return false;
    }
}
bool fee_validator(std::string_view s)
{
    return wart_validator(s);
}

bool address_validator(std::string_view s)
{
    try {
        Address { s };
        return true;
    } catch (...) {
        return false;
    }
}

bool FundsValidator::operator()(std::string_view s) const
{
    return Funds_uint64::parse(s, prec)
        .transform([&](Funds_uint64 f) { return allowZero || !f.is_zero(); })
        .value_or(false);
}

bool nonce_id_validator(std::string_view s)
{
    return NonceId::try_parse(s).has_value();
}

bool LimitValidator::operator()(std::string_view s) const
{
    return Price_uint64::from_string_adjusted(s, basePrec, ceil)
        .has_value();
}
