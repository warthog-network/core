#pragma once
#include "block/body/messages.hpp"
#include "block/body/transaction_map.hpp"
#include "chainserver/state/helpers/cache_fwd.hpp"
#include "comparators.hpp"
#include "defi/token/account_token.hpp"
#include "mempool/updates.hpp"
#include <generator>
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
    auto find(TransactionId id) const { return txs().find(id); }
    auto& txset() const { return txs(); }
    auto begin() const { return txs().begin(); }
    auto end() const { return txs().end(); }

    [[nodiscard]] auto by_fee_inc_le(AccountId aid, wrt::optional<CompactUInt> threshold = {}) const { return txs.by_fee_inc_le(aid, threshold); }
    auto max_size() const { return maxSize; }
    auto size() const { return txs.size(); }

    // operator[]
    [[nodiscard]] auto operator[](const TransactionId& id) const
        -> wrt::optional<TransactionMessage>;
    [[nodiscard]] auto operator[](const HashView txHash) const
        -> wrt::optional<TransactionMessage>;
    [[nodiscard]] CompactUInt min_fee() const;
    [[nodiscard]] auto sample(size_t, bool onlyWartTransfer) const -> std::vector<TxidWithFee>;
    [[nodiscard]] auto filter_new(const std::vector<TxidWithFee>&) const
        -> std::vector<TransactionId>;
    [[nodiscard]] auto get_transactions(size_t n, NonzeroHeight height, std::vector<TxHash>* hashes = nullptr) const -> std::vector<TransactionMessage>;

    MempoolTransactions(size_t maxSize = 10000)
        : maxSize(maxSize)
    {
        assert(maxSize > 0);
    }

    // non-const methods
    void replay_updates(const Updates& log);
    std::pair<iter_t, bool> insert(Entry e);
    void erase(iter_t);
};

class Mempool {
    using Transactions = MempoolTransactions;
    using iter_t = Transactions::iter_t;
    using const_iter_t = Transactions::const_iter_t;

    class Node {
    public:
        using vector_t = std::vector<iter_t>;

    private:
        mutable vector_t buys;
        mutable vector_t sells;
        auto& vector(bool buy) const
        {
            return buy ? buys : sells;
        }
        vector_t& vector(bool buy)
        {
            return buy ? buys : sells;
        }
        static vector_t& sort_and_get(vector_t& v, bool buy)
        {
            static constexpr auto by_price_inc { [](iter_t lhs, iter_t rhs) static {
                return lhs->get<LimitSwapMessage>().limit() < rhs->get<LimitSwapMessage>().limit();
            } };
            static constexpr auto by_price_dec { [](iter_t lhs, iter_t rhs) static {
                return lhs->get<LimitSwapMessage>().limit() > rhs->get<LimitSwapMessage>().limit();
            } };
            std::ranges::sort(v, buy ? by_price_dec : by_price_inc);
            return v;
        }

    public:
        void sort();

        const vector_t& sorted_orders(bool buy) const
        {
            return sort_and_get(vector(buy), buy);
        }
        void push_back(bool buy, iter_t iter)
        {
            vector(buy).push_back(iter);
        }
        auto erase(bool buy, iter_t iter)
        {
            return std::erase(vector(buy), iter);
        }
    };
    std::map<AssetId, Node> orders; // PERFORMANCE: use hash based map but not unordered_map as it is too slow, it would not be worth changing it.
    void insert_swap(AssetId market, iter_t);
    size_t erase_swap(AssetId market, iter_t);

public:
    Mempool(const std::set<BlockVersion>& nextBlockversions,
        size_t maxSize = 10000)
        : transactions(maxSize)
    {
        set_allowed_blockversions(nextBlockversions);
    }

    [[nodiscard]] Updates pop_updates()
    {
        auto out(std::move(updates));
        updates.clear();
        return out;
    }
    // Error insert_tx(const TransactionMessage& pm, TxHeight txh, const TxHash& hash, chainserver::DBCache& dbCache);
    struct InsertParams {
        TransactionMessage&& msg;
        wrt::optional<AssetId> assetId; // if an assetHash parameter was involved, this holds the corresponding asset id
        TxHeight height; // height for pruning on rollback
        const TxHash& hash; // tx hash
        chainserver::DBCache& dbCache;
        struct Nonwart {
            TokenId tokenId { TokenId::WART }; // if WART, means no nonwart token involved
            Funds_uint64 spend { 0 };
        } nonwart;
    };
    void insert_tx_throw(InsertParams);

    using OrderVec = Node::vector_t;
    class OrderLoader {
        friend class Mempool;
        const Node::vector_t* ptr;
        size_t i { 0 };
        OrderLoader(const Node::vector_t* ptr)
            : ptr(ptr)
        {
        }

    public:
        class Entry {
            iter_t iter;
            Entry(iter_t iter)
                : iter(iter)
            {
            }
            friend OrderLoader;

        public:
            auto& hash() const { return iter->txhash; }
            auto& swap() const { return iter->get<LimitSwapMessage>(); }
        };
        wrt::optional<Entry> operator()()
        {
            if (ptr) {
                if (i >= ptr->size()) {
                    ptr = nullptr;
                } else {
                    Entry out { (*ptr)[i++] };
                    return out;
                }
            }
            return {};
        }
    };

private:
    OrderLoader get_sorted_orders(AssetId market, bool buy) const
    {
        if (auto it { orders.find(market) }; it != orders.end()) {
            return &it->second.sorted_orders(buy);
        };
        return nullptr;
    }

public:
    OrderLoader buys_desc(AssetId market) const
    {
        return get_sorted_orders(market, true);
    }
    // This class is representing a range of iterators
    // which correspond to entries of specific accountId
    class AccountTxs {
        friend Mempool;
    private:
        iter_t _begin;
        iter_t _end;
        AccountTxs(iter_t _begin, iter_t _end)
            : _begin(_begin)
            , _end(_end)
        {
        }

    public:
        iter_t begin() const { return _begin; }
        iter_t end() const { return _end; }
    };
    AccountTxs account_txs(AccountId accId) const;
    OrderLoader sells_asc(AssetId market) const
    {
        return get_sorted_orders(market, false);
    }
    size_t on_constraint_update();
    void erase(TransactionId id);
    void set_free_balance(AccountToken, Funds_uint64 newBalance);
    void set_allowed_blockversions(const std::set<BlockVersion>& s);
    size_t erase_from_height(NonzeroHeight, std::vector<TransactionMessage>* pOut = nullptr);
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
    // [[nodiscard]] wrt::optional<TokenFunds> token_spend_throw(const TransactionMessage& pm, chainserver::DBCache& cache) const;
    void erase_internal(Txset::const_iter_t);
    struct EraseResult {
        bool erasedWart;
        bool erasedToken;
    };
    EraseResult erase_internal(Txset::const_iter_t, balance_iterator wartIter, wrt::optional<balance_iterator> tokenIter = {});
    [[nodiscard]] balance_iterator create_or_get_balance_iter(AccountToken at, chainserver::DBCache& cache);
    void prune();

private:
    TransactionMapBool allowedTransactionTypes;
    Updates updates;
    MempoolTransactions transactions;
    BalanceEntries lockedBalances;
};
}
