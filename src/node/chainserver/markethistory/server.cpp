#include "server.hpp"
#include "../server.hpp"
#include <numeric>

namespace market_history {

void Reader::work()
{
    while (true) {
        std::unique_lock l(parent.m);
        parent.cv.wait(l, [&] {
            return parent._shutdown || !parent.readerEvents.empty();
        });
        if (parent._shutdown)
            break;
        // take one event at a time
        auto event { parent.readerEvents.front() };
        parent.readerEvents.pop_front();
        l.unlock();
        dispatch(std::move(event));
    }
    spdlog::debug("Shutting down Reader");
}

Result<Asset> Reader::normalize(const api::AssetIdOrHash& asset)
{
    return asset.visit_overload(
        [&](const AssetId& id) -> Result<Asset> {
            auto a { db.get_asset(id) };
            if (a.has_value()) {
                return *a;
            }
            return Error(EASSETIDNOTFOUND);
        },
        [&](const AssetHash& hash) -> Result<Asset> {
            auto a { db.get_asset(hash) };
            if (a.has_value()) {
                return *a;
            }
            return Error(EASSETHASHNOTFOUND);
        });
}

void Reader::dispatch(ReaderEventInternal&& event)
{
    std::move(event).visit([&](auto&& e) {
        // open transaction to have consistent view on database
        // across multiple queries while the writer can change
        // the database (we use WAL SQLite feature)
        auto tx { db.transaction() };
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::derived_from<T, HasAsset>) {
            auto a { normalize(e.asset) };
            if (!a)
                return e.callback(a.error());
            if constexpr (std::derived_from<T, GetCandlesBase>) {
                return handle_event(*a, std::move(e));
            }
            handle_event(*a, std::move(e));
        } else {
            handle_event(std::move(e));
        }
        tx.commit(); // close transaction (does not write anything)
    });
}

void Reader::handle_event(const Asset& a, GetTradesRange&& e)
{
    e.callback(db.get_trades_range(a, e.from, e.to));
}
void Reader::handle_event(const Asset& a, GetTradesFrom&& e)
{
    e.callback(db.get_trades_from(a, e.from, e.N));
}
void Reader::handle_event(const Asset& a, GetTradesTo&& e)
{
    e.callback(db.get_trades_to(a, e.to, e.N));
}
void Reader::handle_event(const Asset& a, GetTradesLatest&& e)
{
    e.callback(db.get_trades_latest(a, e.N));
}
void Reader::handle_event(const Asset& a, GetCandlesRange&& e)
{
    e.callback(db.get_candles_range(a, e.interval, e.from, e.to));
}
void Reader::handle_event(const Asset& a, GetCandlesFrom&& e)
{
    e.callback(db.get_candles_from(a, e.interval, e.from, e.N));
}
void Reader::handle_event(const Asset& a, GetCandlesTo&& e)
{
    e.callback(db.get_candles_to(a, e.interval, e.to, e.N));
}
void Reader::handle_event(const Asset& a, GetCandlesLatest&& e)
{
    e.callback(db.get_candles_latest(a, e.interval, e.N));
}

ReaderThreadpool::ReaderThreadpool(MarketDb& db, size_t N)
{
    for (size_t i { 0 }; i < N; ++i)
        readers.push_back(std::make_unique<Reader>(db.clone_reader(), *this));
}
namespace {
constexpr size_t MAX_N = 200;
constexpr size_t DEFAULT_N = 100;
}

ReaderEventInternal MarketHistoryServer::wrap_event_throw(GetTrades::Object&& o)
{
    auto& req { o.request };
    if (req.N() > MAX_N)
        throw Error(ERANGETOOBIG);

    auto base { [&] { return GetTradesBase { req.asset(), std::move(o.callback) }; } };
    if (req.to()) {
        if (req.from()) {
            if (req.N()) // cannot have all 3 args
                throw Error(EINVARGCOMB);

            if (*req.to() < *req.from())
                throw Error(EINVRANGE);
            size_t N = req.to()->value() - req.from()->value();
            if (N > MAX_N)
                throw Error(ERANGETOOBIG);
            return GetTradesRange { base(), *req.from(), *req.to() };
        }
        return GetTradesTo { base(), *req.to(), req.N().value_or(DEFAULT_N) };
    }
    if (req.from()) {
        return GetTradesFrom { base(), *req.from(), req.N().value_or(DEFAULT_N) };
    }
    return GetTradesLatest { base(), req.N().value_or(DEFAULT_N) };
}

ReaderEventInternal MarketHistoryServer::wrap_event_throw(GetCandles::Object&& o)
{
    auto& req { o.request };
    auto i { market_history::Interval::from_slug(req.interval()) };
    if (!i) // could not parse interval
        throw Error(EINVINTERVAL);
    auto base { [&] { return GetCandlesBase { req.asset(), *i, std::move(o.callback) }; } };
    if (req.N() > MAX_N)
        throw Error(ERANGETOOBIG);

    if (req.to()) {
        if (req.from()) {
            if (req.N()) // cannot have all 3 args
                throw Error(EINVARGCOMB);

            if (*req.to() < *req.from())
                throw Error(EINVRANGE);
            size_t N = (req.to()->value() - req.from()->value()) / i->seconds();
            if (N > MAX_N)
                throw Error(ERANGETOOBIG);
            return GetCandlesRange {
                base(), *req.from(), *req.to()
            };
        }
        return GetCandlesTo {
            base(), *req.to(), req.N().value_or(DEFAULT_N)
        };
    }
    if (req.from()) {
        return GetCandlesFrom {
            base(), *req.from(), req.N().value_or(DEFAULT_N)
        };
    }
    return GetCandlesLatest { base(), req.N().value_or(DEFAULT_N) };
}

void MarketHistoryServer::api_call(GetCandles::Object&& ev)
{
    try {
        defer(wrap_event_throw(std::move(ev)));
    } catch (Error e) {
        ev.callback(e);
    }
}

void MarketHistoryServer::api_call(GetTrades::Object&& ev)
{
    try {
        defer(wrap_event_throw(std::move(ev)));
    } catch (Error e) {
        ev.callback(e);
    }
}

void MarketHistoryServer::work()
{
    spdlog::debug("Starting MarketHistoryServer");
    while (true) {
        std::unique_lock l(m);
        cv.wait(l, [&] { return _shutdown || !events.empty(); });
        if (_shutdown)
            break;
        decltype(events) tmp(std::move(events));
        events.clear();
        l.unlock();
        for (auto& e : tmp)
            std::move(e).visit([&](auto&& e) { handle_event(std::move(e)); });
    }
    spdlog::debug("Shutting down MarketHistoryServer");
}

MarketHistoryServer::MarketHistoryServer(InitData data)
    : db(data.db)
    , chainServer(data.chainServer)
    , consensusCopy(std::move(data.consensusCopy))
    , consensusDescriptor(std::move(data.consensusDescriptor))
    , readers(db, data.readerCount)
{
    try_initial_rollback();
    worker = std::thread(&MarketHistoryServer::work, this);
}

void MarketHistoryServer::try_initial_rollback()
{
    auto l { std::min(db.chain_length(), consensusCopy.length()) };
    if (!l.is_zero()) {
        auto lower { Height(0) };
        auto upper = l + 1;
        // To boost bisection for long chains we try to go back only
        // 100 blocks in first step to hope for matching headers at
        // that position. This is likely to match and then skips
        // lengthy lookup at earlier heights.
        auto h { (l.value() > 100 ? (l - 100) : Height(1)) };
        while (h != lower) {
            spdlog::info("Testing market history db at height {}", h.value());
            auto hash { db.get_block_hash(h.nonzero_assert()) };
            assert(hash); // should be present in db because tmp < upper
            if (*hash == consensusCopy.hash_at(h)) // matching headers
                lower = h;
            else
                upper = h;
            h = Height(std::midpoint(lower.value(), upper.value()));
        }
        assert(upper == lower.add1());
        // at this point `lower` is the length of identical blocks
        l = lower;
    }
    if (auto h { l.nonzero() }) {
        if (db.chain_length() != *h) {
            scheduledRollbackHeight = *h;
            chainServer.get_rollback_bounds(*h, consensusDescriptor, [&, desc = this->consensusDescriptor](RollbackBounds&& rb) {
                defer(BoundsReply {
                    .rollbackBounds { std::move(rb) },
                    .descriptor { desc } });
            });
            return;
        }
    } else { // joint length l == 0
        db.clear(); // complete rollback, i.e. delete all history from database
    }
    // we can start requesting blocks to sync with consensus chain
    scheduledRollbackHeight.reset();
    try_request_block();
}
void MarketHistoryServer::transaction_rollback(const RollbackBounds& nextBounds)
{
    auto transaction { db.transaction() };
    db.rollback(nextBounds); // roll back if necessary
    transaction.commit();
}

void MarketHistoryServer::handle_event(OnChainReplace&& e)
{
    assert(e.descriptor > consensusDescriptor);
    consensusDescriptor = e.descriptor;
    if (scheduledRollbackHeight) {
        // We are in the initial rollback phase.
        // Since consensus forked we will try
        // adjust rollback preparation and
        // possibly request rollback bounds again.
        try_initial_rollback();
    } else {
        transaction_rollback(e.rollbackBounds);
        try_request_block();
    }
}
void MarketHistoryServer::handle_event(OnChainAppend&& e)
{
    if (scheduledRollbackHeight)
        return; // ignore if we not yet following consensus chain
    if (db.chain_length().add1() != e.block.height) {
        // chain should be behind consensus otherwise there
        // is a bug
        assert(db.chain_length() <= e.block.height);
        return;
    }
    db.append_block(e.block);
}

void MarketHistoryServer::handle_event(BlockReply&& e)
{
    // we don't request blocks (so we can't receive this event)
    // before we have completed initial rollback
    assert(scheduledRollbackHeight.has_value() == false);

    if (e.descriptor == consensusDescriptor) {
        assert(e.blockInfo.height == db.chain_length().add1());
        db.append_block(e.blockInfo);
        try_request_block();
    }
}
void MarketHistoryServer::handle_event(BoundsReply&& e)
{
    // we don't expect this event after initial rollback
    assert(scheduledRollbackHeight.has_value());

    if (e.descriptor == consensusDescriptor) {
        db.rollback(e.rollbackBounds);
        scheduledRollbackHeight.reset();
        try_request_block();
    }
}
void MarketHistoryServer::try_request_block()
{
    // request next block to catch up if necessary
    if (db.chain_length() < consensusCopy.length()) {
        chainServer.get_block_market_history(db.chain_length().add1(), consensusDescriptor, [&, desc = this->consensusDescriptor](BlockInfo&& bi) {
            defer(BlockReply {
                .blockInfo { std::move(bi) },
                .descriptor { desc } });
        });
    }
}
}
