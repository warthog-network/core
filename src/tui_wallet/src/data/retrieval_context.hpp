#pragma once
#include "api/endpoint.hpp"
#include "communication/create_transaction.hpp"
#include "wallet.hpp"
struct DataRetrievalContext {
    Endpoint endpoint;
    Wallet wallet;
    auto address() const { return wallet.address.to_string(); }
    TransactionCreateContext tx_create_context(NonceId nonceId, CompactUInt compactFee) const
    {
        auto [pinHeight, pinHash] { endpoint.get_pin() };
        return TransactionCreateContext {
            .pinHash { pinHash },
            .pinHeight { pinHeight },
            .nonceId { nonceId },
            .compactFee { compactFee },
            .pk { wallet.privKey }
        };
    }

public:
    WartTransferCreate create_wart_transfer(NonceId, CompactUInt, const Address& toAddr, NonzeroWart wart) const;
    TokenTransferCreate create_token_transfer(NonceId, CompactUInt, const AssetHash& asset, bool isLiquidity, const Address& toAddr, NonzeroFunds_uint64 amount) const;
    LimitSwapCreate create_limit_swap(NonceId, CompactUInt, const AssetHash& asset, bool buy, NonzeroFunds_uint64 amount, Price_uint64 price) const;
    LiquidityDepositCreate create_liquidity_deposit(NonceId, CompactUInt, const AssetHash& asset, Funds_uint64 amount, Wart wart) const;
    LiquidityWithdrawalCreate create_liquidity_withdrawal(NonceId, CompactUInt, const AssetHash& asset, Funds_uint64 amount) const;
    CancelationCreate create_cancelation(NonceId, CompactUInt, PinHeight cancelHeight, NonceId cancelNonceId) const;
    AssetCreationCreate create_asset_creation(NonceId, CompactUInt, FundsDecimal supply, AssetName name) const;

    // auto wart_transfer(NonceId nonceId, CompactUInt compactFee, const Address& toAddr, NonzeroWart wart) const
    // {
    //     return endpoint.send_transaction(create_wart_transfer(nonceId, compactFee, toAddr, wart));
    // }
    // auto token_transfer(NonceId nonceId, CompactUInt compactFee, const AssetHash& asset, bool isLiquidity, const Address& toAddr, NonzeroFunds_uint64 amount) const
    // {
    //     return endpoint.send_transaction(create_token_transfer(nonceId, compactFee, asset, isLiquidity, toAddr, amount));
    // }
    // auto limit_swap(NonceId nonceId, CompactUInt compactFee, const AssetHash& asset, bool buy, NonzeroFunds_uint64 amount, Price_uint64 price) const
    // {
    //     return endpoint.send_transaction(create_limit_swap(nonceId, compactFee, asset, buy, amount, price));
    // }
    // auto liquidity_deposit(NonceId nonceId, CompactUInt compactFee, const AssetHash& asset, Funds_uint64 amount, Wart wart) const
    // {
    //     return endpoint.send_transaction(create_liquidity_deposit(nonceId, compactFee, asset, amount, wart));
    // }
    // auto liquidity_withdrawal(NonceId nonceId, CompactUInt compactFee, const AssetHash& asset, Funds_uint64 amount) const
    // {
    //     return endpoint.send_transaction(create_liquidity_withdrawal(nonceId, compactFee, asset, amount));
    // }
    // auto cancelation(NonceId nonceId, CompactUInt compactFee, PinHeight cancelHeight, NonceId cancelNonceId) const
    // {
    //     return endpoint.send_transaction(create_cancelation(nonceId, compactFee, cancelHeight, cancelNonceId));
    // }
    // auto asset_creation(NonceId nonceId, CompactUInt compactFee, FundsDecimal supply, AssetName name) const
    // {
    //     return endpoint.send_transaction(create_asset_creation(nonceId, compactFee, supply, name));
    // }

    auto get_balance(api::TokenIdOrSpec token) const
    {
        return endpoint.get_balance(address(), token);
    }
    auto get_wart_balance() const
    {
        return endpoint.wart_balance(address());
    }
};
