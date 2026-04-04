#pragma once
#include "block/body/transaction_id.hpp"
#include "chainserver/state/block_apply/types.hpp"
#include "crypto/hash.hpp"
#include "defi/token/asset.hpp"
#include "defi/uint64/pool.hpp"
#include "defi/uint64/types.hpp"
#include "general/serializer_fwd.hxx"
#include <strings.h>
#include <unistd.h>
class Headerchain;

class TxIdVerifier;

namespace history {

using SignData = CombineElements<PinNonceEl, CompactFeeEl, OriginAccIdEl>;
struct SignDataEl : public ElementBase<SignData> {
    using base_t::base_t;
    [[nodiscard]] const auto& sign_data() const { return data; }
};

template <uint8_t I, typename T>
requires(I < 256)
struct WithIndicator : public T {
    constexpr static uint8_t INDICATOR = I;
    using T::T;
};

template <uint8_t I, typename... Ts>
using IdCombine = WithIndicator<I, CombineElements<Ts...>>;

template <uint8_t I, typename... Ts>
using IdCombineSigned = IdCombine<I, SignDataEl, Ts...>;

using WartTransferData = IdCombineSigned<1, ToAccIdEl, WartEl>;
using RewardData = IdCombine<2, ToAccIdEl, WartEl>;
using AssetCreationData = IdCombineSigned<3, AssetIdEl, AssetSupplyEl, AssetNameEl>;
using TokenTransferData = IdCombineSigned<4, NonWartTokenIdEl, ToAccIdEl, AmountEl>;
using OrderData = IdCombineSigned<5, AssetIdEl, BuyEl, LimitPriceEl, AmountEl>;
using CancelationData = IdCombineSigned<6, CancelTxidEl, ReferredHistoryIdEl>;

struct PoolBeforeEl : public ElementBase<defi::BaseQuote> {
    using base_t::base_t;
    [[nodiscard]] const auto& pool_before() const { return data; }
};

struct PoolAfterEl : public ElementBase<defi::BaseQuote> {
    using base_t::base_t;
    [[nodiscard]] const auto& pool_after() const { return data; }
    [[nodiscard]] auto& pool_after() { return data; }
};

template <typename T>
struct vect_len32_base : public std::vector<T> {
public:
    using std::vector<T>::vector;
    vect_len32_base(Reader& r)
    {
        size_t N { r.uint32() };
        this->reserve(N);
        for (size_t i { 0 }; i < N; ++i) {
            this->push_back(T { r });
        }
    }
    void serialize(RawSerializer auto& s) const
    {
        s << uint32_t(this->size());
        for (auto& e : *this)
            s << e;
    }
};

template <typename T>
struct vect_len32 : public vect_len32_base<T> {
    using vect_len32_base<T>::vect_len32_base;
    [[nodiscard]] size_t byte_size() const
    {
        size_t N { 4 };
        for (auto& e : *this) {
            N += e.byte_size();
        }
        return N;
    }
};
template <HasStaticBytesize T>
struct vect_len32<T> : public vect_len32_base<T> {
    using vect_len32_base<T>::vect_len32_base;
    [[nodiscard]] size_t byte_size() const
    {
        return 4 + this->size() * T::byte_size();
    }
};

struct BuySwapsEl : public ElementBase<vect_len32<CombineElements<BaseEl, QuoteEl, ReferredHistoryIdEl>>> {
    using ElementBase::ElementBase;
    [[nodiscard]] const auto& buy_swaps() const { return data; }
    [[nodiscard]] auto& buy_swaps() { return data; }
};

struct SellSwapsEl : public ElementBase<vect_len32<CombineElements<BaseEl, QuoteEl, ReferredHistoryIdEl>>> {
    using base_t::base_t;
    [[nodiscard]] const auto& sell_swaps() const { return data; }
    [[nodiscard]] auto& sell_swaps() { return data; }
};

using MatchDataBase = IdCombine<8, AssetIdEl, PoolBeforeEl, PoolAfterEl, BuySwapsEl, SellSwapsEl>;
struct MatchData : public MatchDataBase {
    MatchData(AssetId assetId, defi::PoolLiquidity_uint64 poolBefore)
        : MatchData { assetId, PoolBeforeEl(defi::BaseQuote_uint64(poolBefore)), PoolAfterEl(defi::BaseQuote_uint64(std::move(poolBefore))), BuySwapsEl { {} }, SellSwapsEl { {} } }
    {
    }
    using MatchDataBase::MatchDataBase;
};

using LiquidityDeposit = IdCombineSigned<9, AssetIdEl, BaseEl, QuoteEl, NonzeroSharesEl>;
using LiquidityWithdraw = IdCombineSigned<10, AssetIdEl, BaseEl, QuoteEl, NonzeroSharesEl>;

struct CantParseHistoryExceptionGenerator {
    static std::exception generate(uint8_t)
    {
        return std::runtime_error("Cannot parse history entry");
    }
};

using HistoryVariant = wrt::indicator_variant<
    CantParseHistoryExceptionGenerator,
    WartTransferData,
    RewardData,
    AssetCreationData,
    TokenTransferData,
    OrderData,
    CancelationData,
    MatchData,
    LiquidityDeposit,
    LiquidityWithdraw>;

struct Entry {
    Entry(const RewardInternal& p);
    Entry(const block_apply::WartTransfer::Verified& p);
    Entry(const block_apply::TokenTransfer::Verified& p, NonWartTokenId);
    Entry(const block_apply::Order::Verified& p);
    Entry(const block_apply::Cancelation::Verified& p, HistoryId canceled);
    Entry(const block_apply::AssetCreation::Verified& p, AssetId);
    Entry(const block_apply::LiquidityDeposit::Verified& p, NonzeroFunds_uint64 receivedShares);
    Entry(const block_apply::LiquidityWithdrawal::Verified& p, Funds_uint64 receivedBase, Wart receivedQuote);
    Entry(TxHash h, MatchData);
    Entry(TxHash h, HistoryVariant data)
        : hash(std::move(h))
        , data(std::move(data))
    {
    }
    TxHash hash;
    HistoryVariant data;
};

}
