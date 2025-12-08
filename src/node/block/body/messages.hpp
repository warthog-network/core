#pragma once
#include "block/body/elements.hpp"
#include "block/body/nonce.hpp"
#include "block/body/transaction_id.hpp"
#include "block/body/transaction_types.hpp"
#include "crypto/hasher_sha256.hpp"
#include "general/reader.hpp"
#include "spdlog/spdlog.h"
#include "tools/indicator_variant.hpp"
#include "wrt/variant.hpp"

struct MsgBase : public CombineElements<TransactionIdEl, NonceReservedEl, CompactFeeEl> {
    using CombineElements::CombineElements;
    auto pin_nonce_throw(NonzeroHeight height)
    {
        auto pn { PinNonce::make_pin_nonce(nonce_id(), height, pin_height()) };
        if (!pn)
            throw std::runtime_error("Cannot make pin_nonce");
        return *pn;
    }
};

namespace messages {
struct SpendToken {
    AssetHash hash;
    bool isLiquidity;
    NonzeroFunds_uint64 amount;
};
}

template <uint8_t indicator, typename Self, typename... Ts>
class ComposeTransactionMessage : public MsgBase, public CombineElements<Ts..., SignatureEl> {

protected:
    using parent_t = ComposeTransactionMessage;

    void check_throw() const
    {
    }

public:
    static constexpr bool has_asset_hash = std::derived_from<parent_t, AssetHashEl>;
    static constexpr uint8_t INDICATOR { indicator }; // used for serialization of transaction message vectors
    //
    ComposeTransactionMessage(const TransactionId& txid, NonceReserved reserved, CompactUInt compactFee, Ts::data_t... ts, RecoverableSignature signature)
        : MsgBase { txid, std::move(reserved), std::move(compactFee) }
        , CombineElements<Ts..., SignatureEl>(std::move(ts)..., std::move(signature))
    {
        static_cast<Self*>(this)->check_throw();
    }
    ComposeTransactionMessage(Reader& r)
        : MsgBase { r }
        , CombineElements<Ts..., SignatureEl>(r)
    {
        static_cast<Self*>(this)->check_throw();
    }
    static constexpr size_t byte_size() { return 16 + 3 + 2 + (Ts::byte_size() + ...) + 65; }

    void serialize(RawSerializer auto& s) const
    {
        ((s << txid()
            << nonce_reserved()
            << compact_fee())
            << ... << static_cast<const Ts*>(this)->get())
            << SignatureEl::signature();
    }

    [[nodiscard]] TxHash txhash(const PinHash& pinHash) const
    {

        return TxHash(((HasherSHA256()
                           << pinHash
                           << this->txid().pinHeight
                           << this->txid().nonceId
                           << this->nonce_reserved()
                           << this->compact_fee().uncompact())
            << ... << CombineElements<Ts..., SignatureEl>::template get<Ts>()));
    }
    [[nodiscard]] Wart spend_wart_throw() const { return this->fee(); } // default only spend fee, but is overridden in WartTransferMessage and LiquidityDepositMessage
    // [[nodiscard]] wrt::optional<SpendToken> spend_token_throw() const { return {}; } // default no other token than WART
    [[nodiscard]] Address from_address(const TxHash& txHash) const
    {
        return this->signature().recover_pubkey(txHash.data()).address();
    }
};

class WartTransferMessage : public IsWartTransfer, public ComposeTransactionMessage<1, WartTransferMessage, ToAddrEl, NonzeroWartEl> {
public:
    static_assert(!has_asset_hash);
    using WartTransfer = block::body::WartTransfer;
    using ComposeTransactionMessage::ComposeTransactionMessage;
    void check_throw()
    {
        // nothing to check since the amount() is already zero by type restriction
    }

    [[nodiscard]] Wart spend_wart_throw() const { return sum_throw(fee(), wart()); }
};

class TokenTransferMessage : public IsTokenTransfer, public ComposeTransactionMessage<2, TokenTransferMessage, AssetHashEl, LiquidityFlagEl, ToAddrEl, NonzeroAmountEl> { // for defi we include the asset hash
public:
    static_assert(has_asset_hash);
    using ComposeTransactionMessage::ComposeTransactionMessage;
    void check_throw()
    {
        // nothing to check since the amount() is already zero by type restriction
    }
    [[nodiscard]] messages::SpendToken spend_token_throw() const { return messages::SpendToken { asset_hash(), is_liquidity(), amount() }; }
};

class AssetCreationMessage : public IsAssetCreate, public ComposeTransactionMessage<3, AssetCreationMessage, AssetSupplyEl, AssetNameEl> {
public:
    using ComposeTransactionMessage::ComposeTransactionMessage;
};

class LimitSwapMessage : public IsLimitSwap, public ComposeTransactionMessage<4, LimitSwapMessage, AssetHashEl, BuyEl, NonzeroAmountEl, LimitPriceEl> { // for defi we include the token hash
    static_assert(has_asset_hash);

public:
    [[nodiscard]] wrt::optional<messages::SpendToken> spend_token_throw() const
    {
        if (buy())
            return {};
        return messages::SpendToken { asset_hash(), false, amount() };
    }
    [[nodiscard]] Wart spend_wart_throw() const { return sum_throw(fee(), buy() ? Wart::from_funds_throw(amount()) : Wart::zero()); }
    using parent_t::parent_t;
};

class LiquidityDepositMessage : public IsLiquidityDeposit, public ComposeTransactionMessage<5, LiquidityDepositMessage, AssetHashEl, BaseEl, QuoteEl> {
    static_assert(has_asset_hash);

public:
    [[nodiscard]] Wart spend_wart_throw() const { return sum_throw(fee(), quote()); }
    [[nodiscard]] tl::optional<messages::SpendToken> spend_token_throw() const
    {
        if (base().is_zero())
            return {};
        return messages::SpendToken { asset_hash(), false, base() };
    }
    using parent_t::parent_t;
};

class LiquidityWithdrawalMessage : public IsLiquidityWithdrawal, public ComposeTransactionMessage<6, LiquidityWithdrawalMessage, AssetHashEl, NonzeroAmountEl> {
    static_assert(has_asset_hash);

public:
    [[nodiscard]] messages::SpendToken spend_token_throw() const
    {
        return { asset_hash(), true, amount() };
    }
    using parent_t::parent_t;
};

class CancelationMessage : public IsCancelation, public ComposeTransactionMessage<7, CancelationMessage, CancelHeightEl, CancelNonceEl> {
    static_assert(!has_asset_hash);

private:
    friend ComposeTransactionMessage;
    void check_throw()
    {
        if (cancel_height() > txid().pinHeight)
            throw Error(ECANCELFUTURE);
        if (cancel_height() == txid().pinHeight && cancel_nonceid() == txid().nonceId)
            throw Error(ECANCELSELF);
    }

public:
    TransactionId cancel_txid() const
    {
        return { from_id(), cancel_height(), cancel_nonceid() };
    }
    using ComposeTransactionMessage::ComposeTransactionMessage;
};

struct InvTxTypeExceptionGenerator {
    static auto generate(uint8_t txtype)
    {
        spdlog::warn("Invalid txtype {}", txtype);
        return Error(ETXTYPE);
    }
};

using TransactionVariant = wrt::indicator_variant<InvTxTypeExceptionGenerator, WartTransferMessage, TokenTransferMessage, AssetCreationMessage, LimitSwapMessage, LiquidityDepositMessage, LiquidityWithdrawalMessage, CancelationMessage>;

struct TransactionMessage : public TransactionVariant {
public:
    using TransactionVariant::TransactionVariant;
    const MsgBase& base() const
    {
        return *visit([](const auto& m) -> const MsgBase* { return &m; });
    }
    void validate_throw()
    {
    }
    const RecoverableSignature& signature() const
    {
        return *visit([](const auto& m) -> const RecoverableSignature* { return &m.signature(); });
    }
    [[nodiscard]] auto compact_fee() const { return base().compact_fee(); }
    [[nodiscard]] auto fee() const { return compact_fee().uncompact(); }
    [[nodiscard]] auto spend_wart_throw() const
    {
        return visit([](auto& m) { return m.spend_wart_throw(); });
    }
    [[nodiscard]] auto spend_token_throw() const
    {
        return visit([]<typename T>(const T& m) -> wrt::optional<messages::SpendToken> {
            if constexpr (T::has_asset_hash) {
                return m.spend_token_throw();
            } else {
                return {};
            }
        });
    }
    [[nodiscard]] auto spend_wart_assert() const
    {
        try {
            return spend_wart_throw();
        } catch (Error e) {
            assert(false);
        }
    }
    [[nodiscard]] auto spend_token_assert() const
    {
        try {
            return spend_token_throw();
        } catch (Error e) {
            assert(false);
        }
    }
    [[nodiscard]] auto& txid() const { return base().txid(); }
    [[nodiscard]] auto nonce_reserved() const { return base().nonce_reserved(); }
    [[nodiscard]] AccountId from_id() const { return base().from_id(); }
    [[nodiscard]] PinHeight pin_height() const { return base().pin_height(); }
    [[nodiscard]] NonceId nonce_id() const { return base().nonce_id(); }
    [[nodiscard]] auto from_address(const TxHash txHash) const
    {
        return visit([&](auto& m) { return m.from_address(txHash); });
    }
    [[nodiscard]] TxHash txhash(const PinHash& pinHash) const
    {
        return visit([&](const auto& m) { return m.txhash(pinHash); });
    }
};
