#include "retrieval_context.hpp"
// Address toAddr("3661579d61abde5837a8686dc4d65348a2fc61b1fe5f4093");
// NonzeroWart wart { 1 };
// TransactionCreateContext ctx= global::wallet(). {
//     .pinHash { pinHash },
//     .pinHeight { pinHeight },
//     .nonceId { 0 },
//     .compactFee { CompactUInt::compact(Wart(0)) },
//     .pk { global::wallet().privKey }
// };
// WartTransferCreate c1(ctx, toAddr, wart);
//
// AssetHash ah { AssetHash::parse_throw("0e4825efffa294610d2ac376713e3bcc9b53d378e823834b64e5df01f75d3b0c") };
// Funds_uint64 amount(1);
// Funds_uint64 shares(1);
// TokenTransferCreate c2(ctx, ah, false, toAddr, amount);
// LimitSwapCreate c3(ctx, ah, true, amount, Price_uint64::from_double(1).value());
// LiquidityDepositCreate c4(ctx, ah, amount, wart);
// PinHeight cancelHeight(pinHeight);
// LiquidityWithdrawalCreate c5(ctx, ah, amount);
// CancelationCreate c6(ctx, cancelHeight, cancelNonce);
// AssetCreationCreate c7(ctx, assetSupply, assetName);
//
WartTransferCreate DataRetrievalContext::create_wart_transfer(NonceId nonceId, CompactUInt compactFee, const Address& toAddr, NonzeroWart wart) const
{
    return { tx_create_context(nonceId, compactFee), toAddr, wart };
}
TokenTransferCreate DataRetrievalContext::create_token_transfer(NonceId nonceId, CompactUInt compactFee, const AssetHash& asset, bool isLiquidity, const Address& toAddr, NonzeroFunds_uint64 amount) const
{
    return { tx_create_context(nonceId, compactFee), asset, isLiquidity, toAddr, amount };
}
LimitSwapCreate DataRetrievalContext::create_limit_swap(NonceId nonceId, CompactUInt compactFee, const AssetHash& asset, bool buy, NonzeroFunds_uint64 amount, Price_uint64 price) const
{
    return { tx_create_context(nonceId, compactFee), asset, buy, amount, price };
}
LiquidityDepositCreate DataRetrievalContext::create_liquidity_deposit(NonceId nonceId, CompactUInt compactFee, const AssetHash& asset, Funds_uint64 amount, Wart wart) const
{
    return { tx_create_context(nonceId, compactFee), asset, amount, wart };
}
LiquidityWithdrawalCreate DataRetrievalContext::create_liquidity_withdrawal(NonceId nonceId, CompactUInt compactFee, const AssetHash& asset, Funds_uint64 amount) const
{
    return { tx_create_context(nonceId, compactFee), asset, amount };
}
CancelationCreate DataRetrievalContext::create_cancelation(NonceId nonceId, CompactUInt compactFee, PinHeight cancelHeight, NonceId cancelNonceId) const
{
    return { tx_create_context(nonceId, compactFee), cancelHeight, cancelNonceId };
}
AssetCreationCreate DataRetrievalContext::create_asset_creation(NonceId nonceId, CompactUInt compactFee, FundsDecimal supply, AssetName name) const
{
    return { tx_create_context(nonceId, compactFee), supply, name };
}
