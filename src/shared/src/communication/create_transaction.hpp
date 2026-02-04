#pragma once

#include "api/http/json_converter.hpp"
#include "block/body/labels.hpp"
#include "block/body/nonce.hpp"
#include "block/chain/height.hpp"
#include "create_transaction_fwd.hpp"
#include "crypto/crypto.hpp"
#include "crypto/hash.hpp"
#include "crypto/hasher_sha256.hpp"
#include "general/base_elements.hpp"
#include "general/compact_uint.hpp"

struct TransactionCreateContext {
    PinHash pinHash;
    PinHeight pinHeight;
    NonceId nonceId;
    CompactUInt compactFee;
    PrivKey pk;
};

template <typename Derived, typename... Ts>
class TransactionCreateBase : public CombineElements<PinHeightEl, NonceIdEl, NonceReservedEl, CompactFeeEl, Ts...>, public SignatureEl {

public:
    // using CombineElements<PinHeightEl, NonceIdEl, NonceReservedEl, CompactFeeEl, Ts..., SignatureEl>::CombineElements;
    TransactionCreateBase(PinHeightEl ph, NonceIdEl nonceId, CompactFeeEl fee, Ts&&... ts, SignatureEl s)
        : CombineElements<PinHeightEl, NonceIdEl, NonceReservedEl, CompactFeeEl, Ts...>(ph, nonceId, NonceReserved::zero(), fee, std::forward<Ts>(ts)...)
        , SignatureEl(std::move(s))
    {
    }
    TransactionCreateBase(const TransactionCreateContext& ctx, Ts... ts)
        : CombineElements<PinHeightEl, NonceIdEl, NonceReservedEl, CompactFeeEl, Ts...>(ctx.pinHeight, ctx.nonceId, NonceReserved::zero(), ctx.compactFee, std::move(ts)...)
        , SignatureEl(ctx.pk.sign(tx_hash(ctx.pinHash)))
    {
    }
    TransactionCreateBase(const JSONConverter& c)
        : TransactionCreateBase(c, c, c, Ts(c)..., c)
    {
    }

    [[nodiscard]] auto create_message(AccountId aid) const
    {
        TransactionId txid(aid, this->pin_height(), this->nonce_id());
        return typename Derived::message_t { txid, this->nonce_reserved(), this->compact_fee(), static_cast<const Ts*>(this)->get()..., this->signature() };
    }
    [[nodiscard]] TxHash tx_hash(const PinHash& pinHash) const
    {
        return TxHash(((HasherSHA256()
                           << pinHash
                           << this->pin_height()
                           << this->nonce_id()
                           << this->nonce_reserved()
                           << this->compact_fee().uncompact())
            << ... << static_cast<const Ts*>(this)->get()));
    }
    [[nodiscard]] Address from_address(const TxHash& txHash) const
    {
        return this->signature().recover_pubkey(txHash.data()).address();
    }
    [[nodiscard]] bool valid_signature(const PinHash& pinHash, AddressView fromAddress) const
    {
        return from_address(tx_hash(pinHash)) == fromAddress;
    }
};

#define DEFINE_CREATE_MESSAGE(name, str_tag, ...)                              \
    class name : public TransactionCreateBase<name, __VA_ARGS__> {             \
    public:                                                                    \
        static constexpr const char* tag() { return str_tag; };                \
        using TransactionCreateBase::TransactionCreateBase;                    \
        operator std::string();                                                \
        static name parse_from(const JSONConverter& json) { return { json }; } \
    };

DEFINE_CREATE_MESSAGE(WartTransferCreate, ::block::labels::wartTransfer, ToAddrEl, NonzeroWartEl)
DEFINE_CREATE_MESSAGE(TokenTransferCreate, ::block::labels::tokenTransfer, AssetHashEl, LiquidityFlagEl, ToAddrEl, AmountEl)
DEFINE_CREATE_MESSAGE(LimitSwapCreate, ::block::labels::limitSwap, AssetHashEl, BuyEl, AmountEl, LimitPriceEl)
DEFINE_CREATE_MESSAGE(LiquidityDepositCreate, ::block::labels::liquidityDeposit, AssetHashEl, AmountEl, WartEl)
DEFINE_CREATE_MESSAGE(LiquidityWithdrawalCreate, ::block::labels::liquidityWithdrawal, AssetHashEl, SharesEl)
DEFINE_CREATE_MESSAGE(CancelationCreate, ::block::labels::cancelation, CancelHeightEl, CancelNonceEl)
DEFINE_CREATE_MESSAGE(AssetCreationCreate, ::block::labels::assetCreation, AssetSupplyEl, AssetNameEl)

#undef DEFINE_CREATE_MESSAGE

template <typename... Ts>
struct TransactionCreateCombine : wrt::variant<Ts...> {

    template <typename T>
    static TransactionCreateCombine parse_from(std::string_view type, T&& from)
    {
        wrt::optional<TransactionCreateCombine> result;
        ([&]() {
            if (type == Ts::tag())
                result = Ts::parse_from(std::forward<T>(from));
            return result.has_value();
        }() || ...);
        if (result)
            return *result;
        throw Error(ETXTYPE);
    };
    using wrt::variant<Ts...>::variant;
    std::string tag() const
    {
        return this->visit([&](auto& createTransaction) -> std::string {
            return createTransaction.tag();
        });
    }
};
