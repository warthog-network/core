#include "mempool.hpp"
#include "api/events/emit.hpp"
#include "block/body/transaction_map.hpp"
#include "block/version.hpp"
#include "chainserver/state/helpers/cache.hpp"
#include "general/format_plural.hpp"
#include "global/globals.hpp"
namespace mempool {
bool LockedBalance::try_set_avail(Funds_uint64 amount)
{
    if (used > amount)
        return false;
    avail = amount;
    return true;
}

void LockedBalance::lock(Funds_uint64 amount)
{
    assert(amount <= free());
    used.add_assert(amount);
}

void LockedBalance::unlock(Funds_uint64 amount)
{
    assert(used >= amount);
    used.subtract_assert(amount);
}

std::vector<TransactionMessage> MempoolTransactions::get_transactions(size_t n, NonzeroHeight height, std::vector<TxHash>* hashes) const
{
    std::vector<TransactionMessage> res;
    res.reserve(n);
    constexpr uint32_t fivedaysBlocks = 5 * 24 * 60 * 3;
    constexpr uint32_t unblockXeggexHeight = 2576442 + fivedaysBlocks;

    std::set<TransactionId> tx_txids;
    std::set<TransactionId> cancel_txids;
    for (auto txiter : byFee) {
        if (res.size() >= n)
            break;
        if (height.value() <= unblockXeggexHeight && txiter->from_id().value() == 1910)
            continue;
        auto& tx { *txiter };
        auto id { tx.txid() };
        if (tx_txids.contains(id) || cancel_txids.contains(id))
            continue;
        if (tx.holds<CancelationMessage>()) {
            auto cid { tx.get<CancelationMessage>().cancel_txid() };
            assert(cid != id); // should be ensured in CancelationMessage::throw_if_bad
            if (tx_txids.contains(cid))
                continue;
            cancel_txids.insert(cid);
        }
        tx_txids.insert(id);

        res.push_back(tx);
        if (hashes)
            hashes->emplace_back(tx.txhash);
    }
    return res;
}

auto MempoolTransactions::insert(Entry e) -> std::pair<iter_t, bool>
{
    auto p = txs().insert(std::move(e));
    assert(p.second);
    assert(_index.insert(p.first));
    assert(byFee.insert(p.first));
    assert(byToken.insert(p.first));
    return p;
}

void MempoolTransactions::replay_updates(const Updates& log)
{
    for (auto& l : log) {
        std::visit([&](auto& entry) {
            apply_update(entry);
        },
            l);
    }
}

void MempoolTransactions::apply_update(const Put& a)
{
    erase(a.entry.txid());
    api::event::emit_mempool_add(a, txs.size());
    insert(std::move(a.entry));
}

void MempoolTransactions::apply_update(const Erase& e)
{
    erase(e.id);
    api::event::emit_mempool_erase(e, size());
}
bool MempoolTransactions::erase(TransactionId txid)
{
    if (auto iter { txs().find(txid) }; iter != txs().end()) {
        erase(iter);
        return true;
    }
    return false;
}

void MempoolTransactions::erase(iter_t iter)
{
    assert(size() == _index.size());
    assert(size() == byFee.size());
    assert(size() == byToken.size());
    // erase iter and its references
    assert(_index.erase(iter) == 1);
    assert(byFee.erase(iter) == 1);
    assert(byToken.erase(iter) == 1);
    txs().erase(iter);
}

wrt::optional<TransactionMessage> MempoolTransactions::operator[](const TransactionId& id) const
{
    auto iter = txs().find(id);
    if (iter == txs().end())
        return {};
    return *static_cast<const TransactionMessage*>(&*iter);
}

wrt::optional<TransactionMessage> MempoolTransactions::operator[](const HashView txHash) const
{
    auto iter = _index.hash().find(txHash);
    if (iter == _index.hash().end())
        return {};
    assert((*iter)->txhash == txHash);
    return *static_cast<const TransactionMessage*>(&**iter);
}
CompactUInt MempoolTransactions::min_fee() const
{
    auto minFromMempool { [&]() {
        if (size() < maxSize)
            return CompactUInt::smallest();
        return byFee.smallest()->compact_fee().next();
    }() };
    return std::max(config().minMempoolFee.load(), minFromMempool);
}

std::vector<TxidWithFee> MempoolTransactions::sample(size_t N, bool onlyWartTransfer) const
{
    auto sampled { byFee.sample(800, N) };
    std::vector<TxidWithFee> out;
    for (auto iter : sampled) {
        if (onlyWartTransfer && !iter->holds<WartTransferMessage>())
            continue;
        out.push_back({ iter->txid(), iter->compact_fee() });
    }
    return out;
}
std::vector<TransactionId> MempoolTransactions::filter_new(const std::vector<TxidWithFee>& v) const
{
    std::vector<TransactionId> out;
    for (auto& t : v) {
        auto iter = txs().find(t.txid);
        if (iter == txs().end()) {
            if (t.fee >= min_fee())
                out.push_back(t.txid);
        } else if (t.fee > iter->compact_fee())
            out.push_back(t.txid);
    }
    return out;
}

auto Mempool::erase_internal(Txset::const_iter_t iter, balance_iterator wartIter, wrt::optional<balance_iterator> tokenIter) -> EraseResult
{

    EraseResult er { false, false };

    // now unlock balances that were occupied by erased mempool entry
    updates.push_back(Erase { iter->txid() });
    auto unlock { [&](BalanceEntries::iterator& iter, Funds_uint64 amount) {
        assert(iter != lockedBalances.end()); // because there is nonzero locked balance (t->amount != 0)
        auto& balanceEntry { iter->second };
        balanceEntry.unlock(amount);
        if (balanceEntry.is_clean()) {
            lockedBalances.erase(iter);
            iter = lockedBalances.end();
            return true;
        }
        return false;
    } };
    // update locked token balance
    if (auto tokenSpend { iter->nonwart_token_assert() }) {
        if (tokenSpend->spend > 0) {
            if (!tokenIter)
                tokenIter = lockedBalances.find({ iter->from_id(), iter->altTokenId });
            er.erasedToken = unlock(*tokenIter, tokenSpend->spend);
        }
    }

    // update locked wart balance
    Wart wartSpend { iter->spend_wart_assert() };
    er.erasedWart = unlock(wartIter, wartSpend);

    if (iter->holds<LimitSwapMessage>()) {
        auto nw { iter->altTokenId.non_wart() };
        assert(nw); // should be set at mempool insert
        auto N { erase_swap(nw->asset_id(), iter) };
        assert(N == 1); // should match exactly one element
    }

    transactions.erase(iter);
    return er;
}

void Mempool::Node::sort()
{
    auto lambda { [](iter_t lhs, iter_t rhs) static {
        return lhs->get<LimitSwapMessage>().limit() < rhs->get<LimitSwapMessage>().limit();
    } };
    std::ranges::sort(buys, lambda);
    std::ranges::sort(sells, lambda);
}

void Mempool::insert_swap(AssetId market, iter_t iter)
{
    auto& swap { iter->get<LimitSwapMessage>() };
    orders[market].push_back(swap.buy(), iter);
}
size_t Mempool::erase_swap(AssetId market, iter_t iter)
{
    auto& swap { iter->get<LimitSwapMessage>() };
    return orders[market].erase(swap.buy(), iter);
}

void Mempool::erase_internal(Txset::const_iter_t iter)
{
    auto wartIter = lockedBalances.find({ iter->from_id(), TokenId::WART });
    assert(wartIter != lockedBalances.end());
    erase_internal(iter, wartIter);
}

size_t Mempool::erase_from_height(NonzeroHeight h, std::vector<TransactionMessage>* pOut)
{
    size_t i { 0 };
    auto iter { transactions.index().txheight().lower_bound(h) };
    while (iter != transactions.index().txheight().end()) {
        if (pOut)
            pOut->push_back(std::move(**iter));
        i += 1;
        erase_internal(*(iter++));
    }
    return i;
}

void Mempool::erase_pinned_before_height(Height h)
{
    auto end = transactions.index().pin().lower_bound(h);
    for (auto iter = transactions.index().pin().begin(); iter != end;)
        erase_internal(*(iter++));
}

void Mempool::erase(TransactionId id)
{
    auto& t { transactions };
    if (auto iter = t.find(id); iter != t.end())
        erase_internal(iter);
}

void Mempool::set_free_balance(AccountToken at, Funds_uint64 newBalance)
{
    auto tokenIter { lockedBalances.find(at) };
    if (tokenIter == lockedBalances.end())
        return;
    auto& balanceEntry { tokenIter->second };
    if (balanceEntry.try_set_avail(newBalance))
        return;
    if (at.token_id() == TokenId::WART) {

        auto iterators { transactions.by_fee_inc_le(at.account_id()) };
        for (size_t i = 0; i < iterators.size(); ++i) {
            bool allErased = erase_internal(iterators[i], tokenIter).erasedWart;
            bool lastIteration = (i == iterators.size() - 1);
            assert(allErased == lastIteration);
            // balanceEntry reference is invalidateed when all entries are erased
            // because it will be wiped together with last entry.
            if (allErased || balanceEntry.try_set_avail(newBalance))
                return;
        }
        assert(iterators.empty()); // can only reach this point when empty
    } else {
        auto wart_iter { lockedBalances.find({ at.account_id(), TokenId::WART }) };

        // since tokenIter != end(), there are some transactions in the mempool
        // associated with this account and so there must be some WART locked.
        assert(wart_iter != lockedBalances.end()); // since some WART must be locked

        auto& sorted { transactions.index().account_token_fee() };
        auto iter = sorted.lower_bound(at);
        auto iteration_done { [&]() { return iter == sorted.end() || (*iter)->account_token() != at; } };
        bool done { iteration_done() };
        assert(!done); // tokenIter != end(), there must be some entries for `at`.
        // We know that `balanceEntry.try_set_avail(newBalance) == false` here, was verified before
        do {
            bool erasedTokenEntry { erase_internal(*iter++, wart_iter, tokenIter).erasedToken };
            done = iteration_done();
            assert(erasedTokenEntry == done);
        } while (!done &&
            // by short circuiting, we know that erasedTokenEntry == false and balanceEntry reference is valid.
            !balanceEntry.try_set_avail(newBalance));
    }
}

void Mempool::set_allowed_blockversions(const std::set<BlockVersion>& newSet)
{
    auto supports_transaction { []<typename T>(const std::set<BlockVersion>& vs, T*) {
        for (auto v : vs) {
            if (T::allows_blockversion(v))
                return true;
        }
        return false;
    } };
    auto tmpAllowedTransactions { allowedTransactionTypes };
    bool needsDeletion = false;
    allowedTransactionTypes.for_each([&]<typename T>(T* t, auto& ref) {
        bool& tmpRef = tmpAllowedTransactions.at<T>();
        const bool oldAllowsTransaction = tmpRef;
        const bool newAllowsTransaction = supports_transaction(newSet, t);

        // write new value
        ref = newAllowsTransaction;

        // write tmp value whether we need to delete
        // such transactions from mempool,
        tmpRef = !newAllowsTransaction && oldAllowsTransaction;
        needsDeletion |= tmpRef;
    });

    size_t deleted { 0 };
    if (needsDeletion) {
        deleted = erase_if([&](const mempool::Entry& e) {
            return e.visit([&](auto& tx) {
                // return whether we must delete it from mempool
                return tmpAllowedTransactions.at<
                    typename std::remove_cvref_t<decltype(tx)>::transaction_t>();
            });
        });
    }
    spdlog::debug("Deleted {} of unsupported type from mempool", format_plural(deleted, "transaction"));
}

// Error Mempool::insert_tx(const TransactionMessage& pm, TxHeight txHeight, const TxHash& txHash, chainserver::DBCache& cache)
// {
//     try {
//         insert_tx_throw(pm, txHeight, txHash, cache);
//         return 0;
//     } catch (Error e) {
//         return e;
//     }
// }

// wrt::optional<TokenFunds> Mempool::token_spend_throw(const TransactionMessage& pm, chainserver::DBCache& cache) const
// {
//     if (auto s { pm.nonwart_token_throw() }) {
//         if (auto pAsset { cache.lookup_asset(s->hash) })
//             return TokenFunds { pAsset->id.token_id(s->isLiquidity), s->amount };
//         throw Error(EASSETHASHNOTFOUND);
//     }
//     return {};
// }

auto Mempool::get_balance(AccountToken at, chainserver::DBCache& cache) -> std::pair<LockedBalance, wrt::optional<balance_iterator>>
{
    auto balanceIter { lockedBalances.find(at) };
    if (balanceIter == lockedBalances.end()) {
        auto total { cache.balance[at] };
        return { LockedBalance(total), {} };
    }
    return { balanceIter->second, balanceIter };
}

auto Mempool::create_or_get_balance_iter(AccountToken at, chainserver::DBCache& cache) -> balance_iterator
{
    auto balanceIter { lockedBalances.lower_bound(at) };
    if (balanceIter == lockedBalances.end() || balanceIter->first != at) {
        // need to insert
        balanceIter = lockedBalances.emplace_hint(balanceIter, at, cache.balance[at]);
    }
    return balanceIter;
}

void Mempool::insert_tx_throw(InsertParams tx)
{
    auto canInsertTxType { tx.msg.visit([&](auto tx) {
        return allowedTransactionTypes.at<
            typename std::remove_cvref_t<decltype(tx)>::transaction_t>();
    }) };
    if (!canInsertTxType)
        throw Error(ETXTYPESTATE);

    auto fromId { tx.msg.from_id() };

    wrt::optional<Txset::const_iter_t> match;
    std::vector<Txset::const_iter_t> clear;
    const auto& t { transactions };
    if (auto iter = t.find(tx.msg.txid()); iter != t.end()) {
        if (iter->compact_fee() >= tx.msg.compact_fee()) {
            throw Error(ENONCE);
        }
        clear.push_back(iter);
        match = iter;
    }

    const Wart wartSpend { tx.msg.spend_wart_throw() };
    auto [wartBal, wartIter] { get_balance({ tx.msg.from_id(), TokenId::WART }, tx.dbCache) };
    if (wartBal.total() < wartSpend)
        throw Error(EBALANCE);

    size_t token_idx0 { clear.size() };
    if (tx.nonwart.spend > 0) { // if this transaction spends tokens different from WART
        assert(tx.nonwart.tokenId != TokenId::WART);
        // first make sure we can delete enough elements from the
        // mempool to cover the amount of nonwart tokens needed
        // for this transaction

        AccountToken at { fromId, tx.nonwart.tokenId };
        auto [tokenBal, bal_iter] { get_balance(at, tx.dbCache) };
        assert(wartIter || !bal_iter); // if no wart iterator then there is no entry in lockedBalances for any token with this account because every transaction locks some WART.
        if (tokenBal.total() < tx.nonwart.spend)
            throw Error(ETOKBALANCE);
        auto& set { transactions.index().account_token_fee() };
        // loop through the range where the AccountToken is equal
        for (auto it { set.lower_bound(at) };
            it != set.end() && (*it)->altTokenId == at.token_id() && (*it)->from_id() == fromId; ++it) {
            if (tokenBal.free() >= tx.nonwart.spend)
                break;
            auto iter = *it;
            if (iter == match)
                continue;
            if (iter->compact_fee() >= tx.msg.compact_fee())
                break;
            clear.push_back(iter);
            wartBal.unlock(iter->spend_wart_assert());
            auto nonWart { iter->nonwart_token_throw() };
            assert(nonWart.has_value());
            tokenBal.unlock(nonWart->spend);
        }
        if (tokenBal.free() < tx.nonwart.spend)
            throw Error(ETOKBALANCE);
    }
    size_t token_idx1 { clear.size() };
    size_t i { token_idx0 };

    { // check if we can delete enough old entries to insert new entry
        if (wartBal.free() < wartSpend) {
            auto iterators { transactions.by_fee_inc_le(tx.msg.txid().accountId, tx.msg.compact_fee()) };
            for (auto iter : iterators) {
                if (iter == match)
                    continue;
                if (iter->compact_fee() >= tx.msg.compact_fee())
                    break;
                if (i < token_idx1) {
                    if (clear[i] == iter) {
                        // iter already inserted in token balance loop
                        i += 1;
                        continue;
                    };
                }
                clear.push_back(iter);
                wartBal.unlock(iter->spend_wart_assert());
                if (wartBal.free() >= wartSpend)
                    goto candelete;
            }
            throw Error(EBALANCE);
        candelete:;
        }
    }

    assert(clear.empty() || wartIter); // if there exist transactions that can be deleted, then there must be some WART locked.
    for (auto& iter : clear)
        erase_internal(iter, *wartIter);
    create_or_get_balance_iter({ fromId, TokenId::WART }, tx.dbCache)->second.lock(wartSpend);
    if (tx.nonwart.spend > 0)
        create_or_get_balance_iter({ fromId, tx.nonwart.tokenId }, tx.dbCache)->second.lock(tx.nonwart.spend);

    auto [iter, inserted] = transactions.insert(Entry { std::move(tx.msg), tx.hash, tx.height, tx.nonwart.tokenId });
    if (iter->holds<LimitSwapMessage>()) {
        assert(tx.assetId.has_value()); // this was set at caller side
        insert_swap(*tx.assetId, iter); // to keep track of mempool orderbook
    }
    assert(inserted);
    updates.push_back(Put { *iter });
    prune();
}

size_t Mempool::on_constraint_update()
{
    size_t deleted { 0 };
    auto minFee { config().minMempoolFee.load() };
    while (size() != 0) {
        if (transactions.by_fee().smallest()->compact_fee() >= minFee)
            break;
        erase_internal(transactions.by_fee().smallest());
        deleted += 1;
    }
    return deleted;
}

void Mempool::prune()
{
    while (size() > transactions.max_size())
        erase_internal(transactions.by_fee().smallest()); // delete smallest element
}

}
