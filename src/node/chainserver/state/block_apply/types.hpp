#pragma once

#include "block/body/transaction_id.hpp"
#include "block/chain/history/index.hpp"
#include "crypto/address.hpp"
#include "crypto/hash.hpp"
#include "defi/types.hpp"
#include "signature_verification.hpp"

namespace selectors {
template <bool replace, size_t I>
struct SelectorElement { };
template <typename... SelectorElements>
struct Selectors {
    template <typename T>
    using append = Selectors<SelectorElements..., T>;
};
template <typename Selectors, typename RemainingPath, size_t I, size_t J>
struct SelectorComputer;
template <bool... bs>
struct SelectorPath {
    using ComputedSelectors = SelectorComputer<Selectors<>, SelectorPath<bs...>, 0, 0>::selectors;
};

// compute path using recursion
template <typename Selectors, typename RemainingPath, size_t I, size_t J>
struct SelectorComputer {
    template <typename Path>
    struct Recurse;

    template <bool... remainder>
    struct Recurse<SelectorPath<true, remainder...>> {
        using next = SelectorComputer<typename Selectors::template append<SelectorElement<true, J>>, SelectorPath<remainder...>, I + 1, J + 1>;
    };
    template <bool... remainder>
    struct Recurse<SelectorPath<false, remainder...>> {
        using next = SelectorComputer<typename Selectors::template append<SelectorElement<false, I>>, SelectorPath<remainder...>, I + 1, J>;
    };
    using selectors = Recurse<RemainingPath>::next::selectors;
};
// stopping condition for recursion
template <typename Selectors, size_t I, size_t J>
struct SelectorComputer<Selectors, SelectorPath<>, I, J> {
    using selectors = Selectors;
};

// Now define the templated selector generator
template <bool... bs>
using GetSel = SelectorPath<bs...>::ComputedSelectors;

// Checking for sanity
using Test1 = GetSel<false, true, false, true>;
using Test2 = Selectors<SelectorElement<false, 0>, SelectorElement<true, 0>, SelectorElement<false, 2>, SelectorElement<true, 1>>;
static_assert(std::is_same_v<Test1, Test2>);
}
struct SwapInternal {
    HistoryId oId;
    TransactionId txid;
    Funds_uint64 base;
    Wart quote;
};

struct RewardInternal {
    ValidAccountId toAccountId;
    Wart wart;
    NonzeroHeight height;
    AddressView toAddress;
    Hash hash() const;
};

namespace block_apply {

struct ValidAccount {
    AddressView address;
    ValidAccountId id;
    ValidAccount(AddressView address, ValidAccountId accountId)
        : address(address)
        , id(accountId)
    {
    }
};

namespace impl {

template <typename internal_t>
struct Verified : public VerifiedTransaction {
private:
    friend internal_t;

    template <typename... Ts>
    Verified(const internal_t& internal, const TransactionVerifier& verifier, const Ts&... ts)
        : VerifiedTransaction(verifier.verify(internal, ts...))
        , ref(internal) {};

public:
    const internal_t& ref;
};

template <typename... T>
struct ArgTypesPack {
    template <typename R>
    using append = ArgTypesPack<T..., R>;
};

template <typename T>
struct ReplaceArg {
    static constexpr const bool replace = false;
    using type = void;
};

template <>
struct ReplaceArg<AssetId> {
    static constexpr const bool replace = true;
    using type = AssetHash;
};

template <typename ArgTypesPack, typename... Elements>
struct ReplacedArgsComputer;

template <bool replace, typename ArgTypesPack, typename Element, typename... Elements>
struct RecurseReplacedArgs;

template <typename ArgTypesPack, typename Element, typename... Elements>
struct RecurseReplacedArgs<false, ArgTypesPack, Element, Elements...> {
    using next = ReplacedArgsComputer<ArgTypesPack, Elements...>;
};
template <typename ArgTypesPack, typename Element, typename... Elements>
struct RecurseReplacedArgs<true, ArgTypesPack, Element, Elements...> {
    using next = ReplacedArgsComputer<typename ArgTypesPack::template append<typename ReplaceArg<typename Element::data_t>::type>, Elements...>;
};

template <typename ArgTypesPack, typename Element, typename... Elements>
struct ReplacedArgsComputer<ArgTypesPack, Element, Elements...> {
    using pack_t = RecurseReplacedArgs<ReplaceArg<typename Element::data_t>::replace, ArgTypesPack, Element, Elements...>::next::pack_t;
};
template <typename ArgTypesPack>
struct ReplacedArgsComputer<ArgTypesPack> {
    using pack_t = ArgTypesPack;
};

template <typename... Elements>
using GetArgs = ReplacedArgsComputer<ArgTypesPack<>, Elements...>::pack_t;

template <typename Combined, typename Selectors, typename VerifyArgPack>
struct Internal;

template <typename Combined, typename Selectors, typename... VerifyArgs>
struct Internal<Combined, Selectors, ArgTypesPack<VerifyArgs...>> : public SignerData, public Combined {
    Internal(SignerData signerData, Combined data)
        : SignerData(std::move(signerData))
        , Combined(std::move(data))
    {
    }
    [[nodiscard]] Verified<Internal> verify(const TransactionVerifier& verifier, const VerifyArgs&... args) const
    {
        return verify_tuple(verifier, std::forward_as_tuple(args...), Selectors());
    }

private:
    template <size_t i>
    auto& select_arg(selectors::SelectorElement<true, i>, const std::tuple<const VerifyArgs&...>& args) const
    {
        // get argument from args
        return std::get<i>(args);
    }
    template <size_t i>
    auto& select_arg(selectors::SelectorElement<false, i>, const std::tuple<const VerifyArgs&...>&) const
    {
        // get argument from own base class
        return static_cast<const Combined*>(this)->template get_at<i>();
    }
    auto& map_arg(auto& arg) const
    {
        return arg;
    }
    AddressView map_arg(const ValidAccount& va) const
    {
        return va.address;
    }
    template <typename... Sels>
    Verified<Internal> verify_tuple(const TransactionVerifier& verifier, const std::tuple<const VerifyArgs&...>& t, selectors::Selectors<Sels...>) const
    {
        return Verified<Internal> { *this, verifier, this->map_arg(this->select_arg(Sels(), t))... };
    }
};
}

template <typename... Elements>
struct signed_entry {
    // "Internal" is for temporary representation of block entries while block is applied
    using Internal = impl::Internal<CombineElements<Elements...>, typename selectors::SelectorPath<impl::ReplaceArg<typename Elements::data_t>::replace...>::ComputedSelectors, impl::GetArgs<Elements...>>;

    // "Verified" is a safety type for block entries while block is applied. This type is guaranteed to be signature-verified
    using Verified = impl::Verified<Internal>;
};

struct CancelTxIdElement : public ElementBase<TransactionId> {
    using ElementBase::ElementBase;
    [[nodiscard]] const auto& cancel_txid() const { return data; }
};

struct NonzeroBaseQuote : public defi::BaseQuote {
    NonzeroBaseQuote(Reader& r)
        : defi::BaseQuote(r)
    {
        validate_throw();
    }
    NonzeroBaseQuote(Funds_uint64 base, Wart quote)
        : defi::BaseQuote(base, quote)
    {
        validate_throw();
    }

private:
    void validate_throw()
    {
        if (base().is_zero() && quote().is_zero()) {
            throw Error(EZEROBASEQUOTE);
        }
    }
};

struct NonzeroBaseQuoteEl : public ElementBase<NonzeroBaseQuote> {
    using ElementBase::ElementBase;
    [[nodiscard]] const auto& base_quote() const { return data; }
    [[nodiscard]] const auto& base() const { return data.base(); }
    [[nodiscard]] const auto& quote() const { return data.quote(); }
};

struct ToValidAccEl : public ElementBase<ValidAccount> {
    using ElementBase::ElementBase;
    [[nodiscard]] const auto& to_address() const { return data.address; }
    [[nodiscard]] const auto& to_id() const { return data.id; }
};

using Cancelation = signed_entry<CancelHeightEl, CancelNonceEl>;
using WartTransfer = signed_entry<ToValidAccEl, WartEl>;
using Order = signed_entry<AssetIdEl, BuyEl, NonzeroAmountEl, LimitPriceEl>;
using LiquidityDeposit = signed_entry<AssetIdEl, NonzeroBaseQuoteEl>;
using LiquidityWithdrawal = signed_entry<AssetIdEl, NonzeroAmountEl>;
using TokenTransfer = signed_entry<AssetIdEl, LiquidityFlagEl, ToValidAccEl, NonzeroAmountEl>;
using AssetCreation = signed_entry<AssetSupplyEl, AssetNameEl>;

}
