#pragma once
#include "block/body/merkle_write_hooker.hpp"
#include "block/body/transaction_id.hpp"
#include "block/body/transaction_types.hpp"
#include "elements_fwd.hpp"
#include "general/base_elements.hpp"
#include "general/serializer_fwd.hxx"
#include "general/structured_reader_fwd.hpp"
namespace block {
namespace body {

template <typename... Ts>
struct Combined : public Ts... {
    template <typename... Args>
    Combined(StructuredReader& r, Args&&... args)
        : Ts(r, std::forward<Args>(args)...)...
    {
    }
    Combined(Ts... ts)
        : Ts(std::move(ts))...
    {
    }
    void serialize(RawSerializer auto&& s) const
    {
        (s << ... << static_cast<const Ts*>(this)->get());
    }
    void append_merkle_leaves(std::vector<Hash>& out) const
    {
        out.push_back(hashSHA256(*this));
    }
};
template <typename T>
struct HookMerkle : public T {
    using T::T;
    void serialize(MerkleSerializer auto&& h) const
    {
        [[maybe_unused]] auto hook { h.hook() };
        h.writer << *static_cast<const T*>(this);
    }
};

template <typename... Ts>
struct SignedCombined : public Combined<OriginAccIdEl, PinNonceEl, CompactFeeEl, Ts..., SignatureEl> {
    using Combined<OriginAccIdEl, PinNonceEl, CompactFeeEl, Ts..., SignatureEl>::Combined;
    [[nodiscard]] TransactionId txid(PinHeight pinHeight) const
    {
        PinNonce pn = this->pin_nonce();
        return { this->origin_account_id(), pinHeight, pn.id };
    }
    [[nodiscard]] TransactionId txid_from_floored(PinFloor pinFloor) const
    {
        PinNonce pn = this->pin_nonce();
        auto pinHeight { pn.pin_height_from_floored(pinFloor) };
        return { this->origin_account_id(), pinHeight, pn.id };
    }
    void append_txids(std::vector<TransactionId>& txids, PinFloor pf, PinHeight minPinheight) const
    {
        TransactionId txid { txid_from_floored(pf) };
        if (txid.pinHeight < minPinheight)
            return;
        txids.push_back(txid);
    }
};

template <typename TransactionType, typename... Elements>
struct Transaction : public TransactionType, public HookMerkle<TaggedSignedCombined<TransactionType::label, Elements...>> {
    using parent_t = HookMerkle<TaggedSignedCombined<TransactionType::label, Elements...>>;
    using parent_t::parent_t;
};

//////////////////////////////
// define transaction types

#define DEFINE_TRANSACTION(name, type, ...)               \
    struct name : public Transaction<type, __VA_ARGS__> { \
        using Transaction::Transaction;                   \
    };
DEFINE_TRANSACTION(WartTransfer, IsWartTransfer, ToAccIdEl, WartEl)
DEFINE_TRANSACTION(AssetTransfer, IsTokenTransfer, ToAccIdEl, NonzeroAmountEl)
DEFINE_TRANSACTION(LiquidityTransfer, IsTokenTransfer, ToAccIdEl, NonzeroSharesEl)
DEFINE_TRANSACTION(AssetCreation, IsAssetCreate, AssetSupplyEl, AssetNameEl)
DEFINE_TRANSACTION(Order, IsLimitSwap, BuyEl, NonzeroAmountEl, LimitPriceEl)
DEFINE_TRANSACTION(LiquidityDeposit, IsLiquidityDeposit, BaseEl, QuoteEl)
DEFINE_TRANSACTION(LiquidityWithdrawal, IsLiquidityWithdrawal, NonzeroAmountEl)
#undef DEFINE_TRANSACTION
struct Cancelation : public Transaction<IsCancelation, CancelHeightEl, CancelNonceEl> {
    using Transaction::Transaction;
    TransactionId canceled_txid() const
    {
        return { origin_account_id(), cancel_height(), cancel_nonceid() };
    }
};

}
}
