#include "defi/token/account_token.hpp"
#include "general/function_traits.hpp"
#include "general/now.hpp"
#include "helpers/cache.hpp"
#ifndef DISABLE_LIBUV
#include "api/http/endpoint.hpp"
#endif

#include "../db/chain_db.hpp"
#include "api/types/all.hpp"
#include "block/body/rollback.hpp"
#include "block/chain/history/history.hpp"
#include "block/header/generator.hpp"
#include "block/header/header_impl.hpp"
#include "communication/create_transaction.hpp"
#include "eventloop/types/chainstate.hpp"
#include "general/hex.hpp"
#include "general/is_testnet.hpp"
#include "global/globals.hpp"
#include "spdlog/spdlog.h"
#include "state.hpp"
#include "transactions/apply_stage.hpp"
#include "transactions/block_applier.hpp"
#include <ranges>

namespace chainserver {

void MiningCache::update_validity(CacheValidity cv)
{
    if (cacheValidity != cv)
        cache.clear();
    cacheValidity = cv;
}

auto MiningCache::lookup(const Address& a, bool disableTxs) const
    -> const value_t*
{
    auto iter { std::find_if(cache.begin(), cache.end(), [&](const Item& i) {
        return i.address == a && i.disableTxs == disableTxs;
    }) };
    if (iter != cache.end())
        return &iter->b;
    return nullptr;
}

auto MiningCache::insert(const Address& a, bool disableTxs, value_t v)
    -> const value_t&
{
    cache.push_back({ a, disableTxs, std::move(v) });
    return cache.back().b;
}

State::State(ChainDB& db, BatchRegistry& br,
    wrt::optional<SnapshotSigner> snapshotSigner)
    : db(db)
    , dbcache(db)
    , batchRegistry(br)
    , snapshotSigner(std::move(snapshotSigner))
    , signedSnapshot(db.get_signed_snapshot())
    , chainstate(db, br)
    , nextGarbageCollect(std::chrono::steady_clock::now())
    , _miningCache(mining_cache_validity())
{
}

wrt::optional<api::HeaderInfo> State::get_header(Height h) const
{
    if (auto p { chainstate.headers().get_header(h) }; p.has_value())
        return api::HeaderInfo { h.nonzero_assert(), Header(p.value()) };
    return {};
}

auto State::api_get_header(const api::HeightOrHash& hh) const
    -> Result<api::HeaderInfo>
{
    if (std::holds_alternative<Height>(hh.data)) {
        return get_header(std::get<Height>(hh.data));
    }
    auto h { consensus_height(std::get<Hash>(hh.data)) };
    if (!h.has_value())
        return Error(ENOTFOUND);
    return get_header(*h);
}

wrt::optional<NonzeroHeight> State::consensus_height(const Hash& hash) const
{
    auto o { db.lookup_block_height(hash) };
    if (!o.has_value())
        return {};
    auto& h { o.value() };
    auto hash2 { chainstate.headers().get_hash(h) };
    if (!hash2.has_value() || *hash2 != hash)
        return {};
    return h;
}

wrt::optional<Hash> State::get_hash(Height h) const
{
    return chainstate.headers().get_hash(h);
}

wrt::optional<api::BlockBinary>
State::api_get_block_binary(const api::HeightOrHash& hh) const
{
    if (std::holds_alternative<Height>(hh.data)) {
        return api_get_block_binary(std::get<Height>(hh.data));
    }
    auto h { consensus_height(std::get<Hash>(hh.data)) };
    if (!h.has_value())
        return {};
    return api_get_block_binary(*h);
}

Result<api::Block> State::api_get_block(const api::HeightOrHash& hh) const
{
    if (std::holds_alternative<Height>(hh.data)) {
        return api_get_block(std::get<Height>(hh.data));
    }
    return consensus_height(std::get<Hash>(hh.data))
        .and_then([&](NonzeroHeight h) { return api_get_block(h); });
}

namespace {

void push_history(api::Block& b, const std::pair<HistoryId, history::Entry>& p,
    chainserver::DBCache& c, PinFloor pinFloor)
{
    auto& [hid, e] { p };
    auto signed_info_data { [&](const history::SignData& sd) {
        return api::block::SignedInfoData(
            e.hash, hid, sd.origin_account_id(),
            c.addresses.fetch(sd.origin_account_id()), sd.fee(), sd.pin_nonce().id,
            sd.pin_nonce().pin_height_from_floored(pinFloor));
    } };
    e.data.visit_overload(
        [&](const history::WartTransferData& d) {
            b.actions.wartTransfers.push_back(
                { signed_info_data(d.sign_data()),
                    api::block::WartTransferData { .toAddress = c.addresses.fetch(d.to_id()),
                        .amount = d.wart() } });
        },
        [&](const history::RewardData& d) {
            auto toAddress = c.addresses.fetch(d.to_id());
            b.set_reward({ e.hash, hid, { toAddress, d.wart() } });
        },
        [&](const history::AssetCreationData& d) {
            b.actions.assetCreations.push_back({ signed_info_data(d.sign_data()),
                {
                    .name { d.asset_name() },
                    .supply { d.supply() },
                    .assetId { d.asset_id() },
                } });
        },
        [&](const history::TokenTransferData& d) {
            auto& assetData { c.assetsById[d.token_id().asset_id()] };

            b.actions.tokenTransfers.push_back(
                { signed_info_data(d.sign_data()),
                    { .assetInfo = assetData,
                        .isLiquidity = d.token_id().is_liquidity(),
                        .toAddress = c.addresses.fetch(d.to_id()),
                        .amount = { d.amount() } } });
        },
        [&](const history::OrderData& d) {
            auto& assetData { c.assetsById[d.asset_id()] };
            b.actions.newOrders.push_back({ signed_info_data(d.sign_data()),
                {
                    .assetInfo { assetData },
                    .amount { d.amount() },
                    .limit { d.limit() },
                    .buy = d.buy(),
                } });
        },
        [&](const history::CancelationData& d) {
            b.actions.cancelations.push_back(
                { signed_info_data(d.sign_data()), { d.cancel_txid() } });
        },
        [&](const history::OrderCancelationData& d) {
            auto& asset { c.assetsById[d.asset_id()] };
            b.actions.orderCancelations.push_back({ e.hash,
                hid,
                { .cancelTxid { d.cancel_txid() },
                    .buy = d.buy(),
                    .assetInfo { asset },
                    .historyId { d.order_id() },
                    .remaining { d.amount() } } });
        },
        [&](const history::MatchData& d) {
            auto& asset { c.assetsById[d.asset_id()] };
            b.actions.matches.push_back({ e.hash,
                hid,
                { .assetInfo { asset },
                    .poolBefore { d.pool_before() },
                    .poolAfter { d.pool_after() },
                    .buySwaps {},
                    .sellSwaps {} } });
        },
        [&](const history::LiquidityDeposit& ld) {
            auto& asset { c.assetsById[ld.asset_id()] };
            b.actions.liquidityDeposit.push_back({ signed_info_data(ld.sign_data()),
                { .assetInfo { asset },
                    .baseDeposited { ld.base() },
                    .quoteDeposited { ld.quote() },
                    .sharesReceived { ld.shares() } } });
        },
        [&](const history::LiquidityWithdraw& lw) {
            auto& asset { c.assetsById[lw.asset_id()] };
            b.actions.liquidityWithdrawal.push_back(
                { signed_info_data(lw.sign_data()),
                    {
                        .assetInfo { asset },
                        .sharesRedeemed { lw.shares() },
                        .baseReceived { lw.base() },
                        .quoteReceived { lw.quote() },
                    } });
        });
}
} // namespace

wrt::optional<api::BlockBinary> State::api_get_block_binary(Height h) const
{
    return db.consensus_block_id(h)
        .and_then([&](BlockId id) { return db.get_block_data(id); })
        .transform([](BlockData&& bd) {
            ParseAnnotations annotations;
            auto parsed { std::move(bd).parse_throw(&annotations) };
            return api::BlockBinary { .data { std::move(parsed.body.data) },
                .annotations { std::move(annotations) } };
        });
}

wrt::optional<api::Block> State::api_get_block(Height zh) const
{
    if (zh == 0 || zh > chainlength())
        return {};
    auto h { zh.nonzero_assert() };
    auto pinFloor { h.pin_floor() };
    auto lower = chainstate.history_offset(h);
    auto upper = (h == chainlength() ? HistoryId { 0 } : chainstate.history_offset(h + 1));
    auto header = chainstate.headers()[h];

    auto entries { db.lookup_history_range(lower, upper) };
    api::Block b(header, h, chainlength() - h + 1, {});
    for (auto& e : entries)
        push_history(b, e, dbcache, pinFloor);
    return b;
}

auto State::api_tx_cache() const -> const TransactionIds
{
    return chainstate.txids();
}

api::Transaction State::api_dispatch_mempool(const TxHash& txHash,
    TransactionMessage&& tx) const
{
    auto gen_temporal = []() { return api::TemporalInfo { 0, Height(0), 0 }; };

    auto make_signed_info { [&txHash](auto& tx) {
        return api::block::SignedInfoData(txHash, wrt::nullopt, tx.from_id(),
            tx.from_address(txHash), tx.fee(),
            tx.nonce_id(), tx.pin_height());
    } };

    return std::move(tx).visit_overload(
        [&](WartTransferMessage&& wtm) -> api::Transaction {
            return api::WartTransferTransaction { gen_temporal(),
                { make_signed_info(wtm),
                    {
                        .toAddress = wtm.to_addr(),
                        .amount = wtm.wart(),
                    } } };
        },
        [&](TokenTransferMessage&& ttm) -> api::Transaction {
            // ttm.byte_size
            auto& a { dbcache.assetsByHash.fetch(ttm.asset_hash()) };
            return api::TokenTransferTransaction {
                gen_temporal(),
                { make_signed_info(ttm),
                    {
                        .assetInfo { a },
                        .isLiquidity = ttm.is_liquidity(),
                        .toAddress = ttm.to_addr(),
                        .amount = ttm.amount(),
                    } }
            };
        },
        [&](LimitSwapMessage&& o) -> api::Transaction {
            auto& a { dbcache.assetsByHash.fetch(o.asset_hash()) };
            return api::NewOrderTransaction { gen_temporal(),
                { make_signed_info(o),
                    {
                        .assetInfo { a },
                        .amount { o.amount() },
                        .limit { o.limit() },
                        .buy = o.buy(),
                    } } };
        },
        [&](CancelationMessage&& a) -> api::Transaction {
            return api::CancelationTransaction {
                gen_temporal(),
                { make_signed_info(a), { .cancelTxid { a.cancel_txid() } } }
            };
        },
        [&](LiquidityDepositMessage&& rd) -> api::Transaction {
            auto& a { dbcache.assetsByHash.fetch(rd.asset_hash()) };
            return api::LiquidityDepositTransaction {
                gen_temporal(),
                { make_signed_info(rd),
                    {
                        .assetInfo { a },
                        .baseDeposited { rd.base() },
                        .quoteDeposited { rd.quote() },
                        .sharesReceived { wrt::nullopt },
                    } }
            };
        },
        [&](LiquidityWithdrawalMessage&& rm) -> api::Transaction {
            auto& a { dbcache.assetsByHash.fetch(rm.asset_hash()) };
            return api::LiquidityWithdrawalTransaction {
                gen_temporal(),
                { make_signed_info(rm),
                    {
                        .assetInfo { a },
                        .sharesRedeemed { rm.amount() },
                        .baseReceived { wrt::nullopt },
                        .quoteReceived { wrt::nullopt },
                    } }
            };
        },
        [&](AssetCreationMessage&& rm) -> api::Transaction {
            return api::AssetCreationTransaction { gen_temporal(),
                { make_signed_info(rm),
                    {
                        .name { rm.asset_name() },
                        .supply { rm.supply() },
                        .assetId { wrt::nullopt },
                    } } };
        });
}

api::Transaction State::api_dispatch_history(const TxHash& txHash,
    HistoryId hid,
    history::HistoryVariant&& tx,
    NonzeroHeight h) const
{
    auto gen_temporal = [&]() {
        return api::TemporalInfo { (chainlength() + 1) - h, h,
            get_headers()[h].timestamp() };
    };
    auto fetch_addr { [&](AccountId aid) { return dbcache.addresses.fetch(aid); } };
    auto make_signed_info { [&](auto& tx) {
        return api::block::SignedInfoData(
            txHash, hid, tx.sign_data().origin_account_id(),
            fetch_addr(tx.sign_data().origin_account_id()), tx.sign_data().fee(),
            tx.sign_data().pin_nonce().id,
            tx.sign_data().pin_nonce().pin_height_from_floored(h.pin_floor()));
    } };
    return std::move(tx).visit_overload(
        [&](history::WartTransferData&& wtm) -> api::Transaction {
            // wtm.
            return api::WartTransferTransaction {
                gen_temporal(),
                { make_signed_info(wtm),
                    {
                        .toAddress = fetch_addr(wtm.to_id()),
                        .amount = wtm.wart(),
                    } }
            };
        },
        [&](history::TokenTransferData&& ttm) -> api::Transaction {
            auto& a { dbcache.assetsById[ttm.token_id().asset_id()] };
            return api::TokenTransferTransaction {
                gen_temporal(),
                { make_signed_info(ttm),
                    {
                        .assetInfo { a },
                        .isLiquidity = ttm.token_id().is_liquidity(),
                        .toAddress = fetch_addr(ttm.to_id()),
                        .amount = ttm.amount(),
                    } }
            };
        },
        [&](history::OrderData&& o) -> api::Transaction {
            auto& a { dbcache.assetsById[o.asset_id()] };
            return api::NewOrderTransaction { gen_temporal(),
                { make_signed_info(o),
                    {
                        .assetInfo { a },
                        .amount { o.amount() },
                        .limit { o.limit() },
                        .buy = o.buy(),
                    } } };
        },
        [&](history::CancelationData&& c) -> api::Transaction {
            return api::CancelationTransaction {
                gen_temporal(),
                { make_signed_info(c), { .cancelTxid { c.cancel_txid() } } }
            };
        },
        [&](history::OrderCancelationData&& c) -> api::Transaction {
            auto& a { dbcache.assetsById[c.asset_id()] };
            return api::OrderCancelationTransaction {
                gen_temporal(),
                { txHash, hid,
                    api::block::OrderCancelationData { .cancelTxid { c.cancel_txid() },
                        .buy = c.buy(),
                        .assetInfo { a },
                        .historyId { c.order_id() },
                        .remaining { c.amount() } } }
            };
        },
        [&](history::LiquidityDeposit&& ld) -> api::Transaction {
            auto& a { dbcache.assetsById[ld.asset_id()] };
            return api::LiquidityDepositTransaction {
                gen_temporal(),
                { make_signed_info(ld),
                    { .assetInfo { a },
                        .baseDeposited { ld.base() },
                        .quoteDeposited { ld.quote() },
                        .sharesReceived { ld.shares() } } }
            };
        },
        [&](history::LiquidityWithdraw&& lw) -> api::Transaction {
            auto& a { dbcache.assetsById[lw.asset_id()] };
            return api::LiquidityWithdrawalTransaction {
                gen_temporal(),
                { make_signed_info(lw),
                    { .assetInfo { a },
                        .sharesRedeemed { lw.shares() },
                        .baseReceived { lw.base() },
                        .quoteReceived { lw.quote() } } }
            };
        },
        [&](history::RewardData&& rm) -> api::Transaction {
            return api::RewardTransaction {
                gen_temporal(),
                { txHash,
                    hid,
                    { .toAddress { fetch_addr(rm.to_id()) }, .wart { rm.wart() } } }
            };
        },
        [&](history::AssetCreationData&& rm) -> api::Transaction {
            return api::AssetCreationTransaction { gen_temporal(),
                { make_signed_info(rm),
                    {
                        .name { rm.asset_name() },
                        .supply { rm.supply() },
                        .assetId { rm.asset_id() },
                    } } };
        },
        [&](history::MatchData&& rm) -> api::Transaction {
            auto& a { dbcache.assetsById[rm.asset_id()] };
            return api::MatchTransaction { gen_temporal(),
                { txHash,
                    hid,
                    {
                        .assetInfo { a },
                        .poolBefore { rm.pool_before() },
                        .poolAfter { rm.pool_after() },
                        .buySwaps { rm.buy_swaps() },
                        .sellSwaps { rm.sell_swaps() },
                    } } };
            // AssetIdEl, PoolBeforeEl, PoolAfterEl, BuySwapsEl, SellSwapsEl
        }

    );
}

wrt::optional<api::Transaction> State::api_get_tx(const TxHash& txHash) const
{
    if (auto p { chainstate.mempool()[txHash] }; p)
        return api_dispatch_mempool(txHash, std::move(*p));
    if (auto p { db.lookup_history(txHash) }; p) {
        auto& [parsed, historyId] = *p;
        NonzeroHeight h { chainstate.history_height(historyId) };
        return api_dispatch_history(txHash, historyId, std::move(parsed), h);
    }
    return {};
}

auto State::api_get_transaction_minfee() -> api::TransactionMinfee
{
    return { chainstate.mempool().min_fee() };
}
auto State::api_get_latest_txs(size_t N) const -> api::TransactionsByBlocks
{
    HistoryId upper { db.next_history_id() };
    // note: history ids start with 1
    HistoryId lower { (upper.value() > N + 1) ? db.next_history_id() - N
                                              : HistoryId { 1 } };
    return api_get_transaction_range(lower, upper);
}

auto State::api_get_latest_blocks(size_t N) const -> api::TransactionsByBlocks
{
    HistoryId upper { db.next_history_id() };
    auto l { chainlength().value() };
    auto hLower { l > N ? Height(l + 1 - N).nonzero_assert()
                        : Height { 1 }.nonzero_assert() };
    HistoryId lower { chainstate.history_offset(hLower) };
    return api_get_transaction_range(lower, upper);
}

auto State::api_get_miner(NonzeroHeight h) const
    -> wrt::optional<api::AddressWithId>
{
    if (chainlength() < h)
        return {};
    auto offset { chainstate.history_offset(h) };
    auto parsed { db.fetch_history(offset).data };

    assert(std::holds_alternative<history::RewardData>(parsed));
    auto minerId { std::get<history::RewardData>(parsed).to_id() };
    return api::AddressWithId { db.fetch_address(minerId), minerId };
}

auto State::api_get_miners(HeightRange hr) const
    -> std::vector<api::AddressWithId>
{
    std::vector<api::AddressWithId> res;
    for (auto h : hr) {
        auto o { api_get_miner(h) };
        assert(o);
        res.push_back(*o);
    }
    return res;
}

auto State::api_get_latest_miners(uint32_t N) const
    -> std::vector<api::AddressWithId>
{
    return api_get_miners(chainlength().latest(N));
}

auto State::api_get_transaction_range(HistoryId lower, HistoryId upper) const
    -> api::TransactionsByBlocks
{
    api::TransactionsByBlocks res { .fromId { lower }, .blocks_reversed {} };
    if (upper.value() == 0)
        return res;
    auto lookup { db.lookup_history_range(lower, upper) };
    assert(lookup.size() == upper - lower);
    if (chainlength() != 0) {
        chainserver::DBCache cache(db);
        auto update_tmp = [&](HistoryId id) {
            auto h { chainstate.history_height(id) };
            auto pinFloor { h.pin_floor() };
            auto header { chainstate.headers()[h] };
            auto b { api::Block(header, h, chainlength() - h + 1, {}) };
            auto beginId { chainstate.history_offset(h) };
            return std::tuple { pinFloor, beginId, b };
        };
        auto tmp { update_tmp(upper - 1) };

        for (size_t i = 0; i < lookup.size(); ++i) {
            auto& [pinFloor, beginId, block] { tmp };
            auto id { upper - 1 - i };
            if (id < beginId) { // start new tmp block
                res.blocks_reversed.push_back(block);
                tmp = update_tmp(id);
            }

            push_history(block, lookup[lookup.size() - 1 - i], cache, pinFloor);
        }
        res.count = lookup.size();
        res.blocks_reversed.push_back(std::get<2>(tmp));
    }
    return res;
}

void State::garbage_collect()
{
    // garbage collect old unused blocks
    using namespace std::chrono;
    if (auto n = steady_clock::now(); n > nextGarbageCollect) {
        nextGarbageCollect = n + minutes(5);
        auto tr = db.transaction();
        blockCache.garbage_collect(db);
        tr.commit();
    }
}

Batch State::get_headers_concurrent(BatchSelector s) const
{
    std::lock_guard l(chainstateMutex);
    if (s.descriptor == chainstate.descriptor()) {
        return chainstate.headers().get_headers(s.header_range());
    } else {
        return blockCache.get_batch_concurrent(s);
    }
}

wrt::optional<HeaderView> State::get_header_concurrent(Descriptor descriptor,
    Height height) const
{
    std::lock_guard l(chainstateMutex);
    if (descriptor == chainstate.descriptor()) {
        return chainstate.headers().get_header(height);
    } else {
        return blockCache.get_header_concurrent(descriptor, height);
    }
}

ConsensusSlave State::get_chainstate_concurrent()
{
    std::lock_guard l(chainstateMutex);
    return { signedSnapshot, chainstate.descriptor(), chainstate.headers() };
}

Result<ChainMiningTask> State::mining_task(const Address& a)
{
    return mining_task(a, config().node.disableTxsMining);
}

Result<ChainMiningTask> State::mining_task(const Address& miner,
    bool disableTxs)
{
    auto md = chainstate.mining_data();

    NonzeroHeight height { next_height() };
    BlockVersion v { BlockVersion::hardcoded_for_params(height) };
    if (height.value() < NEWBLOCKSTRUCUTREHEIGHT && !is_testnet())
        return Error(ENOTSYNCED);

    auto make_body { [&]() -> Body {
        std::vector<TransactionMessage> transactions;
        if (!disableTxs)
            transactions = chainstate.mempool().get_transactions(400, height);

        auto minerReward { height.reward() };

        using namespace block;
        AccountId nextAccountId { db.next_id() };

        std::vector<Address> newAddresses;
        auto addr_id { [&, map = std::map<Address, AccountId> {}](
                           const Address& address) mutable -> AccountId {
            auto a { db.lookup_account(address) };
            if (a)
                return a.value();
            auto [iter, inserted] { map.try_emplace(address, nextAccountId) };
            if (inserted) {
                nextAccountId++;
                newAddresses.push_back(address);
            }
            return iter->second;
        } };
        const AccountId minerAccId { addr_id(miner) };
        body::Entries entries;
        auto asset { [&, assetOffsets = std::map<AssetHash, size_t> {}](
                         AssetHash hash) mutable -> auto& {
            auto [it, inserted] = assetOffsets.try_emplace(hash, entries.tokens().size());
            if (inserted)
                entries.tokens().push_back({ dbcache.assetsByHash.fetch(hash).id });
            return entries.tokens()[it->second];
        } };

        auto dispatch_message { [&]<typename... Handlers>(TransactionMessage&& m,
                                    Handlers&&... handlers) {
            wrt::Overload o { std::forward<Handlers>(handlers)... };
            std::move(m).visit([&]<typename Message>(Message&& message) {
                if (!message.allows_blockversion(v))
                    return;
                o(std::forward<Message>(message));
            });
        } };
        for (auto& tx : transactions) {
            minerReward.add_assert(tx.fee()); // assert because
            dispatch_message(
                std::move(tx),
                [&](WartTransferMessage&& m) {
                    entries.wart_transfers().push_back(
                        { m.from_id(), m.pin_nonce_throw(height), m.compact_fee(),
                            addr_id(m.to_addr()), m.wart(), m.signature() },
                        v);
                },
                [&](TokenTransferMessage&& m) {
                    auto pn {
                        PinNonce::make_pin_nonce(m.nonce_id(), height, m.pin_height())
                    };
                    if (!pn)
                        throw std::runtime_error("Cannot make pin_nonce");
                    auto& s { asset(m.asset_hash()) };
                    if (m.is_liquidity()) {
                        auto& transfers = s.liquidity_transfers();
                        transfers.push_back({ m.from_id(), m.pin_nonce_throw(height),
                                                m.compact_fee(), addr_id(m.to_addr()),
                                                m.amount(), m.signature() },
                            v);
                    } else {
                        auto& transfers = s.asset_transfers();
                        transfers.push_back({ m.from_id(), m.pin_nonce_throw(height),
                                                m.compact_fee(), addr_id(m.to_addr()),
                                                m.amount(), m.signature() },
                            v);
                    }
                },
                [&](LimitSwapMessage&& m) {
                    asset(m.asset_hash())
                        .orders()
                        .push_back({ m.from_id(), m.pin_nonce_throw(height),
                                       m.compact_fee(), m.buy(), m.amount(), m.limit(),
                                       m.signature() },
                            v);
                },
                [&](CancelationMessage&& m) {
                    entries.cancelations().push_back(
                        { m.from_id(), m.pin_nonce_throw(height), m.compact_fee(),
                            m.cancel_height(), m.cancel_nonceid(), m.signature() },
                        v);
                },
                [&](LiquidityDepositMessage&& m) {
                    asset(m.asset_hash())
                        .liquidity_deposits()
                        .push_back({ m.from_id(), m.pin_nonce_throw(height),
                                       m.compact_fee(), m.base(), m.quote(),
                                       m.signature() },
                            v);
                },
                [&](LiquidityWithdrawalMessage&& m) {
                    asset(m.asset_hash())
                        .liquidity_withdrawals()
                        .push_back({ m.from_id(), m.pin_nonce_throw(height),
                            m.compact_fee(), m.amount(), m.signature() },v);
                },
                [&](AssetCreationMessage&& m) {
                    entries.asset_creations().push_back(
                        { m.from_id(), m.pin_nonce_throw(height), m.compact_fee(),
                            AssetSupplyEl(m.supply()), m.asset_name(), m.signature() },v);
                });
        }
        return Body::serialize({ std::move(newAddresses),
            { minerAccId, minerReward },
            std::move(entries) });
    } };

    const auto b { [&]() -> const auto& {
        _miningCache.update_validity(mining_cache_validity());
        if (auto* p { _miningCache.lookup(miner, disableTxs) }; p != nullptr) {
            return *p;
        } else {
            auto body { make_body() };
            return _miningCache.insert(miner, disableTxs, std::move(body));
        }
    }() };

    try {
        HeaderGenerator hg(md.prevhash, b, md.target, md.timestamp, height);
        return ChainMiningTask { .block { height, hg.make_header(0), std::move(b) } };
    } catch (const Error& e) {
        spdlog::warn("Cannot create mining task: {}", e.strerror());
        return Error(EBUG);
    }
}

stage_operation::StageSetStatus State::set_stage(Headerchain&& hc)
{
    if (signedSnapshot && !signedSnapshot->compatible(hc)) {
        return {};
    }

    auto l { hc.length() };
    auto t = db.transaction();
    NonzeroHeight fh1 { fork_height(chainstate.headers(), hc) };
    NonzeroHeight fh2 { fork_height(stage, hc) };
    wrt::optional<NonzeroHeight> newProtectBegin;
    if (fh1 >= fh2) {
        newProtectBegin = fh1;
        if (fh1 > fh2)
            assert(fork_height(stage, chainstate.headers()) == fh2);
        spdlog::debug("Drop all stage", fh1.value(), fh2.value() - 1);
        auto dk { db.schedule_protected_all() };
        blockCache.schedule_discard(dk);
    } else {
        newProtectBegin = fh2;
        spdlog::debug("Blocks already in stage: [{},{}], drop from {}", fh1.value(),
            fh2.value() - 1, fh2.value());
        auto dk { db.schedule_protected_part(stage, fh2) };
        blockCache.schedule_discard(dk);
    }

    stage = std::move(hc);
    const Height bound = stage.length();
    NonzeroHeight h(newProtectBegin->value());
    for (; h <= bound; ++h) {
        Hash hash = stage.hash_at(h);
        auto id { db.lookup_block_id(hash) };
        if (!id)
            break;
        db.protect_stage_assert_scheduled(*id);
    }
    t.commit();

    stage.shrink(h - 1);
    if (h > newProtectBegin) {
        spdlog::debug("MISSING: [{},{}), protected downloaded blocks [{},{}]",
            h.value(), l.value(), newProtectBegin->value(),
            (h - 1).value());
    } else {
        spdlog::debug("MISSING: [{},{}), protected no blocks", h.value(),
            l.value());
    }
    return { h };
}

auto State::add_stage(const std::vector<Block>& blocks, const Headerchain& hc)
    -> StageActionResult
{
    if (signedSnapshot && !signedSnapshot->compatible(stage)) {
        return { { { ELEADERMISMATCH, signedSnapshot->height() } }, {}, {} };
    }

    assert(blocks.size() > 0);
    ChainError ce { Error(0), blocks.back().height + 1 };
    auto transaction = db.transaction();

    assert(hc.length() >= stage.length());
    assert(hc.hash_at(stage.length()) == stage.hash_at(stage.length()));
    for (auto& b : blocks) {
        assert(hc.length() >= b.height);
        assert(hc[b.height] == b.header);
        assert(b.height == stage.length() + 1);

        auto prepared { stage.prepare_append(signedSnapshot, b.header) };
        if (!prepared.has_value()) {
            ce = { prepared.error(), b.height };
            break;
        }
        db.insert_protect(b);
        stage.append(prepared.value(), batchRegistry);
    }
    if (stage.total_work() > chainstate.headers().total_work()) {
        auto r { apply_stage(std::move(transaction)) };

        if (r.status.ce.is_error()) {
            assert(r.errorWorksum.has_value());
            // Something went wrong on block body level so block header must be also
            // tainted as we checked for correct merkleroot already
            // => we need to collect data on rogue header
            RogueHeaderData rogueHeaderData(r.status.ce, r.errorHeader.value(),
                r.errorWorksum.value());
            return { { r.status }, std::move(rogueHeaderData), std::move(r.update) };
        } else {
            // pass {} as header arg because we can't to block any headers when
            // we have a wrong body (EINV_BODY or EMROOT)
            return { { ce }, {}, std::move(r.update) };
        }
    } else {
        // pass {} as header arg because we can't to block any headers when
        // we have a wrong body (EINV_BODY or EMROOT)
        transaction.commit();
        dbcache.clear();
        return { { ce }, {}, {} };
    }
}
namespace {
class RollbackSession {
public:
    const ChainDB& db;
    const PinFloor newPinFloor;
    const HistoryId oldHistoryIdStart;
    const StateId64 oldStateId64Start;
    struct DeletePool { };
    using UpdatePool = chain_db::PoolData;
    using PoolAction = wrt::variant<DeletePool, UpdatePool>;

    struct OrderAction {
        wrt::optional<rollback::OrderFillstate> fillstate;
        wrt::optional<chain_db::OrderData> create;
    };

    std::vector<TransactionMessage> toMempool;
    FreeBalanceUpdates freeBalanceUpdates;

    // actions to be run against database
    std::map<BalanceId, Balance_uint64> balanceUpdates;
    std::map<HistoryId, OrderAction> orderActions;
    std::map<AssetId, PoolAction> poolActions;

private:
    RollbackSession(const ChainDB& db, NonzeroHeight beginHeight,
        HistoryId oldHistoryIdStart, const rollback::Data& rb)
        : db(db)
        , newPinFloor(beginHeight.pin_floor())
        , oldHistoryIdStart(oldHistoryIdStart)
        , oldStateId64Start(rb.next_state_id64())
    {
    }
    // returns true if the id passed was not counted up to
    // at the block height that we roll back to.
    template <typename T>
    bool is_deprecated_id(T t)
    {
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, HistoryId>) {
            return t >= oldHistoryIdStart;
        } else if constexpr (StateId64::is_id_t<std::remove_cvref_t<T>>()) {
            return StateId64::from_id(t) >= oldStateId64Start;
        } else {
            static_assert(false, "is_deprecated only takes state ids");
        }
    }

    static BlockUndoData fetch_undo(const ChainDB& db, BlockId id)
    {
        auto u = db.get_block_undo(id);
        if (!u)
            throw std::runtime_error("Database corrupted (could not load block)");
        return *u;
    }

public:
    RollbackSession(const ChainDB& db, NonzeroHeight beginHeight,
        HistoryId oldHistoryIdStart, BlockId firstId)
        : RollbackSession(db, beginHeight, oldHistoryIdStart,
              rollback::Data(fetch_undo(db, firstId).rawUndo))
    {
    }

private:
    void put_txs_into_mempool(const block::ParsedBody& body, NonzeroHeight height,
        DBCache& c)
    {
        auto pinFloor { height.pin_floor() };
        auto apply_to_array { [&](auto&& arr, auto&&... lambdas) {
            auto bindPinheight { [&](auto&& lambda2) {
                // if we have a lambda with two arguments (pinHeight, tx), then create a
                // lambda that only accepts tx and only calls it if the pinHeight is not
                // too new (if it is too new, after rollback the tx cannot exist)
                if constexpr (std::is_same_v<
                                  typename function_traits<
                                      decltype(lambda2)>::template arg<0>::type,
                                  PinHeight>) {
                    using ret_t = typename function_traits<
                        decltype(lambda2)>::template arg<1>::type;
                    return [&](const std::remove_cvref_t<ret_t>& tx) {
                        PinHeight pinHeight = tx.pin_nonce().pin_height_from_floored(pinFloor);
                        if (pinHeight <= newPinFloor)
                            lambda2(pinHeight, tx);
                    };
                } else {
                    return lambda2;
                }
            } };
            arr.visit_components_overload(
                bindPinheight(std::forward<decltype(lambdas)>(lambdas))...);
        } };

        using namespace block::body;
        apply_to_array(
            body,
            // Wart transfer lambda
            [&](PinHeight pinHeight, const WartTransfer& t) {
                auto toAddress { c.addresses.fetch(t.to_id()) };
                // t.wart() cannot be of type NonzeroWart because we had some zero
                // amounts sent in the early days, so it is possible to have zero WART
                // amount in the blocks
                if (!t.wart().is_zero())
                    toMempool.push_back(WartTransferMessage(
                        t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                        toAddress, t.wart().nonzero_assert(), t.signature()));
            },

            // Cancelation lambda
            [&](PinHeight pinHeight, const Cancelation& t) {
                toMempool.push_back(CancelationMessage(
                    t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                    t.cancel_height(), t.cancel_nonceid(), t.signature()));
            },

            // Token section lambda only has one argument
            [&](const TokenSection& s) {
                auto asset { c.assetsById[s.asset_id()] };
                apply_to_array(
                    s,
                    [&](PinHeight pinHeight, const AssetTransfer& t) {
                        auto toAddress { c.addresses.fetch(t.to_id()) };
                        toMempool.push_back(TokenTransferMessage(
                            t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                            asset.hash, false, toAddress, t.amount(), t.signature()));
                    },
                    [&](PinHeight pinHeight, const LiquidityTransfer& t) {
                        auto toAddress { c.addresses.fetch(t.to_id()) };
                        toMempool.push_back(TokenTransferMessage(
                            t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                            asset.hash, true, toAddress, t.shares(), t.signature()));
                    },
                    [&](PinHeight pinHeight, const Order& t) {
                        toMempool.push_back(LimitSwapMessage(
                            t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                            asset.hash, t.buy(), t.amount(), t.limit(), t.signature()));
                    },
                    [&](PinHeight pinHeight, const LiquidityDeposit& t) {
                        toMempool.push_back(LiquidityDepositMessage(
                            t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                            asset.hash, t.base(), t.quote(), t.signature()));
                    },
                    [&](PinHeight pinHeight, const LiquidityWithdrawal& t) {
                        toMempool.push_back(LiquidityWithdrawalMessage(
                            t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                            asset.hash, t.amount(), t.signature()));
                    });
            },
            // asset creation lambda
            [&](PinHeight pinHeight, const AssetCreation& t) {
                toMempool.push_back(AssetCreationMessage(
                    t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                    t.supply(), t.asset_name(), t.signature()));
            });
    }

public:
    void rollback_block_inc_order(BlockId id, NonzeroHeight height, DBCache& c)
    {
        try {
            BlockUndoData d { fetch_undo(db, id) };

            // use block data to fill the mempool again
            auto body { std::move(d.body).parse_throw(height, d.header.version()) };
            put_txs_into_mempool(body, height, c);

            // roll back state modifications
            rollback::Data rbv(d.rawUndo);
            rbv.foreach_changed_balance([&](const IdBalance& entry) {
                if (is_deprecated_id(entry.id))
                    return;
                const auto& bal { entry.balance };
                const BalanceId& id { entry.id };
                auto b { db.get_token_balance(id) };
                if (!b.has_value())
                    throw std::runtime_error("Database corrupted, cannot roll back");

                freeBalanceUpdates.insert_or_assign(
                    AccountToken { b->accountId, b->tokenId }, bal.free_assert());
                balanceUpdates.try_emplace(id, bal);
            });
            rbv.foreach_deleted_order([&](const rollback::OrderData& o) {
                if (is_deprecated_id(o.id))
                    return;
                // restore the order
                auto& create { orderActions.try_emplace(o.id).first->second.create };
                assert(!create.has_value()); // order can only be deleted once
                create = o;
            });
            rbv.foreach_changed_order([&](const rollback::OrderFillstate& o) {
                if (is_deprecated_id(o.id))
                    return;
                auto& action { orderActions.try_emplace(o.id).first->second };

                // we run through blocks in order and in each block either the order was
                // deleted or updated. If it was updated, then it cannot have been
                // deleted before, so there cannot be a create entry
                assert(!action.create.has_value());

                // assign the fillstate
                action.fillstate = o;
            });
            rbv.foreach_changed_poolstate([&](const rollback::Poolstate& s) {
                if (is_deprecated_id(s.id))
                    return;
                poolActions.try_emplace(
                    s.id,
                    UpdatePool { s.id, defi::Pool_uint64 { s.base, s.quote, s.shares } });
            });
            rbv.foreach_newly_created_pool([&](const AssetId& id) {
                if (is_deprecated_id(id))
                    return;
                poolActions.try_emplace(id, DeletePool {});
            });
        } catch (const Error& e) {
            throw std::runtime_error("Cannot rollback block at height" + std::to_string(height) + ":" + e.err_name());
        }
    }
};
} // namespace

RollbackResult State::rollback(const Height newlength) const
{
    assert(newlength < chainlength());
    const Height oldlength { chainlength() };
    auto n { chainlength() - newlength };
    spdlog::info("Rolling back {} blocks to height {}", n, newlength.value());
    const NonzeroHeight beginHeight = newlength.add1();
    auto endHeight(chainlength().add1());

    // load ids
    auto ids { db.consensus_block_ids({ beginHeight, endHeight }) };
    assert(ids.size() == endHeight - beginHeight);
    assert(ids.size() > 0);

    auto historyOffset { chainstate.history_offset(beginHeight) };
    RollbackSession rs(db, beginHeight, historyOffset, ids[0]);

    for (size_t i = 0; i < ids.size(); ++i) {
        NonzeroHeight height = beginHeight + i;
        // note that the blocks are rolled back in increasing height order,
        // the rollback data supports this rollback style and it is more efficient
        // because for example only the earliest occurrence of a balance update is
        // the final rolled back balance when considering the blocks in increasing
        // order.
        rs.rollback_block_inc_order(ids[i], height, dbcache);
    }

    db.delete_history_from(newlength.add1());
    // db.delete_state32_from(rs.oldStateId32Start);
    db.delete_state64_from(rs.oldStateId64Start);
    auto dk { db.delete_consensus_from((newlength + 1).nonzero_assert()) };

    // write balances to db
    for (auto& p : rs.balanceUpdates)
        db.set_balance(p.first, p.second);
    for (auto& [id, a] : rs.orderActions) {
        if (a.create) {
            auto orderData { *a.create };
            if (a.fillstate) {
                orderData.filled = a.fillstate->filled;
                assert(orderData.id == a.fillstate->id);
            }
            db.insert(orderData);
        } else {
            // every element that was inserted into orderActions
            // has a value for at least one of the optional members.
            assert(a.fillstate.has_value());
            db.update_order_fillstate(*a.fillstate);
        }
    }
    for (auto& [id, a] : rs.poolActions) {
        a.visit_overload(
            [&](RollbackSession::DeletePool&) { db.delete_pool(id); },
            [&](RollbackSession::UpdatePool& p) { db.update_pool(p); });
    }

    return chainserver::RollbackResult {
        .shrink { newlength, oldlength - newlength },
        .toMempool { std::move(rs.toMempool) },
        .freeBalanceUpdates { std::move(rs.freeBalanceUpdates) },
        .chainTxIds { db.fetch_tx_ids(newlength) },
        .deletionKey { dk }
    };
}

auto State::apply_stage(ChainDBTransaction&& t) -> ApplyStageResult
{
    dbCacheValidity += 1;
    assert(!signedSnapshot || signedSnapshot->compatible(stage));
    assert(stage.total_work() > chainstate.headers().total_work());
    const NonzeroHeight fh {
        fork_height(chainstate.headers(), stage)
    }; // first different height

    chainserver::ApplyStageTransaction tr { *this, std::move(t) };
    tr.consider_rollback(fh - 1);
    wrt::optional<Worksum> errorWorksum;
    wrt::optional<Header> errorHeader;
    auto status { tr.apply_stage_blocks() };
    if (status.is_error()) {
        if (config().localDebug) {
            assert(0 == 1); // In local debug mode no errors should occurr (no bad actors)
        }
        for (auto h { status.height() }; h < stage.length(); ++h)
            db.delete_bad_block(stage.hash_at(h));
        assert(status.height() < stage.length());
        errorWorksum = stage.total_work_at(status.height());
        errorHeader = stage[status.height()];
        spdlog::warn("Invalid block at height {}: {}", status.height().value(),
            status.err_name());
        stage.shrink(status.height() - 1);
        if (stage.total_work_at(status.height() - 1) <= chainstate.headers().total_work()) {
            return {
                { status },
                errorWorksum,
                errorHeader,
                {},
            };
        }
    }
    db.set_consensus_work(stage.total_work());
    auto update { std::move(tr).commit(*this) };
    dbcache.clear();

    return { { status }, errorWorksum, errorHeader, update };
}

auto State::apply_signed_snapshot(SignedSnapshot&& ssnew)
    -> wrt::optional<StateUpdateWithAPIBlocks>
{
    if (signedSnapshot >= ssnew) {
        return {};
    }
    dbCacheValidity += 1;
    syncdebug_log().info("SetSignedPin {} new", ssnew.height().value());
    signedSnapshot = std::move(ssnew);

    using namespace state_update;

    // consider chainstate
    state_update::StateUpdateWithAPIBlocks res {
        .update {
            .chainstateUpdate = state_update::SignedSnapshotApply {
                .rollback {}, .signedSnapshot { *signedSnapshot } },
            .mempoolUpdates {},
        },
        .appendedBlocks {}
    };
    auto db_t { db.transaction() };
    if (!signedSnapshot->compatible(chainstate.headers())) {
        assert(signedSnapshot->height() <= chainlength());
        auto rb { rollback(signedSnapshot->height() - 1) };

        std::lock_guard l(chainstateMutex);
        auto headers_ptr { blockCache.add_old_chain(chainstate, rb.deletionKey) };

        res.update.chainstateUpdate = state_update::SignedSnapshotApply {
            .rollback { state_update::SignedSnapshotApply::Rollback {
                .deltaHeaders { chainstate.rollback(rb) },
                .prevHeaders { std::move(headers_ptr) },
            } },
            .signedSnapshot { *signedSnapshot }
        };
        res.update.mempoolUpdates = chainstate.pop_mempool_updates();
    } else {
        assert(chainstate.pop_mempool_updates().size() == 0);
    };

    db.set_consensus_work(chainstate.headers().total_work());
    db.set_signed_snapshot(*signedSnapshot);
    db_t.commit();
    dbcache.clear();

    return res;
}

auto State::append_mined_block(const Block& b, bool verifyPOW)
    -> StateUpdateWithAPIBlocks
{
    auto nextHeight { next_height() };
    if (nextHeight != b.height)
        throw Error(EMINEDDEPRECATED);
    auto prepared { chainstate.prepare_append(signedSnapshot, b.header, verifyPOW) };
    if (!prepared.has_value())
        throw Error(prepared.error());
    if (chainlength() + 1 != b.height)
        throw Error(EBADHEIGHT);

    const auto nextStateId { db.next_id64() };
    const auto nextHistoryId { db.next_history_id() };

    // do db transaction for new block
    auto transaction = db.transaction();

    auto [blockId, inserted] { db.insert_protect(b) };
    if (!inserted) {
        spdlog::error("Mined block is already in database. This is a bug.");
        throw Error(EMINEDDEPRECATED);
    }

    chainserver::BlockApplier e { db, chainstate.headers(), chainstate.txids() };
    auto apiBlock { e.apply_block(b, prepared->hash, blockId) };
    db.set_consensus_work(chainstate.work_with_new_block());
    transaction.commit();
    dbcache.clear();

    std::unique_lock ul(chainstateMutex);
    auto headerchainAppend = chainstate.append(Chainstate::AppendSingle {
        .freeBalanceUpdates { e.move_free_balance_updates() },
        .signedSnapshot { signedSnapshot },
        .prepared { prepared.value() },
        .newTxIds { e.move_new_txids() },
        .newHistoryOffset { nextHistoryId },
        .newStateOffset { nextStateId } });
    ul.unlock();

    dbCacheValidity += 1;
    return { .update {
                 .chainstateUpdate { state_update::Append {
                     headerchainAppend, try_sign_locked_chainstate() } },
                 .mempoolUpdates { chainstate.pop_mempool_updates() },
             },
        .appendedBlocks { std::move(apiBlock) } };
}

std::pair<mempool::Updates, TxHash>
State::append_gentx(const TransactionCreate& m)
{
    try {
        auto txhash { chainstate.create_tx(m) };
        auto log { chainstate.pop_mempool_updates() };
        spdlog::info("Added new \"{}\" transaction to mempool", m.tag());
        return { std::move(log), std::move(txhash) };
    } catch (const Error& e) {
        spdlog::warn("Rejected new \"{}\" transaction: {}", m.tag(), e.strerror());
        throw;
    }
}

api::WartBalance State::api_get_wart_balance(api::AccountIdOrAddress a) const
{
    api::WartBalance res;
    auto b { api_get_token_balance_recursive(a, TokenId::WART) };
    if (b.lookup) {
        res.balance = Wart::from_funds_throw(b.total.funds);
        res.address = b.lookup->address;
    }
    return res;
}

auto State::normalize(api::TokenIdOrSpec token) const
    -> wrt::optional<api::NormalizedToken>
{
    return token.visit_overload(
        [&](TokenId id) -> wrt::optional<api::NormalizedToken> {
            if (auto nwId { id.non_wart() }) {
                auto asset { db.lookup_asset(nwId->asset_id()) };
                if (asset)
                    return api::NormalizedToken {
                        .id { id },
                        .spec { asset->hash, nwId->is_liquidity() },
                        .name { asset->name.to_string() },
                        .assetPrecision { asset->precision },
                    };
                return {}; // asset corresponding to token id does not exist
            } else {
                return api::NormalizedToken::WART();
            }
        },
        [&](const api::TokenSpec& h) -> wrt::optional<NormalizedToken> {
            auto asset { db.lookup_asset(h.assetHash) };
            if (asset) {
                auto tid { asset->id.token_id(h.isLiquidity) };
                assert(tid.is_liquidity() == h.isLiquidity);
                return api::NormalizedToken {
                    .id { tid },
                    .spec { asset->hash, h.isLiquidity },
                    .name { asset->name.to_string() },
                    .assetPrecision { asset->precision },
                };
            } else if (h.assetHash.is_wart()) {
                return api::NormalizedToken::WART();
            }
            return {};
        });
}

size_t State::on_mempool_constraint_update()
{
    return chainstate.on_mempool_constraint_update();
}
wrt::optional<AccountId> State::normalize(api::AccountIdOrAddress a) const
{
    return a.map_alternative(
        [&](const Address& a) { return db.lookup_account(a); });
}

api::TokenBalance
State::api_get_token_balance_recursive(api::AccountIdOrAddress account,
    api::TokenIdOrSpec spec) const
{
    auto accountId { normalize(account) };
    auto token { normalize(spec) };
    if (!accountId || !token)
        return api::TokenBalance::notfound();
    return api_get_token_balance_recursive(*accountId, token->id);
}

api::TokenBalance State::api_get_token_balance_recursive(AccountId aid,
    TokenId tid) const
{
    if (auto addr { db.lookup_address(aid) }) {
        api::AssetLookupTrace trace;
        auto b { db.get_token_balance_recursive(aid, tid, &trace) };

        wrt::optional<TokenPrecision> prec;
        if (auto nw { tid.non_wart() }; nw && !nw->is_liquidity()) {
            if (!trace.fails.empty()) {
                prec = trace.fails.front().precision;
            } else {
                prec = db.lookup_asset(nw->asset_id())->precision;
            }
        } else { // means that token id is that of WART or pool liquidity (has WART
                 // precision by definition)
            prec = Wart::precision;
        }
        if (!prec)
            return api::TokenBalance::notfound();
        return api::TokenBalance::found(*addr, aid, std::move(trace),
            FundsDecimal(b.balance.total, *prec),
            FundsDecimal(b.balance.locked, *prec));
    }
    return api::TokenBalance::notfound();
}

// wrt::optional<AssetDetail> State::db_lookup_token(const api::AssetIdOrHash&
// token) const
// {
//     return token.visit([&](const auto& token) { return
//     db.lookup_asset(token); });
// }

auto State::insert_txs(const TxVec& txs)
    -> std::pair<std::vector<Error>, mempool::Updates>
{
    return { chainstate.insert_txs(txs), chainstate.pop_mempool_updates() };
}

api::ChainHead State::api_get_head() const
{
    NonzeroHeight nextHeight { next_height() };
    PinFloor pf { nextHeight.pin_floor() };
    PinHeight ph { pf };
    return api::ChainHead {
        .signedSnapshot { signedSnapshot },
        .worksum { chainstate.headers().total_work() },
        .nextTarget { chainstate.headers().next_target() },
        .hash { chainstate.final_hash() },
        .height { chainlength() },
        .pinHash { chainstate.headers().hash_at(pf) },
        .pinHeight { PinHeight(pf) },
        .hashrate = chainstate.headers().hashrate_at(chainlength(), 100)
    };
}

auto State::api_get_mempool(size_t n) const -> api::MempoolEntries
{
    std::vector<TxHash> hashes;
    auto nextHeight { next_height() };
    auto entries = chainstate.mempool().get_transactions(n, nextHeight, &hashes);
    assert(hashes.size() == entries.size());
    api::MempoolEntries out;
    for (size_t i = 0; i < hashes.size(); ++i) {
        out.entries.push_back(api::MempoolEntry { entries[i], hashes[i] });
    }
    return out;
}

auto State::api_get_history(const api::AccountIdOrAddress& a,
    int64_t beforeId) const
    -> wrt::optional<api::AccountHistory>
{
    auto p = a.map_alternative([&](const Address& a) { return db.lookup_account(a); });
    ;
    if (!p)
        return {};
    auto& accountId(*p);
    auto wartBalance(
        db.get_token_balance_recursive(accountId, TokenId::WART).balance);

    std::vector entries_desc = db.lookup_history_100_desc(accountId, beforeId);
    std::vector<api::Block> blocks_reversed;
    PinFloor pinFloor { 0 };
    auto firstHistoryId = HistoryId { 0 };
    auto nextHistoryOffset = HistoryId { 0 };
    chainserver::DBCache cache(db);

    auto prevHistoryId = HistoryId { 0 };
    api::block::Actions actions;
    for (auto iter = entries_desc.rbegin(); iter != entries_desc.rend(); ++iter) {
        auto& [historyId, entry] = *iter;
        if (firstHistoryId == HistoryId { 0 })
            firstHistoryId = historyId;
        assert(prevHistoryId < historyId);
        prevHistoryId = historyId;
        if (historyId >= nextHistoryOffset) {
            auto height { chainstate.history_height(historyId) };
            pinFloor = height.pin_floor();
            auto header = chainstate.headers()[height];
            bool b = height == chainlength();
            nextHistoryOffset = (b ? HistoryId { std::numeric_limits<uint64_t>::max() }
                                   : chainstate.history_offset(height + 1));
            blocks_reversed.push_back(api::Block(
                header, height, 1 + (chainlength() - height), std::move(actions)));
            actions = {};
        }
        api::Block& b = blocks_reversed.back();
        push_history(b, { historyId, std::move(entry) }, cache, pinFloor);
    }

    return api::AccountHistory {
        .balance = Wart::from_funds_throw(wartBalance.total),
        .locked = Wart::from_funds_throw(wartBalance.locked),
        .fromId = firstHistoryId,
        .blocks_reversed = blocks_reversed
    };
}

auto State::api_get_richlist(api::TokenIdOrSpec spec, size_t limit) const
    -> Result<api::RichlistInfo>
{
    if (auto token { normalize(spec) })
        return api::RichlistInfo { db.lookup_richlist(token->id, limit),
            std::move(*token) };
    return Error(ETOKIDNOTFOUND);
}

auto State::get_body_data(DescriptedBlockRange range) const
    -> std::vector<BodyData>
{
    assert(range.first() != 0);
    assert(range.last() >= range.first());
    std::vector<Hash> hashes;
    hashes.reserve(range.last() - range.first() + 1);
    std::vector<BodyData> res;
    if (range.descriptor == chainstate.descriptor()) {
        if (chainstate.length() < range.last())
            return {};
        for (Height h = range.first(); h < range.last() + 1; ++h) {
            hashes.push_back(chainstate.headers().hash_at(h));
        }
    } else {
        hashes = blockCache.get_hashes(range);
    }
    for (size_t i = 0; i < hashes.size(); ++i) {
        auto hash { hashes[i] };
        auto b { db.get_block_body(hash) };
        if (b) {
            res.push_back(std::move(*b));
        } else {
            spdlog::error("BUG: no block with hash {} in db.", serialize_hex(hash));
            return {};
        }
    }
    return res;
}

auto State::get_mempool_tx(TransactionId txid) const
    -> wrt::optional<TransactionMessage>
{
    return chainstate.mempool()[txid];
}

auto State::commit_fork(RollbackResult&& rr, AppendBlocksResult&& abr)
    -> StateUpdate
{
    assert(!signedSnapshot || signedSnapshot->compatible(stage));
    auto headers_ptr { blockCache.add_old_chain(chainstate, rr.deletionKey) };

    std::lock_guard l(chainstateMutex);
    chainstate.fork(chainserver::Chainstate::ForkData {
        .stage { stage },
        .rollbackResult { std::move(rr) },
        .appendResult { std::move(abr) },
    });

    state_update::Fork forkMsg {
        chainstate.headers().get_fork(rr.shrink, chainstate.descriptor()),
        std::move(headers_ptr), try_sign_locked_chainstate()
    };

    return StateUpdate {
        .chainstateUpdate { std::move(forkMsg) },
        .mempoolUpdates { chainstate.pop_mempool_updates() },
    };
}

auto State::commit_append(AppendBlocksResult&& abr) -> StateUpdate
{
    assert(!signedSnapshot || signedSnapshot->compatible(stage));
    std::lock_guard l(chainstateMutex);
    auto headerchainAppend { chainstate.append(Chainstate::AppendMulti {
        .patchedChain = stage,
        .appendResult { std::move(abr) },
    }) };

    return {
        .chainstateUpdate { state_update::Append {
            headerchainAppend,
            try_sign_locked_chainstate(),
        } },
        .mempoolUpdates { chainstate.pop_mempool_updates() },
    };
}

wrt::optional<SignedSnapshot> State::try_sign_locked_chainstate()
{
    // here, chainstateMutex should be locked already
    if ((!signedSnapshot.has_value() || (signedSnapshot->height() < chainstate.length())) && (signAfter < std::chrono::steady_clock::now() && signingEnabled) && snapshotSigner.has_value()) {
        spdlog::info("Signing chain state at height {}",
            chainstate.length().value());
        signedSnapshot = snapshotSigner->sign(chainstate);
        db.set_signed_snapshot(*signedSnapshot);
        return signedSnapshot;
    }
    return {};
}

MiningCache::CacheValidity State::mining_cache_validity() const
{
    return { dbCacheValidity, chainstate.mempool().cache_validity(),
        now_timestamp() };
}

size_t State::api_db_size() const { return db.byte_size(); }
} // namespace chainserver
