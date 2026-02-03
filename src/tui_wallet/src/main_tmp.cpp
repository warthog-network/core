
// #include "create_transaction.hpp"
// #include "general/hex.hpp"
// #include "nlohmann/json.hpp"
// WartTransferCreate::operator std::string()
// {
//     return nlohmann::json {
//         { "type", "wartTransfer" },
//         { "feeStr", compact_fee().to_string() },
//         { "nonceId", nonce_id().value() },
//         { "pinHeight", pin_height().value() },
//         { "signature65", signature().to_string() },
//         { "toAddr", to_addr().to_string() },
//         { "wartStr", wart().to_string() },
//     }
//         .dump(1);
// }
//
// TokenTransferCreate::operator std::string()
// {
//     return nlohmann::json {
//         { "type", "tokenTransfer" },
//         { "amountU64", amount().u64() },
//         { "assetHash", serialize_hex(asset_hash()) },
//         { "feeStr", compact_fee().to_string() },
//         { "isLiquidity", is_liquidity() },
//         { "nonceId", nonce_id().value() },
//         { "pinHeight", pin_height().value() },
//         { "signature65", signature().to_string() },
//         { "toAddr", to_addr().to_string() },
//     }
//         .dump(1);
// }
// LimitSwapCreate::operator std::string()
// {
//     return nlohmann::json {
//         { "type", "limitSwap" },
//         { "amountU64", amount().u64() },
//         { "assetHash", serialize_hex(asset_hash()) },
//         { "feeStr", compact_fee().to_string() },
//         { "isBuy", buy() },
//         { "limit", serialize_hex(limit().to_uint32()) },
//         { "nonceId", nonce_id().value() },
//         { "pinHeight", pin_height().value() },
//         { "signature65", signature().to_string() },
//     }
//         .dump(1);
// }
// LiquidityDepositCreate::operator std::string()
// {
//     return nlohmann::json {
//         { "type", "liquidityDeposit" },
//         { "amountU64", amount().u64() },
//         { "assetHash", serialize_hex(asset_hash()) },
//         { "feeStr", compact_fee().to_string() },
//         { "nonceId", nonce_id().value() },
//         { "pinHeight", pin_height().value() },
//         { "signature65", signature().to_string() },
//         { "wartStr", wart().to_string() },
//     }
//         .dump(1);
// }
// LiquidityWithdrawalCreate::operator std::string()
// {
//     return nlohmann::json {
//         { "type", "LiquidityWithdrawal" },
//         { "amountE8", shares().u64() },
//         { "assetHash", serialize_hex(asset_hash()) },
//         { "feeStr", compact_fee().to_string() },
//         { "nonceId", nonce_id().value() },
//         { "pinHeight", pin_height().value() },
//         { "signature65", signature().to_string() },
//     }
//         .dump(1);
// }
// CancelationCreate::operator std::string()
// {
//     return nlohmann::json {
//         { "type", "cancelation" },
//         { "cancelHight", cancel_height().value() },
//         { "cancelNonceId", cancel_nonceid().value() },
//         { "feeStr", compact_fee().to_string() },
//         { "nonceId", nonce_id().value() },
//         { "pinHeight", pin_height().value() },
//         { "signature65", signature().to_string() },
//     }
//         .dump(1);
// }
// AssetCreationCreate::operator std::string()
// {
//     return nlohmann::json {
//         { "type", "assetCreation" },
//         { "feeStr", compact_fee().to_string() },
//         { "name", asset_name().to_string() },
//         { "nonceId", nonce_id().value() },
//         { "pinHeight", pin_height().value() },
//         { "precision", supply().precision.value() },
//         { "signature65", signature().to_string() },
//         { "supplyU64", supply().funds.u64() },
//     }
//         .dump(1);
// }
