#include "server.hpp"
#include "api/events/emit.hpp"
#include "api/events/subscription.hpp"
#include "api/types/all.hpp"
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

Batch ChainServer::get_headers(BatchSelector selector)
{
    return state.get_headers_concurrent(selector);
}

wrt::optional<HeaderView> ChainServer::get_descriptor_header(Descriptor descriptor, Height height)
{
    return state.get_header_concurrent(descriptor, height);
}

ConsensusSlave ChainServer::get_chainstate()
{
    return state.get_chainstate_concurrent();
}

ChainServer::ChainServer(ChainDB& db, BatchRegistry& br, wrt::optional<SnapshotSigner> snapshotSigner, Token)
    : db(db)
    , batchRegistry(br)
    , state(db, br, snapshotSigner)
{
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

void ChainServer::async_stage_request(stage_operation::Operation r)
{
    std::visit([&](auto req) {
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

void ChainServer::workerfun()
{
    // initialization
    while (true) {
        {
            std::unique_lock<std::mutex> ul(mutex);
            while (!haswork) {
                cv.wait(ul);
            }
        }
        haswork = false;
        if (closing)
            break;
        state.garbage_collect();

        { // work
            decltype(events) tmpq;
            {
                std::unique_lock<std::mutex> ul(mutex);
                std::swap(tmpq, events);
            }
            timing = timing_log().session();
            while (!tmpq.empty()) {
                dispatch_event(std::move(tmpq.front()));
                tmpq.pop();
            }
            timing.reset();
        }
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
    std::vector<wrt::optional<TransactionMessage>> out;
    std::transform(e.txids.begin(), e.txids.end(), std::back_inserter(out),
        [&](auto txid) { return state.get_mempool_tx(txid); });
    e.callback(out);
}

void ChainServer::emit_chain_state_event()
{
    api::event::emit_chain_state({ .length { state.chainlength() },
        .target { state.get_headers().next_target() },
        .totalWork { state.get_headers().total_work() } });
}

template <typename T>
T noval_throw(wrt::optional<T>&& v)
{
    if (v)
        return *v;
    throw Error(ENOTFOUND);
}

template <typename T>
Result<T> noval_to_err(wrt::optional<T>&& v)
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

auto ChainServer::handle_api(chainserver::PutMempool&& e) -> TxHash
{
    auto [log, txhash] = state.append_gentx(e.message());
    global().core->async_mempool_update(std::move(log));
    return txhash;
}

void ChainServer::fake_mine(const Address& address)
{
    auto b { state.mining_task(address)->block };
    Block reparsedBlock { b.height, b.header, Body::parse_throw(std::move(b.body.data), b.height) };
    return append_mined({ reparsedBlock, "fakemine" }, false);
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
    auto [_, log] { state.insert_txs(mb.txs) };
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
