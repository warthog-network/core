#pragma once
#include "block/body/messages.hpp"
#include "block/body/transaction_map.hpp"
#include "chainserver/state/helpers/cache_fwd.hpp"
#include "comparators.hpp"
#include "defi/token/account_token.hpp"
#include "mempool/updates.hpp"
namespace chainserver {
struct TransactionIds;
}
namespace mempool {

struct LockedBalance {
    LockedBalance(Funds_uint64 total)
        : avail(std::move(total)) { };

    void lock(Funds_uint64 amount);
    void unlock(Funds_uint64 amount);
    [[nodiscard]] bool try_set_avail(Funds_uint64 amount);
    auto free() const { return diff_assert(avail, used); }
    auto locked() const { return used; }
    auto total() const { return sum_assert(avail, used); }
    bool is_clean() { return used.is_zero(); }

private:
    Funds_uint64 avail { Funds_uint64::zero() };
    Funds_uint64 used { Funds_uint64::zero() };
};

class MempoolTransactions {
public:
    using iter_t = Txset::iterator;
    using const_iter_t = Txset::const_iter_t;

private:
    template <typename... Comparators>
    struct MultiIndex {
        static_assert(sizeof...(Comparators) > 0);
        using tuple_t = std::tuple<std::set<const_iter_t, Comparators>...>;
        template <size_t I>
        requires(I < sizeof...(Comparators))
        auto& get()
        {
            return std::get<I>(tuple);
        }
        template <size_t I>
        requires(I < sizeof...(Comparators))
        auto& get() const
        {
            return std::get<I>(tuple);
        }
        bool insert(const_iter_t iter)
        {
            struct check_t {
                size_t size;
                bool inserted;
                bool operator==(const check_t&) const = default;
            };
            wrt::optional<check_t> prev;
            std::apply([&](auto&... args) {
                ([&](auto& arg) {
                    auto inserted { arg.insert(iter).second };
                    check_t next { arg.size(), inserted };
                    if (prev)
                        assert(*prev == next);
                    else
                        prev = next;
                }(args),
                    ...);
            },
                tuple);
            return prev.value().inserted;
        }
        size_t erase(const_iter_t iter)
        {
            wrt::optional<size_t> prevErased;
            std::apply([&](auto&... args) {
                ([&](auto& arg) {
                    auto erased { arg.erase(iter) };
                    if (prevErased)
                        assert(*prevErased == erased);
                    else
                        prevErased = erased;
                }(args),
                    ...);
            },
                tuple);
            return prevErased.value();
        }
        auto size() const { return get<0>().size(); }

    private:
        tuple_t tuple;
    };

    void apply_update(const Put&);
    void apply_update(const Erase&);
    bool erase(TransactionId txid);

private:
    size_t maxSize;
    Txset txs;
    ByFeeDesc byFee;
    TokenData byToken;

    struct : public MultiIndex<ComparatorPin, ComparatorTokenAccountFee, ComparatorAccountFee, ComparatorHash, ComparatorTxHeight> {
        [[nodiscard]] const auto& pin() const { return get<0>(); }
        [[nodiscard]] const auto& account_token_fee() const { return get<1>(); }
        [[nodiscard]] const auto& account_fee() const { return get<2>(); }
        [[nodiscard]] const auto& hash() const { return get<3>(); }
        [[nodiscard]] const auto& txheight() const { return get<4>(); }
    } _index;

public:
    auto& index() const { return _index; }
    auto& by_fee() const { return byFee; }
    [[nodiscard]] auto cache_validity() const { return txs.cache_validity(); }
    std::pair<iter_t, bool> insert(Entry e);
    auto find(TransactionId id) const { return txs().find(id); }
    auto begin() const { return txs().begin(); }
    auto end() const { return txs().end(); }
    MempoolTransactions(size_t maxSize = 10000)
        : maxSize(maxSize)
    {
        assert(maxSize > 0);
    }
    [[nodiscard]] auto by_fee_inc_le(AccountId aid, wrt::optional<CompactUInt> threshold = {}) { return txs.by_fee_inc_le(aid, threshold); }
    auto max_size() const { return maxSize; }
    auto size() const { return txs.size(); }
    void replay_updates(const Updates& log);

    // operator[]
    [[nodiscard]] auto operator[](const TransactionId& id) const
        -> wrt::optional<TransactionMessage>;
    [[nodiscard]] auto operator[](const HashView txHash) const
        -> wrt::optional<TransactionMessage>;
    [[nodiscard]] CompactUInt min_fee() const;
    [[nodiscard]] auto sample(size_t, bool onlyWartTransfer) const -> std::vector<TxidWithFee>;
    [[nodiscard]] auto filter_new(const std::vector<TxidWithFee>&) const
        -> std::vector<TransactionId>;
    void erase(iter_t);
    [[nodiscard]] auto get_transactions(size_t n, NonzeroHeight height, std::vector<TxHash>* hashes = nullptr) const -> std::vector<TransactionMessage>;
};

class Mempool {
    using Transactions = MempoolTransactions;
    using iter_t = Transactions::iter_t;
    using const_iter_t = Transactions::const_iter_t;

public:
    Mempool(const std::set<BlockVersion>& nextBlockversions,
        size_t maxSize = 10000)
        : transactions(maxSize)
    {
        set_allowed_blockversions(nextBlockversions);
    }

    [[nodiscard]] Updates pop_updates()
    {
        return std::move(updates);
        updates.clear();
    }
    Error insert_tx(const TransactionMessage& pm, TxHeight txh, const TxHash& hash, chainserver::DBCache& wartCache);
    void insert_tx_throw(const TransactionMessage& pm, TxHeight txh, const TxHash& hash, chainserver::DBCache& wartCache);

    size_t on_constraint_update();
    void erase(TransactionId id);
    void set_free_balance(AccountToken, Funds_uint64 newBalance);
    void set_allowed_blockversions(const std::set<BlockVersion>& s);
    void erase_from_height(NonzeroHeight);
    void erase_pinned_before_height(Height);
    [[nodiscard]] auto get_transactions(size_t n, NonzeroHeight height, std::vector<TxHash>* hashes = nullptr) const { return transactions.get_transactions(n, height, hashes); }
    [[nodiscard]] CompactUInt min_fee() const { return transactions.min_fee(); }

    // getters
    [[nodiscard]] auto cache_validity() const { return transactions.cache_validity(); }

    [[nodiscard]] auto operator[](const TransactionId& id) const { return transactions[id]; }
    [[nodiscard]] auto operator[](const HashView txHash) const { return transactions[txHash]; }
    [[nodiscard]] size_t size() const { return transactions.size(); }

private:
    using BalanceEntries = std::map<AccountToken, LockedBalance>;
    using balance_iterator = BalanceEntries::iterator;

    auto erase_if(auto&& lambda)
    {
        size_t i { 0 };
        for (auto iter = transactions.begin(); iter != transactions.end();) {
            auto iter_tmp = iter++;
            if (std::forward<decltype(lambda)>(lambda)(*iter_tmp)) {
                i += 1;
                erase_internal(iter_tmp);
            }
        }
        return i;
    }
    [[nodiscard]] std::pair<LockedBalance, wrt::optional<balance_iterator>> get_balance(AccountToken at, chainserver::DBCache&);
    [[nodiscard]] wrt::optional<TokenFunds> token_spend_throw(const TransactionMessage& pm, chainserver::DBCache& cache) const;
    void erase_internal(Txset::const_iter_t);
    struct EraseResult {
        bool erasedWart;
        bool erasedToken;
    };
    EraseResult erase_internal(Txset::const_iter_t, balance_iterator wartIter, wrt::optional<balance_iterator> tokenIter = {});
    [[nodiscard]] balance_iterator create_or_get_balance_iter(AccountToken at, chainserver::DBCache& cache);
    void prune();

private:
    TransactionMapBool allowedTransactions;
    Updates updates;
    MempoolTransactions transactions;
    BalanceEntries lockedBalances;
};
}
