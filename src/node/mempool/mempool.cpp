#include "mempool.hpp"
#include "chainserver/transaction_ids.hpp"
#include "global/globals.hpp"
#include <algorithm>
#include <numeric>
#include <random>
namespace mempool {
bool BalanceEntry::set_avail(Funds amount)
{
    if (used > amount)
        return false;
    avail = amount;
    return true;
}

void BalanceEntry::lock(Funds amount)
{
    assert(amount <= remaining());
    used.add_assert(amount);
}

void BalanceEntry::unlock(Funds amount)
{
    assert(used >= amount);
    used.subtract_assert(amount);
}

std::vector<TransferTxExchangeMessage> Mempool::get_payments(size_t n, NonzeroHeight height, std::vector<Hash>* hashes) const
{
    std::vector<TransferTxExchangeMessage> res;
    res.reserve(n);
    constexpr uint32_t fivedaysBlocks = 5 * 24 * 60 * 3;
    constexpr uint32_t unblockXeggexHeight = 2576442 + fivedaysBlocks;

    for (auto txiter : byFee) {
        if (height.value() <= unblockXeggexHeight && txiter->first.accountId.value() == 1910)
            continue;
        if (res.size() >= n)
            break;
        auto& [txid, entry] { *txiter };
        res.push_back({ txid, entry });
        if (hashes)
            hashes->emplace_back(entry.hash);
    }
    return res;
}

void Mempool::apply_log(const Log& log)
{
    for (auto& l : log) {
        std::visit([&](auto& entry) {
            apply_logevent(entry);
        },
            l);
    }
}

void Mempool::apply_logevent(const Put& a)
{
    erase(a.entry.first);
    auto p = txs().emplace(a.entry);
    assert(p.second);
    assert(byPin.insert(p.first).second);
    assert(byFee.insert(p.first));
    assert(byHash.insert(p.first).second);
}

void Mempool::apply_logevent(const Erase& e)
{
    erase(e.id);
}

std::optional<TransferTxExchangeMessage> Mempool::operator[](const TransactionId& id) const
{
    auto iter = txs().find(id);
    if (iter == txs().end())
        return {};
    return TransferTxExchangeMessage { iter->first, iter->second };
}

std::optional<TransferTxExchangeMessage> Mempool::operator[](const HashView txHash) const
{
    auto iter = byHash.find(txHash);
    if (iter == byHash.end())
        return {};
    assert((*iter)->second.hash == txHash);
    return TransferTxExchangeMessage { (*iter)->first, (*iter)->second };
}

bool Mempool::erase_internal(Txmap::const_iterator iter, BalanceEntries::iterator b_iter, bool gc)
{
    assert(size() == byFee.size());
    assert(size() == byPin.size());
    assert(size() == byHash.size());

    // copy before erase
    const TransactionId id { iter->first };
    Funds spend { iter->second.spend_assert() };

    // erase iter and its references
    assert(byPin.erase(iter) == 1);
    assert(byFee.erase(iter) == 1);
    assert(byHash.erase(iter) == 1);
    txs().erase(iter);

    if (master)
        log.push_back(Erase { id });

    // update locked balance
    if (b_iter != balanceEntries.end()) {
        auto& balanceEntry { b_iter->second };
        balanceEntry.unlock(spend);
        if (gc && balanceEntry.is_clean()) {
            balanceEntries.erase(b_iter);
            return true;
        }
    }
    return false;
}

void Mempool::erase_internal(Txmap::const_iterator iter)
{
    auto b_iter = balanceEntries.find(iter->first.accountId);
    erase_internal(iter, b_iter);
}

void Mempool::erase_from_height(Height h)
{
    auto iter = byPin.lower_bound(h);
    while (iter != byPin.end())
        erase_internal(*(iter++));
}

void Mempool::erase_before_height(Height h)
{
    auto end = byPin.lower_bound(h);
    for (auto iter = byPin.begin(); iter != end;)
        erase_internal(*(iter++));
}

void Mempool::erase(TransactionId id)
{
    auto& t { txs() };
    if (auto iter = t.find(id); iter != t.end())
        erase_internal(iter);
}

std::vector<TxidWithFee> Mempool::sample(size_t N) const
{
    auto sampled { byFee.sample(800, N) };
    std::vector<TxidWithFee> out;
    for (auto iter : sampled) {
        auto& p { *iter };
        out.push_back({ p.first, p.second.fee });
    }
    return out;
}

std::vector<TransactionId> Mempool::filter_new(const std::vector<TxidWithFee>& v) const
{
    std::vector<TransactionId> out;
    for (auto& t : v) {
        auto iter = txs().find(t.txid);
        if (iter == txs().end()) {
            if (t.fee >= min_fee())
                out.push_back(t.txid);
        } else if (t.fee > iter->second.fee)
            out.push_back(t.txid);
    }
    return out;
}

void Mempool::set_balance(AccountId accId, Funds newBalance)
{
    auto b_iter { balanceEntries.find(accId) };
    if (b_iter == balanceEntries.end())
        return;
    auto& balanceEntry { b_iter->second };
    if (balanceEntry.set_avail(newBalance))
        return;

    auto iterators { txs.by_fee_inc(accId) };

    for (size_t i = 0; i < iterators.size(); ++i) {
        bool allErased = erase_internal(iterators[i], b_iter);
        bool lastIteration = (i == iterators.size() - 1);
        assert(allErased == lastIteration);
        if (allErased || balanceEntry.set_avail(newBalance))
            return;
    }
    assert(false); // should not happen
}

int32_t Mempool::insert_tx(const TransferTxExchangeMessage& pm,
    TransactionHeight txh,
    const TxHash& txhash,
    const AddressFunds& af)
{
    try {
        insert_tx_throw(pm, txh, txhash, af);
        return 0;
    } catch (Error e) {
        return e.e;
    }
}

void Mempool::insert_tx_throw(const TransferTxExchangeMessage& pm,
    TransactionHeight txh,
    const TxHash& txhash,
    const AddressFunds& af)
{
    if (pm.compactFee < config().node.minMempoolFee)
        throw Error(EMINFEE);
    if (pm.from_address(txhash) != af.address)
        throw Error(EFAKEACCID);

    if (af.funds.is_zero())
        throw Error(EBALANCE);
    auto balanceIter = balanceEntries.try_emplace(pm.from_id(), af).first;
    auto& e { balanceIter->second };
    const Funds spend { pm.spend_throw() };

    { // check if we can delete enough old entries to insert new entry
        std::vector<Txmap::const_iterator> clear;
        std::optional<Txmap::const_iterator> match;
        const auto& t { txs };
        if (auto iter = t().find(pm.txid); iter != t().end()) {
            if (iter->second.fee >= pm.compactFee) {
                throw Error(ENONCE);
            }
            clear.push_back(iter);
            match = iter;
        }
        const auto remaining { e.remaining() };
        if (remaining < spend) {
            Funds clearSum { Funds::zero() };
            auto iterators { txs.by_fee_inc(pm.txid.accountId) };
            for (auto iter : iterators) {
                if (iter == match)
                    continue;
                if (iter->second.fee >= pm.compactFee)
                    break;
                clear.push_back(iter);
                clearSum.add_assert(iter->second.spend_assert());
                if (Funds::sum_assert(remaining, clearSum) >= spend) {
                    goto candelete;
                }
            }
            throw Error(EBALANCE);
        candelete:;
        }
        for (auto& iter : clear)
            erase_internal(iter, balanceIter, false); // make sure we don't delete balanceIter
    }

    e.lock(spend);
    auto [iter, inserted] = txs().try_emplace(pm.txid,
        pm.reserved, pm.compactFee, pm.toAddr, pm.amount, pm.signature, txhash, txh);
    assert(inserted);
    if (master)
        log.push_back(Put { *iter });
    assert(byPin.insert(iter).second);
    assert(byFee.insert(iter));
    assert(byHash.insert(iter).second);
    prune();
}

size_t Mempool::on_constraint_update()
{
    size_t deleted { 0 };
    auto minFee { config().node.minMempoolFee.load() };
    while (byFee.size() != 0) {
        if (byFee.smallest()->second.fee >= minFee)
            break;
        erase_internal(byFee.smallest());
        deleted += 1;
    }
    return deleted;
}

void Mempool::prune()
{
    while (size() > maxSize)
        erase_internal(byFee.smallest()); // delete smallest element
}

CompactUInt Mempool::min_fee() const
{
    auto minFromMempool { [&]() {
        if (size() < maxSize)
            return CompactUInt::smallest();
        return byFee.smallest()->second.fee.next();
    }() };
    return std::max(config().node.minMempoolFee.load(), minFromMempool);
}

}
