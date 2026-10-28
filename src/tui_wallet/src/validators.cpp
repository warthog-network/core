#include "validators.hpp"
#include "block/body/nonce.hpp"
#include "crypto/address.hpp"
#include "defi/uint64/price.hpp"
#include "general/funds.hpp"

bool validate_wart(std::string_view s)
{
    try {
        Wart::parse_throw(s);
        return true;
    } catch (...) {
        return false;
    }
}
bool nonzero_wart_validator(std::string_view s)
{
    try {
        return !Wart::parse_throw(s).is_zero();
    } catch (...) {
        return false;
    }
}
bool validate_fee(std::string_view s)
{
    return validate_wart(s);
}

bool validate_address(std::string_view s)
{
    try {
        Address { s };
        return true;
    } catch (...) {
        return false;
    }
}

bool validate_nonce_id(std::string_view s)
{
    return NonceId::try_parse(s).has_value();
}

bool LimitValidator::operator()(std::string_view s) const
{
    return Price_uint64::from_string_adjusted(s, basePrec, ceil)
        .has_value();
}

bool validate_asset_supply(std::string_view s)
{
    return parse_asset_supply(s).has_value();
}
std::optional<FundsDecimal> parse_asset_supply(std::string_view s)
{
    if (auto p { FundsDecimal::try_parse(s) }) {
        return *p;
    }
    return {};
}

bool validate_asset_name(std::string_view s)
{
    return parse_asset_name(s).has_value();
}

Result<AssetName> parse_asset_name(std::string_view s)
{
    return AssetName::try_parse(s);
}
