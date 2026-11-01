#include "server.hpp"
#include "api/events/emit.hpp"
#include "api/events/subscription.hpp"
#include "api/types/shared.hpp"
#include "block/header/header_impl.hpp"
#include "chainserver/db/chain_db.hpp"
#include "eventloop/eventloop.hpp"
#include "global/globals.hpp"
#include "spdlog/spdlog.h"

bool ChainServer::is_busy()
{
    std::unique_lock<std::mutex> ul(mutex);
    return switching;
}

HeaderBatch ChainServer::get_headers(HeaderBatchSelector selector)
{
    return state.get_headers_concurrent(selector);
}

std::optional<HeaderView> ChainServer::get_descriptor_header(Descriptor descriptor, Height height)
{
    return state.get_header_concurrent(descriptor, height);
}

ConsensusSlave ChainServer::get_chainstate()
{
    return state.get_chainstate_concurrent();
}

ChainServer::ChainServer(ChainDB& db, MarketDb* marketDb, BatchRegistry& br, std::optional<SnapshotSigner> snapshotSigner, Token)
    : db(db)
    , batchRegistry(br)
    , state(db, br, snapshotSigner)
{

    if (marketDb != nullptr)
        marketServer.emplace(market_history::MarketHistoryServer::InitData {
            .db = *marketDb,
            .chainServer = *this,
            .consensusCopy = state.get_headers(),
            .consensusDescriptor = state.get_descriptor(),
            .readerCount = 1 });
    emit_chain_state_event();
}

ChainServer::~ChainServer()
{
    wait_for_shutdown();
}

void ChainServer::async_set_synced(bool synced)
{
    spdlog::debug("Set synced {}", synced);
    defer(SetSynced { synced });
}

void ChainServer::async_put_mempool(std::vector<TransactionMessage> txs)
{
    defer(PutMempoolBatch { std::move(txs) });
}

auto ChainServer::api_subscribe_mining(Address address, mining_subscription::callback_t callback) -> mining_subscription::MiningSubscription
{
    auto req { SubscribeMining::make(address, callback) };
    auto id { req.id };
    defer(std::move(req));
    return { shared_from_this(), id };
}

void ChainServer::api_unsubscribe_mining(mining_subscription::SubscriptionId id)
{
    defer(UnsubscribeMining { id });
}

void ChainServer::subscribe_account_event(SubscriptionRequest r, Address a)
{
    defer(SubscribeAccount { std::move(r), std::move(a) });
}
void ChainServer::subscribe_chain_event(SubscriptionRequest r)
{
    defer(SubscribeChain { std::move(r) });
}
void ChainServer::subscribe_minerdist_event(SubscriptionRequest r)
{
    defer(SubscribeMinerdist { std::move(r) });
}

void ChainServer::destroy_subscriptions(subscription_data_ptr p)
{
    defer(DestroySubscriptions { p });
}

void ChainServer::async_get_blocks(DescriptedBlockRange range, getBlocksCb&& callback)
{
    defer(GetBlocks { range, std::move(callback) });
}

void ChainServer::get_rollback_bounds(NonzeroHeight h, Descriptor d, GetRollbackBounds::callback_t cb)
{
    defer(GetRollbackBounds { h, d, std::move(cb) });
}

void ChainServer::get_block_market_history(NonzeroHeight h, Descriptor d, GetBlockMarketHistory::callback_t cb)
{
    defer(GetBlockMarketHistory { h, d, std::move(cb) });
}

void ChainServer::async_stage_request(stage_operation::Operation r)
{
    std::visit([&](auto&& req) {
        defer(std::move(req));
    },
        std::move(r));
}

void ChainServer::async_set_signed_checkpoint(SignedSnapshot ss)
{
    defer(SetSignedPin { ss });
}

void ChainServer::shutdown()
{
    std::unique_lock<std::mutex> ul(mutex);
    closing = true;
    haswork = true;
    cv.notify_one();
}

void ChainServer::wait_for_shutdown()
{
    if (worker.joinable())
        worker.join();
}

void ChainServer::work()
{
    Events tmp;
    while (true) {
        {
            std::unique_lock l(mutex);
            cv.wait(l, [&] { return haswork; });
            swap(tmp, events);
            haswork = false;
            if (closing)
                return;
        }
        state.garbage_collect();

        timing = timing_log().session();
        for (auto& e : tmp)
            dispatch_event(std::move(e));
        timing.reset();
        tmp.clear();
    }
}

void ChainServer::dispatch_mining_subscriptions()
{
    miningSubscriptions.dispatch([&](const Address& a) -> Result<ChainMiningTask> {
        return state.mining_task(a);
    });
}

TxHash ChainServer::append_gentx(const TransactionCreate& m)
{
    auto [log, txhash] = state.append_gentx(m);
    global().core->async_mempool_update(std::move(log));
    return txhash;
}

void ChainServer::on_chain_changed(StateUpdateWithAPIBlocks&& su)
{
    emit_chain_state_event();

    subscription_state::NewBlockInfo info {
        su.update.chainstateUpdate.rollback(),
        su.appendedBlocks
    };
    minerdistSubscriptions.on_chain_changed(state, info);
    addressSubscriptions.on_chain_changed(state, info);
    chainSubscriptions.on_chain_changed(state, info);

    // rollback api actions
    if (auto s { su.update.chainstateUpdate.rollback() })
        api::event::emit_rollback(s->length);

    // incremental block api actions
    for (auto& b : su.appendedBlocks)
        api::event::emit_block_append(std::move(b));

    global().core->async_state_update(std::move(su.update));
    dispatch_mining_subscriptions();
}

void ChainServer::append_mined(const chainserver::MiningAppend& a, bool verifyPOW)
{
    try {
        on_chain_changed(state.append_mined_block(a.block(), verifyPOW));
        if (verifyPOW) {
            spdlog::info("Accepted new block #{} (worker {})", state.chainlength().value(), a.worker());

        } else {
            spdlog::info("Faked new block #{}", state.chainlength().value());
        }
        dispatch_mining_subscriptions();
    } catch (Error err) {
        spdlog::info("Rejected new block #{} (worker {}): {}", (state.chainlength() + 1).value(),
            a.worker(), err.strerror());
        throw;
    }
}

void ChainServer::handle_event(LookupTxids&& e)
{
    auto t { timing->time("LookupTxIds") };
    std::vector<std::optional<TransactionMessage>> out;
    std::transform(e.txids.begin(), e.txids.end(), std::back_inserter(out),
        [&](auto txid) -> std::optional<TransactionMessage> {
            if (auto p { state.get_mempool_tx(txid) })
                return *p;
            return {};
        });
    e.callback(out);
}

void ChainServer::emit_chain_state_event()
{
    api::event::emit_chain_state({ .length { state.chainlength() },
        .target { state.get_headers().next_target() },
        .totalWork { state.get_headers().total_work() } });
}

template <typename T>
T noval_throw(std::optional<T>&& v)
{
    if (v)
        return *v;
    throw Error(ENOTFOUND);
}

template <typename T>
Result<T> noval_to_err(std::optional<T>&& v)
{
    if (v)
        return *v;
    return Error(ENOTFOUND);
}

void ChainServer::handle_event(SetSynced&& e)
{
    auto t { timing->time("SetSynced") };
    state.set_sync_state(e.synced);
}

void ChainServer::handle_event(SubscribeMining&& e)
{
    auto t { timing->time("SubscribeMining") };
    e.callback(state.mining_task(e.address));
    miningSubscriptions.subscribe(std::move(e));
}

void ChainServer::handle_event(UnsubscribeMining&& e)
{
    auto t { timing->time("UnsubscribeMining") };
    miningSubscriptions.unsubscribe(e.id);
}

void ChainServer::handle_event(GetBlocks&& e)
{
    auto t { timing->time("GetBlocks") };
    e.callback(state.get_body_data(e.range));
}

void ChainServer::handle_event(GetRollbackBounds&& e)
{
    spdlog::info("Handle GetRollbackBounds");
    auto d { state.get_descriptor() };
    if (e.descriptor != d)
        return; // ignore;
    // the descriptor is equal so we know that
    // the following assert succeeds
    auto rb { state.get_rollback_bounds(e.height) };
    assert(rb);
    e.callback(*rb);
}
void ChainServer::handle_event(GetBlockMarketHistory&& e)
{
    spdlog::info("Handle GetBlockMarketHistory {}", e.height.value());
    auto d { state.get_descriptor() };
    if (e.descriptor != d)
        return; // ignore;
    auto h { state.get_block_market_history(e.height) };
    assert(h);
    e.callback(*h);
}

void ChainServer::handle_event(stage_operation::StageSetOperation&& r)
{
    auto t { timing->time("StageSet") };
    global().core->async_stage_action(state.set_stage(std::move(r.headers)));
}

void ChainServer::handle_event(stage_operation::StageAddOperation&& r)
{
    auto t { timing->time("StageAdd") };
    auto res { state.add_stage(r.blocks, r.headers) };
    if (res.update)
        on_chain_changed(std::move(*res.update));
    if (res.rogueHeaderData)
        global().core->async_push_rogue(*res.rogueHeaderData);
    global().core->async_stage_action(res.status);
}

auto ChainServer::handle_api(chainserver::PutMempool&& e) -> api::TransactionAddResult
{
    auto [log, txhash] = state.append_gentx(e.message());
    global().core->async_mempool_update(std::move(log));
    return { txhash };
}

void ChainServer::fake_mine(const Address& address)
{
    auto b { state.mining_task(address)->block };
    BlockData bd(b.height, b.header, b.body.data);
    auto reparsedBlock { std::move(bd).parse_throw() };
    return append_mined({ std::move(reparsedBlock), "fakemine" }, false);
}

void ChainServer::api_rollback()
{
    if (state.chainlength() == 0) {
        spdlog::info("API can't roll back empty chain.");
        return;
    }
    Height h(state.chainlength() - 1);
    auto res { state.api_rollback(h) };
    assert(res);
    on_chain_changed(std::move(*res));
}

// void ChainServer::handle_event(PutMempool&& e)
// {
//     auto t { timing->time("PutMempool") };
//     try {
//         auto txhash { append_gentx(std::move(e.m)) };
//         e.callback(txhash);
//     } catch (Error err) {
//         e.callback(err);
//     }
// }

void ChainServer::handle_event(PutMempoolBatch&& mb)
{
    auto t { timing->time("PutMempoolBatch") };
    auto [_, log] { state.insert_txs(std::move(mb.txs)) };
    // LATER: introduce some logic to ban
    // peers who sent such bad transactions
    global().core->async_mempool_update(std::move(log));
}

void ChainServer::handle_event(SetSignedPin&& e)
{
    auto t { timing->time("SetSignedPin") };
    auto res { state.apply_signed_snapshot(std::move(e.ss)) };
    if (res)
        on_chain_changed(std::move(*res));
}
void ChainServer::handle_event(SubscribeAccount&& s)
{
    addressSubscriptions.handle_subscription(std::move(s.req), state, s.addr);
}

void ChainServer::handle_event(SubscribeChain&& s)
{
    chainSubscriptions.handle_subscription(std::move(s), state);
}
void ChainServer::handle_event(SubscribeMinerdist&& s)
{
    minerdistSubscriptions.handle_subscription(std::move(s), state);
}

void ChainServer::handle_event(DestroySubscriptions&& s)
{
    addressSubscriptions.erase_all(s.p);
    chainSubscriptions.erase_all(s.p);
}
