#include "defi/token/account_token.hpp"
#include "defi/uint64/lazy_matching.hpp"
#include "general/format_plural.hpp"
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

auto State::api_get_asset(const api::AssetIdOrHash& a) const -> wrt::optional<api::Asset>
{
    auto asset { normalize(a) };
    if (!asset.has_value())
        return {};
    return api::Asset {
        .name { asset->name.to_string() },
        .hash { asset->hash },
        .height { asset->height },
        .decimals { asset->decimals },
    };
}
auto State::api_search_asset(const api::AssetSearchArgs& args) const -> Result<api::AssetSearchResult>
{
    api::AssetSearchResult result(args);
    for (auto& a : db.search_assets(args)) {
        result.entries.push_back({ .name { a.name.to_string() }, .hash { a.hash }, .height { a.height }, .decimals { a.decimals } });
    };
    return result;
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

auto State::get_rollback_bounds(NonzeroHeight h) const -> wrt::optional<market_history::RollbackBounds>
{
    auto h1 { h + 1 };
    if (h > chainlength())
        return {};
    auto nextAssetId { [&] {
        if (h == chainlength())
            return AssetId(db.next_id64());
        return AssetId(chainstate.state_lower_bound(h1));
    }() };
    ;
    return market_history::RollbackBounds {
        .assetIdDeleteFrom { nextAssetId },
        .length { h },
        .timestamp { get_headers()[h].timestamp() },
    };
}

auto State::get_block_market_history(NonzeroHeight h) const -> wrt::optional<market_history::BlockInfo>
{
    auto id { db.consensus_block_id(h) };
    if (!id)
        return {};
    auto b { api_get_block(h) };
    assert(b);

    // BlockInfo(NonzeroHeight height, BlockHash hash, Timestamp timestamp)
    market_history::BlockInfo bi(b->height, b->header.hash(), b->header.timestamp());
    for (auto& ac : b->actions.assetCreations) {
        assert(ac.transaction.data.assetId.has_value()); // this block comes from database so it must be filled (unlike pending block)
        bi.insert_new_asset(ac.transaction.data.assetId.value(), AssetHash(ac.transaction.hash));
    }

    for (auto& m : b->actions.matches) {
        double baseTotal { 0.0 };
        double quoteTotal { 0.0 };
        for (auto& e : m.transaction.data.buySwaps) {
            quoteTotal += e.quote().to_double();
            baseTotal += e.base().to_decimal(m.transaction.data.assetInfo.decimals).to_double();
        }
        for (auto& e : m.transaction.data.sellSwaps) {
            quoteTotal += e.quote().to_double();
            baseTotal += e.base().to_decimal(m.transaction.data.assetInfo.decimals).to_double();
        }
        auto ta { market_history::TradeAmount::create(baseTotal, quoteTotal) };
        if (ta) {
            bi.push_trade(m.transaction.data.assetInfo.id, *ta);
        }
    }
    return bi;
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
namespace {
using MempoolOrderLoader = mempool::Mempool::OrderLoader;
struct OrderInfo : public defi::Order_uint64 {
    std::optional<HistoryId> hid;
    TxHash txHash;
    TransactionId txid;
    Funds_uint64 filled { 0 };
    Funds_uint64 remaining() const
    {
        return diff_assert(amount, filled);
    }
    OrderInfo(const OrderDataWithTxhash& od) // for entries that come from database
        : defi::Order_uint64(od.order)
        , hid(od.id)
        , txHash(od.txHash)
        , txid(od.txid)
        , filled(od.filled)
    {
    }
    OrderInfo(const MempoolOrderLoader::Entry& e)
        : defi::Order_uint64(e.swap().amount(), e.swap().limit())
        , txHash(e.hash())
        , txid(e.swap().txid())
    {
    }
};

template <bool ASCENDING>
class MergeSortOrderLoader {
    OrderLoaderTxhash<ASCENDING> dbLoader;
    MempoolOrderLoader loadFromMempool;

    wrt::optional<OrderDataWithTxhash> nextFromDb;
    wrt::optional<MempoolOrderLoader::Entry> nextFromMempool;
    std::vector<OrderInfo> loaded;

    void load_next_from_db()
    {
        if (nextFromDb) // only load if last load was successsful
            nextFromDb = dbLoader();
    }
    void load_next_mempool_order()
    {
        if (nextFromMempool)
            nextFromMempool = loadFromMempool();
    }
    void push_back(OrderInfo o)
    {
        if (!loaded.empty()) {
            if (ASCENDING)
                assert(o.limit >= loaded.back().limit);
            else
                assert(o.limit <= loaded.back().limit);
        }
        loaded.push_back(std::move(o));
    }

public:
    constexpr bool ascending() { return ASCENDING; }
    MergeSortOrderLoader(OrderLoaderTxhash<ASCENDING> loader, MempoolOrderLoader mempoolLoader)
        : dbLoader(std::move(loader))
        , loadFromMempool(std::move(mempoolLoader))
    {
        nextFromDb = dbLoader();
        nextFromMempool = loadFromMempool();
    }
    const OrderInfo* operator()()
    {
        std::optional<bool> fromMempool;

        // decide whether next element comes from mempool or from db
        if (nextFromMempool) {
            if (nextFromDb) {
                if (ASCENDING) { // we need to pick smaller to find next element in ascending order
                    fromMempool = nextFromMempool->swap().limit() < nextFromDb->order.limit;
                } else { // we need to pick larger to find next element in descending order
                    fromMempool = nextFromMempool->swap().limit() > nextFromDb->order.limit;
                }
            } else
                fromMempool = true;
        } else if (nextFromDb)
            fromMempool = false;

        if (fromMempool.has_value() == false)
            return nullptr;

        // load and consume the element.
        if (*fromMempool) {
            push_back(OrderInfo(*nextFromMempool));
            load_next_mempool_order();
        } else {
            push_back(OrderInfo(*nextFromDb));
            load_next_from_db();
        }
        return &loaded.back();
    }
    [[nodiscard]] auto fill_up_to(size_t N) &&
    {
        while (loaded.size() < N) {
            if (!(*this)())
                break;
        }
        return std::move(loaded);
    }
};

template <typename Loader>
class AggregateOrders {
private:
    Loader load;
    const OrderInfo* loaded;

private:
    void load_next()
    {
        if (!loaded)
            return;
        loaded = load();
    }

public:
    Loader&& loader() && { return std::move(load); }
    AggregateOrders(Loader l)
        : load(std::move(l))
        , loaded(load())
    {
    }

    defi::Order_uint64 load_next_order()
    {
        assert(loaded != nullptr);
        defi::Order_uint64 out { loaded->remaining(), loaded->limit };
        while (true) {
            loaded = load();
            if (!loaded)
                break;
            if (loaded->limit != out.limit) {
                if (load.ascending()) {
                    assert(loaded->limit > out.limit);
                } else {
                    assert(loaded->limit < out.limit);
                }
                break;
            }
            out.amount.add_assert(loaded->remaining());
        }
        return out;
    }
    wrt::optional<Price_uint64> next_price() const
    {
        if (loaded)
            return loaded->limit;
        return {};
    }
};

}

Result<api::MarketDetail> State::api_market_detail(const api::AssetIdOrHash& h, size_t N) const
{
    auto d { normalize(h) };
    if (!d)
        return d.error();
    auto assetId { d->id };
    MergeSortOrderLoader<true> sellsAsc(db.base_order_loader_txhash_ascending(assetId), chainstate.mempool().sells_asc(assetId));
    MergeSortOrderLoader<false> buysDesc(db.quote_order_loader_txhash_descending(assetId), chainstate.mempool().buys_desc(assetId));
    auto aggregatedSells { AggregateOrders(std::move(sellsAsc)) };
    auto aggregatedBuys { AggregateOrders(std::move(buysDesc)) };
    const auto pool { [&] -> defi::Pool_uint64 {
    if (auto dbpool { db.select_pool(assetId) })
        return *dbpool;
    else
        return defi::Pool_uint64{0,0,0}; }() };
    auto match { defi::match_lazy(aggregatedSells, aggregatedBuys, pool) };
    api::LiquidityPool p { pool.base, pool.quote.as_wart(), pool.shares_total() };
    api::MarketDetail orders(*d, p, match);
    auto to_api {
        [&](const OrderInfo& order) -> api::Order {
            uint32_t confirmations { 0 };
            std::optional<Height> height;
            if (order.hid) {
                auto hh { chainstate.history_height(*order.hid) };
                height = hh;
                confirmations = chainlength() - hh + 1;
            }
            return {
                .confirmations = confirmations,
                .height { height },
                .historyId { order.hid },
                .txHash { order.txHash },
                .txid { order.txid },
                .limit { order.limit },
                .amount { order.amount },
                .filled { order.filled },
            };
        }
    };
    for (auto& order : std::move(aggregatedBuys).loader().fill_up_to(N))
        orders.buys.push_back(to_api(order));
    for (auto& order : std::move(aggregatedSells).loader().fill_up_to(N))
        orders.sells.push_back(to_api(order));
    return orders;
};

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
        return api::block::TransactionSignedData(
            sd.origin_account_id(),
            c.existing_address(sd.origin_account_id()), sd.fee(), sd.pin_nonce().id,
            sd.pin_nonce().pin_height_from_floored(pinFloor));
    } };
    // e.hash, hid,
    e.data.visit_overload(
        [&](const history::WartTransferData& d) {
            b.actions.wartTransfers.push_back({ { e.hash,
                                                    api::block::WartTransferData { .toAddress = c.existing_address(d.to_id()),
                                                        .amount = d.wart() },
                                                    signed_info_data(d.sign_data()) },
                hid });
        },
        [&](const history::RewardData& d) {
            auto toAddress = c.existing_address(d.to_id());
            b.set_reward({ { e.hash, { toAddress, d.wart() } }, hid });
        },
        [&](const history::AssetCreationData& d) {
            b.actions.assetCreations.push_back(
                { { e.hash,
                      {
                          .name { d.asset_name() },
                          .supply { d.supply() },
                          .assetId { d.asset_id() },
                      },
                      signed_info_data(d.sign_data()) },
                    hid });
        },
        [&](const history::TokenTransferData& d) {
            auto& assetData { c.existing_asset(d.token_id().asset_id()) };

            b.actions.tokenTransfers.push_back(
                { { e.hash,
                      { .assetInfo = assetData,
                          .isLiquidity = d.token_id().is_liquidity(),
                          .toAddress = c.existing_address(d.to_id()),
                          .amount = { d.amount() } },
                      signed_info_data(d.sign_data()) },
                    hid });
        },
        [&](const history::OrderData& d) {
            auto& assetData { c.existing_asset(d.asset_id()) };
            b.actions.newOrders.push_back(
                { { e.hash,
                      {
                          .assetInfo { assetData },
                          .amount { d.amount() },
                          .limit { d.limit() },
                          .buy = d.buy(),
                      },
                      signed_info_data(d.sign_data()) },
                    hid });
        },
        [&](const history::CancelationData& d) {
            b.actions.cancelations.push_back(
                { { e.hash, { d.cancel_txid() }, signed_info_data(d.sign_data()) }, hid });
        },
        [&](const history::OrderCancelationData& d) {
            auto& asset { c.existing_asset(d.asset_id()) };
            b.actions.orderCancelations.push_back(
                { { e.hash,
                      { .cancelTxid { d.cancel_txid() },
                          .buy = d.buy(),
                          .assetInfo { asset },
                          .historyId { d.order_id() },
                          .remaining { d.amount() } } },
                    hid });
        },
        [&](const history::MatchData& d) {
            auto& asset { c.existing_asset(d.asset_id()) };
            b.actions.matches.push_back(
                { { e.hash,
                      { .assetInfo { asset },
                          .poolBefore { d.pool_before() },
                          .poolAfter { d.pool_after() },
                          .buySwaps { d.buy_swaps() },
                          .sellSwaps { d.sell_swaps() } } },
                    hid });
        },
        [&](const history::LiquidityDeposit& ld) {
            auto& asset { c.existing_asset(ld.asset_id()) };
            b.actions.liquidityDeposit.push_back(
                { { e.hash,
                      { .assetInfo { asset },
                          .baseDeposited { ld.base() },
                          .quoteDeposited { ld.quote() },
                          .sharesReceived { ld.shares() } },
                      signed_info_data(ld.sign_data()) },
                    hid });
        },
        [&](const history::LiquidityWithdraw& lw) {
            auto& asset { c.existing_asset(lw.asset_id()) };
            b.actions.liquidityWithdrawal.push_back(
                { { e.hash,
                      {
                          .assetInfo { asset },
                          .sharesRedeemed { lw.shares() },
                          .baseReceived { lw.base() },
                          .quoteReceived { lw.quote() },
                      },
                      signed_info_data(lw.sign_data()) },
                    hid });
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
    auto lower = chainstate.history_lower_bound(h);
    auto upper = (h == chainlength() ? HistoryId { 0 } : chainstate.history_lower_bound(h + 1));
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

const AssetDetail* State::lookup_hash_warn(const AssetHash& h) const
{
    auto p { dbcache.lookup_asset(h) };
    if (!p)
        spdlog::error("Cannot lookup asset for hash {}", serialize_hex(h));

    return p;
}

api::TransactionDetails State::api_dispatch_mempool(const TxHash& txHash,
    TransactionMessage&& tx) const
{

    auto get_asset {
        [&](AssetHash hash) -> const AssetDetail& {
            auto p { dbcache.lookup_asset(hash) };
            assert(p);
            return *p;
        }
    };
    auto make_signed_info { [&txHash](auto& tx) {
        // TransactionSignedData(AccountId originId, Address originAddress, Wart fee, NonceId nonceId, PinHeight pinHeight)
        return api::block::TransactionSignedData(tx.from_id(),
            tx.from_address(txHash), tx.fee(),
            tx.nonce_id(), tx.pin_height());
    } };

    return std::move(tx).visit_overload(
        [&](WartTransferMessage&& wtm) -> api::TransactionDetails {
            return api::MaybeMinedWartTransfer { {},
                { txHash,
                    {
                        .toAddress = wtm.to_addr(),
                        .amount = wtm.wart(),
                    },
                    make_signed_info(wtm) } };
        },
        [&](TokenTransferMessage&& ttm) -> api::TransactionDetails {
            // ttm.byte_size
            return api::TransactionDetails { api::MaybeMinedTokenTransfer {
                {},
                { txHash,
                    {
                        .assetInfo { get_asset(ttm.asset_hash()) },
                        .isLiquidity = ttm.is_liquidity(),
                        .toAddress = ttm.to_addr(),
                        .amount = ttm.amount(),
                    },
                    make_signed_info(ttm) } } };
        },
        [&](LimitSwapMessage&& o) -> api::TransactionDetails {
            return api::MaybeMinedNewOrder { {},
                { txHash,
                    {
                        .assetInfo { get_asset(o.asset_hash()) },
                        .amount { o.amount() },
                        .limit { o.limit() },
                        .buy = o.buy(),
                    },
                    make_signed_info(o) } };
        },
        [&](CancelationMessage&& a) -> api::TransactionDetails {
            return api::MaybeMinedCancelation {
                {},
                { txHash, { .cancelTxid { a.cancel_txid() } }, make_signed_info(a) }
            };
        },
        [&](LiquidityDepositMessage&& ld) -> api::TransactionDetails {
            return api::MaybeMinedLiquidityDeposit {
                {},
                { txHash,
                    {
                        .assetInfo { get_asset(ld.asset_hash()) },
                        .baseDeposited { ld.base() },
                        .quoteDeposited { ld.quote() },
                        .sharesReceived { wrt::nullopt },
                    },
                    make_signed_info(ld) }
            };
        },
        [&](LiquidityWithdrawalMessage&& lw) -> api::TransactionDetails {
            return api::MaybeMinedLiquidityWithdrawal {
                {},
                { txHash,
                    {
                        .assetInfo { get_asset(lw.asset_hash()) },
                        .sharesRedeemed { lw.amount() },
                        .baseReceived { wrt::nullopt },
                        .quoteReceived { wrt::nullopt },
                    },
                    make_signed_info(lw) }
            };
        },
        [&](AssetCreationMessage&& rm) -> api::TransactionDetails {
            return api::MaybeMinedAssetCreation { {},
                { txHash,
                    {
                        .name { rm.asset_name() },
                        .supply { rm.supply() },
                        .assetId { wrt::nullopt },
                    },
                    make_signed_info(rm) } };
        });
}

api::TransactionDetails State::api_dispatch_history(const TxHash& txHash,
    HistoryId hid, history::HistoryVariant&& tx) const
{
    NonzeroHeight h { chainstate.history_height(hid) };
    auto fetch_addr { [&](AccountId aid) { return dbcache.existing_address(aid); } };
    // TransactionMetaData(TxHash txhash, uint32_t confirmations, HistoryId hid, Height height, uint32_t timestamp)
    api::TransactionMinedData minedData {
        .hid { hid },
        .block {
            .height { h },
            .hash { chainstate.hash_at(h) },
            .timestamp = get_headers()[h].timestamp() }
    };
    auto make_signed_info { [&](auto& tx) {
        return api::block::TransactionSignedData(
            tx.sign_data().origin_account_id(),
            fetch_addr(tx.sign_data().origin_account_id()), tx.sign_data().fee(),
            tx.sign_data().pin_nonce().id,
            tx.sign_data().pin_nonce().pin_height_from_floored(h.pin_floor()));
    } };
    return std::move(tx).visit_overload(
        [&](history::WartTransferData&& wtm) -> api::TransactionDetails {
            // wtm.
            return api::MaybeMinedWartTransfer {
                minedData,
                { txHash,
                    {
                        .toAddress = fetch_addr(wtm.to_id()),
                        .amount = wtm.wart(),
                    },
                    make_signed_info(wtm) }
            };
        },
        [&](history::TokenTransferData&& ttm) -> api::TransactionDetails {
            auto& a { dbcache.existing_asset(ttm.token_id().asset_id()) };
            return api::MaybeMinedTokenTransfer {
                minedData,
                { txHash,
                    {
                        .assetInfo { a },
                        .isLiquidity = ttm.token_id().is_liquidity(),
                        .toAddress = fetch_addr(ttm.to_id()),
                        .amount = ttm.amount(),
                    },
                    make_signed_info(ttm) }
            };
        },
        [&](history::OrderData&& o) -> api::TransactionDetails {
            auto& a { dbcache.existing_asset(o.asset_id()) };
            return api::MaybeMinedNewOrder { minedData,
                { txHash,
                    {
                        .assetInfo { a },
                        .amount { o.amount() },
                        .limit { o.limit() },
                        .buy = o.buy(),
                    },
                    make_signed_info(o) } };
        },
        [&](history::CancelationData&& c) -> api::TransactionDetails {
            return api::MaybeMinedCancelation {
                minedData,
                { txHash, { .cancelTxid { c.cancel_txid() } }, make_signed_info(c) }
            };
        },
        [&](history::OrderCancelationData&& c) -> api::TransactionDetails {
            auto& a { dbcache.existing_asset(c.asset_id()) };
            return api::MaybeMinedOrderCancelation {
                minedData,
                { txHash,
                    api::block::OrderCancelationData { .cancelTxid { c.cancel_txid() },
                        .buy = c.buy(),
                        .assetInfo { a },
                        .historyId { c.order_id() },
                        .remaining { c.amount() } } }
            };
        },
        [&](history::LiquidityDeposit&& ld) -> api::TransactionDetails {
            auto& a { dbcache.existing_asset(ld.asset_id()) };
            return api::MaybeMinedLiquidityDeposit {
                minedData,
                { txHash,
                    { .assetInfo { a },
                        .baseDeposited { ld.base() },
                        .quoteDeposited { ld.quote() },
                        .sharesReceived { ld.shares() } },
                    make_signed_info(ld) }
            };
        },
        [&](history::LiquidityWithdraw&& lw) -> api::TransactionDetails {
            auto& a { dbcache.existing_asset(lw.asset_id()) };
            return api::MaybeMinedLiquidityWithdrawal {
                minedData,
                { txHash,
                    { .assetInfo { a },
                        .sharesRedeemed { lw.shares() },
                        .baseReceived { lw.base() },
                        .quoteReceived { lw.quote() } },
                    make_signed_info(lw) }
            };
        },
        [&](history::RewardData&& rm) -> api::TransactionDetails {
            return api::MinedReward {
                minedData,
                { txHash,
                    { .toAddress { fetch_addr(rm.to_id()) }, .amount { rm.wart() } } }
            };
        },
        [&](history::AssetCreationData&& rm) -> api::TransactionDetails {
            return api::MaybeMinedAssetCreation { minedData,
                { txHash,
                    {
                        .name { rm.asset_name() },
                        .supply { rm.supply() },
                        .assetId { rm.asset_id() },
                    },
                    make_signed_info(rm) } };
        },
        [&](history::MatchData&& rm) -> api::TransactionDetails {
            auto& a { dbcache.existing_asset(rm.asset_id()) };
            return api::MinedMatch { minedData,
                { txHash,
                    {
                        .assetInfo { a },
                        .poolBefore { rm.pool_before() },
                        .poolAfter { rm.pool_after() },
                        .buySwaps { rm.buy_swaps() },
                        .sellSwaps { rm.sell_swaps() },
                    } } };
        });
}

wrt::optional<api::TransactionDetails> State::api_get_tx(const TxHash& txHash) const
{
    if (auto p { chainstate.mempool()[txHash] }; p)
        return api_dispatch_mempool(txHash, std::move(*p));
    if (auto p { db.lookup_history(txHash) }; p) {
        auto& [parsed, historyId] = *p;
        return api_dispatch_history(txHash, historyId, std::move(parsed));
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
    HistoryId lower { chainstate.history_lower_bound(hLower) };
    return api_get_transaction_range(lower, upper);
}

auto State::api_get_miner(NonzeroHeight h) const
    -> wrt::optional<api::Account>
{
    if (chainlength() < h)
        return {};
    auto offset { chainstate.history_lower_bound(h) };
    auto parsed { db.fetch_existing<history::Entry>(offset).data };

    assert(std::holds_alternative<history::RewardData>(parsed));
    auto minerId { std::get<history::RewardData>(parsed).to_id() };
    return api::Account { db.fetch_existing<Address>(minerId), minerId };
}

auto State::api_get_miners(HeightRange hr) const
    -> std::vector<api::Account>
{
    std::vector<api::Account> res;
    for (auto h : hr) {
        auto o { api_get_miner(h) };
        assert(o);
        res.push_back(*o);
    }
    return res;
}

auto State::api_get_latest_miners(uint32_t N) const
    -> std::vector<api::Account>
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
            auto beginId { chainstate.history_lower_bound(h) };
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

HeaderBatch State::get_headers_concurrent(HeaderBatchSelector s) const
{
    std::lock_guard l(chainstateMutex);
    if (s.descriptor == chainstate.descriptor()) {
        return chainstate.headers().get_headers(s.header_range());
    } else {
        return blockCache.get_headerbatch_concurrent(s);
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
                         AssetHash hash) mutable -> block::body::TokenSection* {
            auto iter = assetOffsets.lower_bound(hash);
            if (iter == assetOffsets.end() || iter->first != hash) {
                auto a { lookup_hash_warn(hash) };
                if (a == nullptr) {
                    // this should not happen but we will just exclude this transaction
                    return nullptr;
                }
                iter = assetOffsets.emplace_hint(iter, hash, entries.tokens().size());
                entries.tokens().push_back({ a->id });
            }
            return &entries.tokens()[iter->second];
        } };

        for (auto& tx : transactions) {
            bool inserted = std::move(tx).visit_overload(
                [&](WartTransferMessage&& m) {
                    return entries.wart_transfers().try_push_back(
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
                    auto a { asset(m.asset_hash()) };
                    if (a == nullptr)
                        return false;
                    if (m.is_liquidity()) {
                        auto& transfers = a->liquidity_transfers();
                        return transfers.try_push_back({ m.from_id(), m.pin_nonce_throw(height),
                                                           m.compact_fee(), addr_id(m.to_addr()),
                                                           m.amount(), m.signature() },
                            v);
                    } else {
                        auto a { asset(m.asset_hash()) };
                        if (a == nullptr)
                            return false;
                        auto& transfers = a->asset_transfers();
                        return transfers.try_push_back({ m.from_id(), m.pin_nonce_throw(height),
                                                           m.compact_fee(), addr_id(m.to_addr()),
                                                           m.amount(), m.signature() },
                            v);
                    }
                },
                [&](LimitSwapMessage&& m) {
                    auto a { asset(m.asset_hash()) };
                    if (a == nullptr)
                        return false;
                    return a
                        ->orders()
                        .try_push_back({ m.from_id(), m.pin_nonce_throw(height),
                                           m.compact_fee(), m.buy(), m.amount(), m.limit(),
                                           m.signature() },
                            v);
                },
                [&](CancelationMessage&& m) {
                    return entries.cancelations().try_push_back(
                        { m.from_id(), m.pin_nonce_throw(height), m.compact_fee(),
                            m.cancel_height(), m.cancel_nonceid(), m.signature() },
                        v);
                },
                [&](LiquidityDepositMessage&& m) {
                    auto a { asset(m.asset_hash()) };
                    if (a == nullptr)
                        return false;
                    return a
                        ->liquidity_deposits()
                        .try_push_back({ m.from_id(), m.pin_nonce_throw(height),
                                           m.compact_fee(), m.base(), m.quote(),
                                           m.signature() },
                            v);
                },
                [&](LiquidityWithdrawalMessage&& m) {
                    auto a { asset(m.asset_hash()) };
                    if (a == nullptr)
                        return false;
                    return a->liquidity_withdrawals()
                        .try_push_back({ m.from_id(), m.pin_nonce_throw(height),
                                           m.compact_fee(), m.amount(), m.signature() },
                            v);
                },
                [&](AssetCreationMessage&& m) {
                    return entries.asset_creations().try_push_back(
                        { m.from_id(), m.pin_nonce_throw(height), m.compact_fee(),
                            AssetSupplyEl(m.supply()), m.asset_name(), m.signature() },
                        v);
                });
            if (inserted)
                minerReward.add_assert(tx.fee()); // assert because
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
                auto toAddress { c.existing_address(t.to_id()) };
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
                auto asset { c.existing_asset(s.asset_id()) };
                apply_to_array(
                    s,
                    [&](PinHeight pinHeight, const AssetTransfer& t) {
                        auto toAddress { c.existing_address(t.to_id()) };
                        toMempool.push_back(TokenTransferMessage(
                            t.txid(pinHeight), t.pin_nonce().reserved, t.compact_fee(),
                            asset.hash, false, toAddress, t.amount(), t.signature()));
                    },
                    [&](PinHeight pinHeight, const LiquidityTransfer& t) {
                        auto toAddress { c.existing_address(t.to_id()) };
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

RollbackResult State::rollback(const Height newlength, std::string_view reason) const
{
    assert(newlength < chainlength());
    const Height oldlength { chainlength() };
    auto n { chainlength() - newlength };
    spdlog::info("Rolling back {} to height {}, reason: {}", format_plural(n, "block"), newlength.value(), reason);
    const NonzeroHeight beginHeight = newlength.add1();
    auto endHeight(chainlength().add1());

    // load ids
    auto ids { db.consensus_block_ids({ beginHeight, endHeight }) };
    assert(ids.size() == endHeight - beginHeight);
    assert(ids.size() > 0);

    auto historyOffset { chainstate.history_lower_bound(beginHeight) };
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
            .chainstateUpdate = state_update::SignedSnapshotApply { {}, *signedSnapshot },
            .mempoolUpdates {},
        },
        .appendedBlocks {}
    };
    auto dbTx { db.transaction() };
    if (!signedSnapshot->compatible(chainstate.headers())) {
        assert(signedSnapshot->height() <= chainlength());
        auto rb { rollback(signedSnapshot->height() - 1, "signed snapshot") };

        std::lock_guard l(chainstateMutex);
        auto headers_ptr { blockCache.add_old_chain(chainstate, rb.deletionKey) };

        res.update.chainstateUpdate = state_update::SignedSnapshotApply {
            { state_update::SignedSnapshotApply::Rollback::Data {
                .deltaHeaders { chainstate.rollback(rb) },
                .prevHeaders { std::move(headers_ptr) },
            } },
            *signedSnapshot,
        };
        res.update.mempoolUpdates = chainstate.pop_mempool_updates();
    } else {
        assert(chainstate.pop_mempool_updates().size() == 0);
    };

    db.set_consensus_work(chainstate.headers().total_work());
    db.set_signed_snapshot(*signedSnapshot);
    dbTx.commit();
    dbcache.clear();

    return res;
}

auto State::api_rollback(Height h) -> wrt::optional<StateUpdateWithAPIBlocks>
{
    dbCacheValidity += 1;

    using namespace state_update;

    // consider chainstate
    state_update::StateUpdateWithAPIBlocks res {
        .update {
            .chainstateUpdate = state_update::Rollback {
                .rollback {} },
            .mempoolUpdates {},
        },
        .appendedBlocks {}
    };
    auto dbTx { db.transaction() };
    if (h < chainlength()) {
        auto rb { rollback(h, "API") };

        std::lock_guard l(chainstateMutex);
        auto headers_ptr { blockCache.add_old_chain(chainstate, rb.deletionKey) };

        res.update.chainstateUpdate = state_update::Rollback {
            .rollback { state_update::Rollback::Data {
                .deltaHeaders { chainstate.rollback(rb) },
                .prevHeaders { std::move(headers_ptr) },
            } },
        };
        res.update.mempoolUpdates = chainstate.pop_mempool_updates();
    } else {
        assert(chainstate.pop_mempool_updates().size() == 0);
    };

    db.set_consensus_work(chainstate.headers().total_work());
    dbTx.commit();
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
        auto txhash { chainstate.create_tx_throw(m) };
        auto log { chainstate.pop_mempool_updates() };
        spdlog::info("Added new \"{}\" transaction to mempool", m.tag());
        return { std::move(log), std::move(txhash) };
    } catch (const Error& e) {
        spdlog::warn("Rejected new \"{}\" transaction: {}", m.tag(), e.strerror());
        throw;
    }
}

api::WartBalanceLookup State::api_get_wart_balance(api::AccountIdOrAddress account) const
{
    wrt::optional<api::Account> acc;
    if (auto a { normalize(account) })
        acc = *a;
    auto balance { acc ? api_get_token_balance_recursive(acc->id, TokenId::WART).balance : api::FundsBalance::zero() };

    return {
        .account { std::move(acc) },
        .balance { .total { Wart::from_funds(balance.total.funds) }, .locked { Wart::from_funds(balance.locked.funds) }, .mempool { Wart::from_funds(balance.mempool.funds) } }
    };
}
template <class Factory>
struct CallConverter {
    constexpr CallConverter(Factory f)
        : factory(std::move(f))
    {
    }
    constexpr operator auto() const { return factory(); }
    Factory factory;
};

std::vector<api::MarketOrders> State::api_account_orders(api::AccountIdOrAddress account) const
{
    auto acc { normalize(account).value_or_throw() };
    std::map<AssetId, api::MarketOrders> map;
    auto map_entry {
        [&, assetId = std::optional<AssetId>(),
            pOrders = (api::MarketOrders*)(nullptr)](AssetId id) mutable -> api::MarketOrders& {
            if (id != assetId) {
                assetId = id;
                pOrders = &map.try_emplace(id, CallConverter([&]() {
                                  auto asset { normalize(id) };
                                  assert(asset.has_value());
                                  return api::MarketOrders(*asset);
                              }))
                               .first->second;
            }
            return *pOrders;
        }
    };

    for (auto& [e, txhash] : db.lookup_account_orders(acc.id)) {
        auto& ref { map_entry(e.aid) };
        auto& v { (e.buy ? ref.buys : ref.sells) };
        auto height { chainstate.history_height(e.id) };
        v.push_back({
            .confirmations = chainlength() - height + 1,
            .height { height },
            .historyId { e.id },
            .txHash { txhash },
            .txid { e.txid },
            .limit { e.limit },
            .amount { e.total },
            .filled { e.filled },
        });
    }
    for (auto& entry : chainstate.mempool().account_txs(acc.id)) {
        if (entry.holds<LimitSwapMessage>()) {
            auto nw { entry.altTokenId.non_wart() };
            assert(nw);
            auto& swap { entry.get<LimitSwapMessage>() };
            auto currentAssetId { nw->asset_id() };
            auto& ref { map_entry(currentAssetId) };
            auto& v { (swap.buy() ? ref.buys : ref.sells) };
            v.push_back({
                .confirmations = 0,
                .height {},
                .historyId {},
                .txHash { entry.txhash },
                .txid { entry.txid() },
                .limit { swap.limit() },
                .amount { swap.amount() },
                .filled { 0 },
            });
        }
    };

    // now convert map values to vector
    std::vector<api::MarketOrders> out;
    for (auto&& e : std::move(map)) {
        out.push_back(std::move(e.second));
    }
    return out;
}

api::MarketOrders State::api_account_orders_market(api::AccountIdOrAddress account, api::AssetIdOrHash asset) const
{
    auto acc { normalize(account).value_or_throw() };
    auto as { normalize(asset).value_or_throw() };
    api::MarketOrders out(as);

    for (auto& [e, txhash] : db.lookup_account_orders_market(acc.id, as.id)) {
        assert(e.aid == as.id);
        auto& v { (e.buy ? out.buys : out.sells) };
        auto height { chainstate.history_height(e.id) };
        v.push_back({
            .confirmations = chainlength() - height + 1,
            .height { height },
            .historyId { e.id },
            .txHash { txhash },
            .txid { e.txid },
            .limit { e.limit },
            .amount { e.total },
            .filled { e.filled },
        });
    }

    for (auto& entry : chainstate.mempool().account_txs(acc.id)) {
        if (!entry.holds<LimitSwapMessage>())
            continue;
        auto nw { entry.altTokenId.non_wart() };
        assert(nw);
        if (nw->asset_id() != as.id)
            continue;
        auto& swap { entry.get<LimitSwapMessage>() };
        auto& v { (swap.buy() ? out.buys : out.sells) };
        v.push_back({
            .confirmations = 0,
            .height {},
            .historyId {},
            .txHash { entry.txhash },
            .txid { entry.txid() },
            .limit { swap.limit() },
            .amount { swap.amount() },
            .filled { 0 },
        });
    };
    return out;
}

auto State::normalize(api::TokenIdOrSpec token) const
    -> Result<api::Token>
{
    return token.visit_overload(
        [&](TokenId id) -> Result<api::Token> {
            if (auto nwId { id.non_wart() }) {
                auto asset { db.lookup_asset(nwId->asset_id()) };
                if (asset)
                    return api::Token {
                        .id { id },
                        .spec { asset->hash, nwId->is_liquidity() },
                        .name { asset->name.to_string() },
                        .assetDecimals { asset->decimals },
                    };
                return Error(ETOKENNOTFOUND); // asset corresponding to token id does not exist
            } else {
                return api::Token::WART();
            }
        },
        [&](const api::TokenSpec& h) -> Result<NormalizedToken> {
            auto asset { db.lookup_asset(h.assetHash) };
            if (asset) {
                auto tid { asset->id.token_id(h.isLiquidity) };
                assert(tid.is_liquidity() == h.isLiquidity);
                return api::Token {
                    .id { tid },
                    .spec { asset->hash, h.isLiquidity },
                    .name { asset->name.to_string() },
                    .assetDecimals { asset->decimals },
                };
            } else if (h.assetHash.is_wart()) {
                return api::Token::WART();
            }
            return Error(ETOKENNOTFOUND);
        });
}

size_t State::on_mempool_constraint_update()
{
    return chainstate.on_mempool_constraint_update();
}
Result<api::Account> State::normalize(api::AccountIdOrAddress a) const
{
    return a.visit_overload(
        [&](const AccountId& id) -> Result<api::Account> {
            if (auto addr { db.lookup_address(id) })
                return api::Account { .address { *addr }, .id { id } };
            return Error(EACCIDNOTFOUND);
        },
        [&](const Address& addr) -> Result<api::Account> {
            if (auto id { db.lookup_account(addr) })
                return api::Account { .address { addr }, .id { *id } };
            return Error(EADDRNOTFOUND);
        });
}

Result<api::TokenBalanceLookup> State::api_get_token_balance_recursive(api::AccountIdOrAddress account,
    api::TokenIdOrSpec spec) const
{
    auto token { normalize(spec) };
    if (!token)
        return Error(ETOKENNOTFOUND);
    auto acc { normalize(account) };
    if (!acc)
        return api::TokenBalanceLookup { .token { *token }, .balance { api::FundsBalance::zero() }, .account {}, .lookupTrace {} };
    auto b { api_get_token_balance_recursive(acc->id, token->id) };
    return api::TokenBalanceLookup { .token { *token }, .balance { b.balance }, .account { *acc }, .lookupTrace { std::move(b.lookupTrace) } };
}

auto State::api_get_token_balance_recursive(AccountId aid, TokenId tid) const -> BalanceLookup
{
    api::AssetLookupTrace trace;
    auto b { db.get_token_balance_recursive(aid, tid, &trace) };

    wrt::optional<TokenDecimals> dec;
    if (auto nw { tid.non_wart() }; nw && !nw->is_liquidity()) {
        if (!trace.fails.empty()) {
            dec = trace.fails.front().decimals;
        } else {
            dec = db.lookup_asset(nw->asset_id())->decimals;
        }
    } else { // means that token id is that of WART or pool liquidity (has WART
             // decimals by definition)
        dec = Wart::decimals;
    }
    if (!dec)
        return { .lookupTrace = {}, .balance { api::FundsBalance::zero() } };
    return { .lookupTrace { std::move(trace) },
        .balance {
            .total { FundsDecimal(b.balance.total, *dec) },
            .locked { FundsDecimal(b.balance.locked, *dec) },
            .mempool { FundsDecimal(chainstate.mempool().locked_balance(aid, tid), *dec) } } };
}

Result<AssetDetail> State::normalize(const api::AssetIdOrHash&
        token) const
{
    return token.visit([&](const auto& token) { return db.lookup_asset(token); });
}

auto State::insert_txs(TxVec&& txs)
    -> std::pair<std::vector<Error>, mempool::Updates>
{
    return { chainstate.insert_txs(std::move(txs)), chainstate.pop_mempool_updates() };
}

api::ChainHead State::api_get_head() const
{
    NonzeroHeight nextHeight { next_height() };
    PinFloor pf { nextHeight.pin_floor() };
    PinHeight ph { pf };
    return api::ChainHead {
        // .signedSnapshot { signedSnapshot },
        .worksum { chainstate.headers().total_work() },
        .nextTarget { chainstate.headers().next_target() },
        .hash { chainstate.final_hash() },
        .height { chainlength() },
        .pinHash { chainstate.headers().hash_at(pf) },
        .pinHeight { PinHeight(pf) },
        .hashrate = chainstate.headers().hashrate_at(chainlength(), 100)
    };
}

auto State::api_get_account_mempool(api::AccountIdOrAddress account, size_t) const -> api::MempoolEntries
{
    auto acc { normalize(account).value_or_throw() };
    api::MempoolEntries out;
    for (auto& e : chainstate.mempool().account_txs(acc.id))
        out.entries.push_back({ *static_cast<const TransactionMessage*>(&e), e.txhash });
    return out;
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
                                   : chainstate.history_lower_bound(height + 1));
            blocks_reversed.push_back(api::Block(
                header, height, 1 + (chainlength() - height), std::move(actions)));
            actions = {};
        }
        api::Block& b = blocks_reversed.back();
        push_history(b, { historyId, std::move(entry) }, cache, pinFloor);
    }

    return api::AccountHistory {
        .balance = Wart::from_funds(wartBalance.total),
        .locked = Wart::from_funds(wartBalance.locked),
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
