#include "json_converter.hpp"
#include "nlohmann/json.hpp"
Address JSONConverter::to_addr() const
{
    try {
        return Address(json.at("toAddr").get<std::string>());
    } catch (...) {
        throw Error(EBADADDRESS);
    }
}
JSONConverter::operator ToAddrEl() const { return to_addr(); }
AssetHash JSONConverter::asset_hash() const
{
    try {
        auto h = json.at("assetHash").get<std::string>();
        if (auto ah { AssetHash::try_parse(h) })
            return *ah;
    } catch (...) {
    }
    throw Error(EPARSEHASH);
}
JSONConverter::operator AssetHashEl() const { return asset_hash(); }
auto JSONConverter::liquidity_flag() const
{
    try {
        return json.at("isLiquidity").get<bool>();
    } catch (...) {
    }
    throw Error(EBADLIQUIDITYFLAG);
}
JSONConverter::operator LiquidityFlagEl() const { return liquidity_flag(); }
NonceId JSONConverter::nonce_id() const
{
    try {
        return NonceId(json.at("nonceId").get<uint32_t>());
    } catch (...) {
    }
    throw Error(EBADNONCE);
}
JSONConverter::operator NonceIdEl() const { return nonce_id(); }
NonceId JSONConverter::cancel_nonce_id() const
{
    try {
        return NonceId(json.at("cancelNonceId").get<uint32_t>());
    } catch (...) {
    }
    throw Error(EBADCANCELNONCE);
}
JSONConverter::operator CancelNonceEl() const { return cancel_nonce_id(); }
CompactUInt JSONConverter::fee() const
{
    try {
        wrt::optional<Wart> fee;
        auto iter = json.find("feeStr");
        if (iter != json.end()) {
            fee = { Wart::parse_throw(iter->get<std::string>()) };
            if (!fee.has_value())
                goto error;
        }
        iter = json.find("feeE8");
        if (iter != json.end()) {
            if (fee.has_value())
                goto error; // exclusive, either "feeStr" or feeE8"
            fee = Wart::from_value(iter->get<uint64_t>());
            if (!fee.has_value())
                goto error;
        }

        auto compactFee { CompactUInt::compact(*fee) };
        bool exact = compactFee.uncompact() == *fee;
        if (!exact)
            throw Error(EINEXACTFEE);
        return compactFee;
    } catch (nlohmann::json::exception&) {
    }
error:
    throw Error(EBADFEE);
}
JSONConverter::operator CompactFeeEl() const
{
    return fee();
}
PinHeight JSONConverter::pin_height() const
{
    try {
        auto h = json.at("pinHeight").get<uint32_t>();
        return PinHeight(Height(h));
    } catch (...) {
    }
    throw Error(EBADHEIGHT);
}
JSONConverter::operator PinHeightEl() const
{
    return { pin_height() };
}
Wart JSONConverter::wart() const
{
    try {
        wrt::optional<Wart> f;
        auto iter = json.find("wartStr");
        if (iter != json.end()) {
            f = Wart::parse(iter->get<std::string>());
            if (!f.has_value())
                goto error;
        }
        iter = json.find("wartE8");
        if (iter != json.end()) {
            if (f.has_value())
                goto error; // exclusive, either "wartStr" or "wartE8"
            f = Wart::from_value(iter->get<uint64_t>());
        }

        if (f.has_value())
            return f.value();
    } catch (...) {
    }
error:
    throw Error(EBADAMOUNT);
}
JSONConverter::operator WartEl() const { return wart(); }
JSONConverter::operator NonzeroWartEl() const { return wart().nonzero_throw(); }
Funds_uint64 JSONConverter::amount(std::string_view key) const
{
    try {
        return json.at(key).get<uint64_t>();
    } catch (...) {
    }
    throw Error(EBADAMOUNT);
}

JSONConverter::operator AmountEl() const { return amount("amountU64"); }
JSONConverter::operator SharesEl() const { return amount("amountE8"); }
bool JSONConverter::buy() const
{
    try {
        return json.at("isBuy").get<bool>();
    } catch (...) {
    }
    throw Error(EBADBUYFLAG);
}
JSONConverter::operator BuyEl() const { return buy(); }
Price_uint64 JSONConverter::limit() const
{
    try {
        auto pricestr { json.at("limit").get<std::string>() };
        auto p { Price_uint64::from_string(pricestr) };
        if (p)
            return *p;
    } catch (...) {
    }
    throw Error(EBADPRICE);
}
JSONConverter::operator LimitPriceEl() const
{
    return limit();
}

RecoverableSignature JSONConverter::signature() const
{
    try {
        return RecoverableSignature(json.at("signature65").get<std::string>());
    } catch (...) {
        throw Error(EPARSESIG);
    }
}
JSONConverter::operator SignatureEl() const { return signature(); }
PinHeight JSONConverter::cancel_height() const
{
    try {
        auto ch { json.at("cancelHeight").get<uint32_t>() };
        auto h { Height(ch).pin_height() };
        if (h)
            return *h;
    } catch (...) {
    }
    throw Error(EBADCANCELHEIGHT);
}
JSONConverter::operator CancelHeightEl() const { return cancel_height(); }
AssetName JSONConverter::asset_name() const
{
    try {
        return AssetName(json.at("name").get<std::string>());
    } catch (...) {
    }
    throw Error(EASSETNAME);
}
JSONConverter::operator AssetNameEl() const { return asset_name(); }
Funds_uint64 JSONConverter::asset_supply_u64() const
{
    try {
        return Funds_uint64(json.at("supplyU64").get<uint64_t>());
    } catch (...) {
    }
    throw Error(EBADASSETUNITS);
}
TokenPrecision JSONConverter::asset_precision() const
{
    try {
        return TokenPrecision(json.at("precision").get<size_t>());
    } catch (...) {
    }
    throw Error(EBADASSETPRECISION);
}
FundsDecimal JSONConverter::asset_supply() const
{
    return FundsDecimal(
        asset_supply_u64(),
        asset_precision());
}
JSONConverter::operator AssetSupplyEl() const { return asset_supply(); }
