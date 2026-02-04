#include "create_transaction.hpp"
#include "general/hex.hpp"
#include "general/writer.hpp"
#include "nlohmann/json.hpp"

namespace {
std::string dump(nlohmann::json j)
{
    return j.dump(1);
}
}
WartTransferCreate::operator std::string()
{
    return dump({
        { "type", "wartTransfer" },
        { "feeStr", compact_fee().to_string() },
        { "nonceId", nonce_id().value() },
        { "pinHeight", pin_height().value() },
        { "signature65", signature().to_string() },
        { "toAddr", to_addr().to_string() },
        { "wartStr", wart().to_string() },
    });
}

TokenTransferCreate::operator std::string()
{
    return dump({
        { "type", "tokenTransfer" },
        { "amountU64", amount().u64() },
        { "assetHash", serialize_hex(asset_hash()) },
        { "feeStr", compact_fee().to_string() },
        { "isLiquidity", is_liquidity() },
        { "nonceId", nonce_id().value() },
        { "pinHeight", pin_height().value() },
        { "signature65", signature().to_string() },
        { "toAddr", to_addr().to_string() },
    });
}
LimitSwapCreate::operator std::string()
{
    return dump({
        { "type", "limitSwap" },
        { "amountU64", amount().u64() },
        { "assetHash", serialize_hex(asset_hash()) },
        { "feeStr", compact_fee().to_string() },
        { "isBuy", buy() },
        { "limit", serialize_hex(to_bytes(limit())) },
        { "nonceId", nonce_id().value() },
        { "pinHeight", pin_height().value() },
        { "signature65", signature().to_string() },
    });
}
LiquidityDepositCreate::operator std::string()
{
    return dump({
        { "type", "liquidityDeposit" },
        { "amountU64", amount().u64() },
        { "assetHash", serialize_hex(asset_hash()) },
        { "feeStr", compact_fee().to_string() },
        { "nonceId", nonce_id().value() },
        { "pinHeight", pin_height().value() },
        { "signature65", signature().to_string() },
        { "wartStr", wart().to_string() },
    });
}
LiquidityWithdrawalCreate::operator std::string()
{
    return dump({
        { "type", "liquidityWithdrawal" },
        { "amountE8", shares().u64() },
        { "assetHash", serialize_hex(asset_hash()) },
        { "feeStr", compact_fee().to_string() },
        { "nonceId", nonce_id().value() },
        { "pinHeight", pin_height().value() },
        { "signature65", signature().to_string() },
    });
}
CancelationCreate::operator std::string()
{
    return dump({
        { "type", "cancelation" },
        { "cancelHeight", cancel_height().value() },
        { "cancelNonceId", cancel_nonceid().value() },
        { "feeStr", compact_fee().to_string() },
        { "nonceId", nonce_id().value() },
        { "pinHeight", pin_height().value() },
        { "signature65", signature().to_string() },
    });
}
AssetCreationCreate::operator std::string()
{
    return dump({
        { "type", "assetCreation" },
        { "feeStr", compact_fee().to_string() },
        { "name", asset_name().to_string() },
        { "nonceId", nonce_id().value() },
        { "pinHeight", pin_height().value() },
        { "precision", supply().precision.value() },
        { "signature65", signature().to_string() },
        { "supplyU64", supply().funds.u64() },
    });
}
