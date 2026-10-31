#pragma once
#include "comparators.hpp"
#include "general/address_funds.hpp"
#include "mempool/log.hpp"
#include <set>
namespace chainserver {
struct TransactionIds;
}
namespace mempool {
struct BalanceEntry {
    Address address;
    BalanceEntry(const AddressFunds& af)
        : address(af.address)
        , avail(af.funds) {};

    void lock(Funds amount);
    void unlock(Funds amount);
    [[nodiscard]] bool set_avail(Funds amount);
    Funds remaining() const { return Funds::diff_assert(avail, used); }
    Funds locked() const { return used; }
    bool is_clean() { return used.is_zero(); }

private:
    Funds avail { Funds::zero() };
    Funds used { Funds::zero() };
};

class Mempool {
    using iter_t = Txmap::iterator;
    using const_iter_t = Txmap::const_iterator;

public:
    Mempool(bool master = true, size_t maxSize = 10000)
        : master(master)
        , maxSize(maxSize)
    {
    }

    [[nodiscard]] Log pop_log()
    {
        return std::move(log);
        log.clear();
    }
    void apply_log(const Log& log);
    int32_t insert_tx(const TransferTxExchangeMessage& pm, TransactionHeight txh, const TxHash& hash, const AddressFunds& e);
    void insert_tx_throw(const TransferTxExchangeMessage& pm, TransactionHeight txh, const TxHash& hash, const AddressFunds& e);
    size_t on_constraint_update();
    void erase(TransactionId id);
    void set_balance(AccountId, Funds newBalance);
    void erase_from_height(Height);
    void erase_before_height(Height);

    // getters
    [[nodiscard]] auto cache_validity() const { return txs.cache_validity(); }
    [[nodiscard]] auto get_payments(size_t n, NonzeroHeight height, std::vector<Hash>* hashes = nullptr) const
        -> std::vector<TransferTxExchangeMessage>;
    [[nodiscard]] auto sample(size_t) const -> std::vector<TxidWithFee>;
    [[nodiscard]] auto filter_new(const std::vector<TxidWithFee>&) const
        -> std::vector<TransactionId>;

    // operator[]
    [[nodiscard]] auto operator[](const TransactionId& id) const
        -> std::optional<TransferTxExchangeMessage>;
    [[nodiscard]] auto operator[](const HashView txHash) const
        -> std::optional<TransferTxExchangeMessage>;
    [[nodiscard]] size_t size() const { return txs.size(); }
    [[nodiscard]] CompactUInt min_fee() const;

private:
    using BalanceEntries = std::map<AccountId, BalanceEntry>;
    void apply_logevent(const Put&);
    void apply_logevent(const Erase&);
    void erase_internal(Txmap::const_iterator);
    bool erase_internal(Txmap::const_iterator, BalanceEntries::iterator, bool gc = true);
    void prune();

private:
    Log log;
    Txmap txs;
    std::set<const_iter_t, ComparatorPin> byPin;
    ByFeeDesc byFee;
    std::set<const_iter_t, ComparatorHash> byHash;
    BalanceEntries balanceEntries;
    bool master;
    size_t maxSize;
};
}
