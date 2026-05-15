#include "consensus.hpp"
#include "cache.hpp"
#include "chainserver/db/chain_db.hpp"
#include "communication/create_transaction.hpp"
#include "global/globals.hpp"
#include <spdlog/spdlog.h>
namespace chainserver {
namespace {

auto next_block_version(const Headerchain& hc)
{
    auto nextHeight { hc.length().add1() };
    return BlockVersion::hardcoded_for_params(nextHeight);
}

}

Chainstate::Chainstate(const ChainDB& db, BatchRegistry& br)
    : Chainstate(db.get_consensus_headers(), db, br)
{
}

Chainstate::Chainstate(
    std::tuple<std::vector<HeaderBatch>, HistoryHeights, State64Heights> init,
    const ChainDB& db,
    BatchRegistry& br)
    : db(db)
    , headerchain(std::move(std::get<0>(init)), br)
    , nextBlockversion(next_block_version(headerchain))
    , historyOffsets(std::move(std::get<1>(init)))
    , stateOffsets(std::move(std::get<2>(init)))
    , chainTxIds(db.fetch_tx_ids(length()))
    , _mempool({ nextBlockversion })
{
    assert(this->historyOffsets.size() == headerchain.length());
    spdlog::info("Cache has {} entries", chainTxIds.size());
}

void Chainstate::assert_equal_length()
{
    assert(historyOffsets.size() == headerchain.length());
    assert(stateOffsets.size() == headerchain.length());
}

void Chainstate::fork(Chainstate::ForkData&& fd)
{
    const auto shrinkLength { fd.rollbackResult.shrink.length };
    const auto forkHeight { shrinkLength.add1() };
    assert(fd.rollbackResult.shrink.length < length());
    // increment descriptor
    dsc += 1;

    // take transactions that are affected by rollback out of mempool
    auto txsReinsert { std::move(fd.rollbackResult.toMempool) };
    _mempool.erase_from_height(forkHeight, &txsReinsert);

    // adapt header chain and offsets
    headerchain = std::move(fd.stage);
    historyOffsets.shrink(fd.rollbackResult.shrink.length);
    historyOffsets.append_vector(fd.appendResult.newHistoryOffsets);
    stateOffsets.shrink(fd.rollbackResult.shrink.length);
    stateOffsets.append_vector(fd.appendResult.stateOffsets);
    assert_equal_length();

    //////////////////////////////
    // inform mempool about balance changes
    auto balanceUpdates { std::move(fd.appendResult.freeBalanceUpdates) };
    balanceUpdates.merge(std::move(fd.rollbackResult.freeBalanceUpdates));
    update_free_balances(balanceUpdates);

    // set transaction ids
    chainTxIds = std::move(fd.rollbackResult.chainTxIds);
    chainTxIds.merge(std::move(fd.appendResult.newTxIds));

    // remove transactions used in forked chain from mempool
    for (auto& tid : chainTxIds)
        _mempool.erase(tid);
    update_allowed_mempool_transaction_types();

    insert_txs(std::move(txsReinsert));

    prune_txids();
}

void Chainstate::update_allowed_mempool_transaction_types()
{
    auto newNextBlockversion { next_block_version(headerchain) };
    if (nextBlockversion != newNextBlockversion) {
        nextBlockversion = newNextBlockversion;
        set_allowed_mempool_transaction_types();
    }
}

void Chainstate::set_allowed_mempool_transaction_types()
{
    _mempool.set_allowed_blockversions({ nextBlockversion });
}

auto Chainstate::rollback(const RollbackResult& rb) -> HeaderchainRollback
{
    const auto shrinkLength { rb.shrink.length };
    const auto forkHeight { shrinkLength.add1() };
    assert(rb.shrink.length < length());

    // increment descriptor
    dsc += 1;

    // take transactions that are affected by rollback out of mempool
    auto txsReinsert { std::move(rb.toMempool) };
    _mempool.erase_from_height(forkHeight, &txsReinsert);

    // adapt header chain and offsets
    headerchain.shrink(rb.shrink.length);
    historyOffsets.shrink(rb.shrink.length);
    stateOffsets.shrink(rb.shrink.length);
    assert_equal_length();

    // set transaction ids
    chainTxIds = std::move(rb.chainTxIds);

    prune_txids();

    // remove from mempool
    for (auto& tid : chainTxIds)
        _mempool.erase(tid);

    //////////////////////////////
    // inform mempool about balance changes
    update_free_balances(rb.freeBalanceUpdates);

    update_allowed_mempool_transaction_types();

    insert_txs(std::move(txsReinsert));

    return HeaderchainRollback {
        .shrink { rb.shrink },
        .descriptor = dsc
    };
}

auto Chainstate::append(AppendMulti ad) -> HeaderchainAppend
{
    // safety check, length must increment
    const Height l { length() };
    assert(l <= ad.patchedChain.length());
    if (l != 0) {
        auto ll { l.nonzero_assert() };
        assert(ad.patchedChain[ll] == headerchain[ll]);
    }

    // adapt header chain and offsets
    headerchain = std::move(ad.patchedChain);
    historyOffsets.append_vector(ad.appendResult.newHistoryOffsets);
    stateOffsets.append_vector(ad.appendResult.stateOffsets);
    assert_equal_length();

    //////////////////////////////
    // inform mempool about balance changes
    update_free_balances(ad.appendResult.freeBalanceUpdates);

    // remove from mempool
    // remove outdated transactions
    auto nextBlockPinBegin { (ad.patchedChain.length() + 1).pin_begin() };
    _mempool.erase_pinned_before_height(nextBlockPinBegin);
    // remove used transactions
    for (const auto& tid : ad.appendResult.newTxIds)
        _mempool.erase(tid);

    // merge transaction ids
    chainTxIds.merge(std::move(ad.appendResult.newTxIds));

    update_allowed_mempool_transaction_types();

    // prune transaction ids
    prune_txids();

    // return append message
    return headers().get_append(l);
}

auto Chainstate::append(AppendSingle d) -> HeaderchainAppend
{
    const Height l { length() };

    // adapt header chain and offsets
    headerchain.append(d.prepared, *global().batchRegistry);
    historyOffsets.append(d.newHistoryOffset);
    stateOffsets.append(d.newStateOffset);
    assert_equal_length();

    //////////////////////////////
    // inform mempool about balance changes
    update_free_balances(d.freeBalanceUpdates);

    // remove from mempool
    // remove outdated transactions
    auto nextBlockPinBegin { l.add1().pin_begin() };
    _mempool.erase_pinned_before_height(nextBlockPinBegin);
    // remove used transactions
    for (auto& tid : d.newTxIds)
        _mempool.erase(tid);

    // merge transaction ids
    chainTxIds.merge(std::move(d.newTxIds));

    update_allowed_mempool_transaction_types();

    // prune transaction ids
    prune_txids();

    // return append message
    return headers().get_append(l);
}

size_t Chainstate::on_mempool_constraint_update()
{
    return _mempool.on_constraint_update();
};

auto Chainstate::insert_txs(std::vector<TransactionMessage>&& txs) -> std::vector<Error>
{
    DBCache c(db);
    std::vector<Error> res;
    res.reserve(txs.size());
    for (auto& tx : txs) {
        try {
            insert_tx_throw(std::move(tx), c);
            res.push_back(0);
        } catch (const Error& e) {
            res.push_back(e.code);
        }
    }
    return res;
}

TxHash Chainstate::insert_tx_throw(TransactionMessage&& tm, DBCache& cache)
{
    auto txHash { tm.txhash(pin_hash(tm.pin_height())) };
    auto fromAddr = db.lookup_address(tm.from_id());
    if (!fromAddr)
        throw Error(EACCIDNOTFOUND);
    if (tm.from_address(txHash) != fromAddr)
        throw Error(EFAKEACCID);

    return insert_tx_internal_throw(std::move(tm), txHash, *fromAddr, cache);
}

PinHash Chainstate::pin_hash(PinHeight pinHeight) const
{
    if (pinHeight > length())
        throw Error(EPINHEIGHT);
    if (pinHeight < length().add1().pin_begin())
        throw Error(EPINHEIGHT);
    return headers().hash_at(pinHeight);
}

[[nodiscard]] TxHash Chainstate::create_tx_throw(const TransactionCreate& m)
{
    return m.visit([&]<typename T>(T&& m) {
        DBCache cache(db);
        PinHeight pinHeight = m.pin_height();
        auto txHash { m.tx_hash(pin_hash(pinHeight)) };
        auto fromAddr = m.from_address(txHash);
        auto fromId = db.lookup_account(fromAddr);
        if (!fromId)
            throw Error(EADDRNOTFOUND);
        // TxHeight th(pinHeight, account_height(*fromId)); // TODO: rethink transaction height
        TransactionId txid(*fromId, pinHeight, m.nonce_id());
        return insert_tx_internal_throw(create_specific_tx(txid, std::forward<T>(m)), txHash, fromAddr, cache);
    });
}

WartTransferMessage Chainstate::create_specific_tx(const TransactionId& txid, const WartTransferCreate& m)
{
    return { txid, m.nonce_reserved(), m.compact_fee(), m.to_addr(), m.wart(), m.signature() };
}

TokenTransferMessage Chainstate::create_specific_tx(const TransactionId& txid, const TokenTransferCreate& m)
{

    return { txid, m.nonce_reserved(), m.compact_fee(), m.asset_hash(), m.is_liquidity(), m.to_addr(), m.amount(), m.signature() };
}

LimitSwapMessage Chainstate::create_specific_tx(const TransactionId& txid, const LimitSwapCreate& m)
{
    return { txid, m.nonce_reserved(), m.compact_fee(), m.asset_hash(), m.buy(), m.amount(), m.limit(), m.signature() };
}

LiquidityDepositMessage Chainstate::create_specific_tx(const TransactionId& txid, const LiquidityDepositCreate& m)
{
    return { txid, m.nonce_reserved(), m.compact_fee(), m.asset_hash(), m.amount(), m.wart(), m.signature() };
}

LiquidityWithdrawalMessage Chainstate::create_specific_tx(const TransactionId& txid, const LiquidityWithdrawalCreate& m)
{
    return { txid, m.nonce_reserved(), m.compact_fee(), m.asset_hash(), m.shares(), m.signature() };
}
CancelationMessage Chainstate::create_specific_tx(const TransactionId& txid, const CancelationCreate& m)
{
    return { txid, m.nonce_reserved(), m.compact_fee(), m.cancel_height(), m.cancel_nonceid(), m.signature() };
}
AssetCreationMessage Chainstate::create_specific_tx(const TransactionId& txid, const AssetCreationCreate& m)
{
    return { txid, m.nonce_reserved(), m.compact_fee(), m.supply(), m.asset_name(), m.signature() };
}

void Chainstate::update_free_balances(const FreeBalanceUpdates& updates)
{
    // First set balance for nonWart because this will implicitly free some
    // frozen from mempool WART too, i.e. this order is superior in purging
    // as few entries as possible from the mempool.
    for (auto& [accountToken, balance] : updates.nonWart)
        _mempool.set_free_balance(accountToken, balance);
    for (auto& [accountToken, balance] : updates.wart)
        _mempool.set_free_balance(accountToken, balance);
}

TxHash Chainstate::insert_tx_internal_throw(TransactionMessage&& m, TxHash txHash, const Address& fromAddr, DBCache& c)
{
    auto stateHeight { state_height(m.from_id()) };
    std::optional<AssetId> assetId;

    // These two variables are set when the transaction uses non-wart token:
    TokenId tokenId { TokenId::WART };
    Funds_uint64 spend { 0 };

    if (auto nw { m.nonwart_token_throw() }) {
        auto aid { c.fetch_asset_throw(nw->assetHash).id };
        assetId = aid;

        // adjust state height (height that if rolled back earlier
        // causes transaction to be reinserted into the mempool)
        auto sh { state_height(aid) };
        // sh is height at which asset was introduced into the chain
        if (sh > stateHeight)
            stateHeight = sh;

        tokenId = aid.token_id(nw->isLiquidity);
        spend = nw->spend;
    }

    TxHeight th(m.pin_height(), stateHeight);

    if (txids().contains(m.txid()))
        throw Error(ENONCE);
    if (m.compact_fee() < config().minMempoolFee)
        throw Error(EMINFEE);
    // additional checks
    m.visit_overload(
        [&](const WartTransferMessage& m) {
            if (fromAddr == m.to_addr())
                throw Error(ESELFSEND);
        },
        [&](const TokenTransferMessage& m) {
            if (fromAddr == m.to_addr())
                throw Error(ESELFSEND);
        },
        [&](const auto&) {});
    _mempool.insert_tx_throw({ .msg { std::move(m) }, .assetId { assetId }, .height { th }, .hash { txHash }, .dbCache { c }, .nonwart { .tokenId { tokenId }, .spend { spend } } });
    return txHash;
}

void Chainstate::prune_txids()
{
    chainTxIds.prune(length());
}
mempool::Updates Chainstate::pop_mempool_updates()
{
    return _mempool.pop_updates();
}
}
