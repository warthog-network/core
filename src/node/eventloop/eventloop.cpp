#include "eventloop.hpp"
#include "../asyncio/connection.hpp"
#include "address_manager/address_manager_impl.hpp"
#include "api/types/all.hpp"
#include "block/chain/header_chain.hpp"
#include "block/header/batch.hpp"
#include "block/header/view.hpp"
#include "chainserver/server.hpp"
#include "global/globals.hpp"
#include "mempool/order_key.hpp"
#include "peerserver/peerserver.hpp"
#include "spdlog/spdlog.h"
#include "types/conref_impl.hpp"
#include "types/peer_requests.hpp"
#include <algorithm>
#include <future>
#include <iostream>
#include <sstream>

using namespace std::chrono_literals;
Eventloop::Eventloop(PeerServer& ps, ChainServer& cs, const Config& config)
    : stateServer(cs)
    , chains(cs.get_chainstate())
    , mempool(false)
    , connections(ps, config.peers.connect)
    // , signedSnapshot(chains.signed_snapshot())
    , headerDownload(chains, consensus().total_work())
    , blockDownload(*this)
{
    auto& ss = consensus().get_signed_snapshot();
    spdlog::info("Chain info: length {}, work {}, ", consensus().headers().length().value(), consensus().total_work().getdouble());
    if (ss.has_value()) {
        bool valid { ss->compatible(consensus().headers()) };
        spdlog::info("Chain snapshot is {}: priority {}, height {}", (valid ? "valid" : "invalid"), ss->priority.importance, ss->height().value());
    } else {
        spdlog::info("Chain snapshot not present");
    }

    update_wakeup();
}

Eventloop::~Eventloop()
{
    if (worker.joinable()) {
        worker.join(); // worker should already have terminated
    }
}
void Eventloop::start_async_loop()
{
    if (!worker.joinable()) {
        worker = std::thread(&Eventloop::loop, this);
    }
}

bool Eventloop::defer(Event e)
{
    std::unique_lock<std::mutex> l(mutex);
    if (closeReason)
        return false;
    haswork = true;
    events.push(std::move(e));
    cv.notify_one();
    return true;
}
bool Eventloop::async_process(std::shared_ptr<Connection> c)
{
    return defer(OnProcessConnection { std::move(c) });
}
void Eventloop::async_shutdown(int32_t reason)
{
    std::unique_lock<std::mutex> l(mutex);
    haswork = true;
    closeReason = reason;
    cv.notify_one();
}

void Eventloop::async_report_failed_outbound(EndpointAddress a)
{
    defer(OnFailedAddressEvent { a });
}

void Eventloop::async_erase(std::shared_ptr<Connection> c, int32_t error)
{
    if (!defer(OnRelease { std::move(c), error })) {
    }
}

void Eventloop::async_state_update(StateUpdate&& s)
{
    defer(std::move(s));
}

void Eventloop::async_mempool_update(mempool::Log&& s)
{
    defer(std::move(s));
}

void Eventloop::api_get_peers(PeersCb&& cb)
{
    defer(std::move(cb));
}

void Eventloop::api_get_synced(SyncedCb&& cb)
{
    defer(std::move(cb));
}

void Eventloop::api_inspect(InspectorCb&& cb)
{
    defer(std::move(cb));
}
void Eventloop::api_get_hashrate(HashrateCb&& cb, size_t n)
{
    defer(GetHashrate { std::move(cb), n });
}

void Eventloop::api_get_hashrate_chart(NonzeroHeight from, NonzeroHeight to, size_t window, HashrateChartCb&& cb)
{
    defer(GetHashrateChart { std::move(cb), from, to, window });
}

void Eventloop::async_forward_blockrep(uint64_t conId, std::vector<BodyContainer>&& blocks)
{
    defer(OnForwardBlockrep { conId, std::move(blocks) });
}

bool Eventloop::has_work()
{
    auto now = std::chrono::steady_clock::now();
    return haswork || (now > timer.next());
}

void Eventloop::loop()
{
    connect_scheduled();
    while (true) {
        {
            std::unique_lock<std::mutex> ul(mutex);
            while (!has_work()) {
                auto until = timer.next();
                using namespace std::chrono;
                auto count = duration_cast<seconds>(until.time_since_epoch()).count();
                spdlog::debug("Eventloop wait until {} ms", count);
                cv.wait_until(ul, until);
            }
            haswork = false;
        }
        work();
        if (check_shutdown()) {
            return;
        }
    }
}

void Eventloop::work()
{
    decltype(events) tmp;
    std::vector<Timer::Event> expired;
    {
        std::unique_lock<std::mutex> l(mutex);
        std::swap(tmp, events);
        expired = timer.pop_expired();
    }
    // process expired
    for (auto& data : expired) {
        std::visit([&](auto&& e) {
            handle_timeout(std::move(e));
        },
            data);
    }
    while (!tmp.empty()) {
        std::visit([&](auto&& e) {
            handle_event(std::move(e));
        },
            tmp.front());
        tmp.pop();
    }
    connections.garbage_collect();
    update_sync_state();
}

bool Eventloop::check_shutdown()
{

    {
        std::unique_lock<std::mutex> l(mutex);
        if (closeReason == 0)
            return false;
    }

    spdlog::debug("Shutdown connectionManager.size() {}", connections.size());
    for (auto cr : connections.all()) {
        if (cr->erased())
            continue;
        erase(cr, closeReason);
    }

    stateServer.shutdown_join();
    return true;
}

void Eventloop::handle_event(OnRelease&& m)
{
    bool erased { m.c->eventloop_erased };
    bool registered { m.c->eventloop_registered };
    if ((!erased) && registered)
        erase(m.c->dataiter, m.error);
}

void Eventloop::handle_event(OnProcessConnection&& m)
{
    process_connection(std::move(m.c));
}

void Eventloop::handle_event(StateUpdate&& e)
{
    // mempool
    mempool.apply_log(std::move(e.mempoolUpdate));

    // header chain
    std::visit([&](auto&& action) {
        update_chain(std::move(action));
    },
        std::move(e.chainstateUpdate));
}

void Eventloop::update_chain(Append&& m)
{
    const auto msg = chains.update_consensus(std::move(m));
    log_chain_length();
    for (auto c : connections.all()) {
        try {
            if (c.initialized())
                c->chain.on_consensus_append(chains);
        } catch (ChainError e) {
            close(c, e);
        }
        c.send(msg);
    }
    //  broadcast new snapshot
    for (auto c : connections.initialized())
        consider_send_snapshot(c);

    coordinate_sync();
    do_requests();
}

void Eventloop::update_chain(Fork&& fork)
{
    const auto msg { chains.update_consensus(std::move(fork)) };
    log_chain_length();
    for (auto c : connections.all()) {
        try {
            if (c.initialized())
                c->chain.on_consensus_fork(msg.forkHeight, chains);
            c.send(msg);
        } catch (ChainError e) {
            close(c, e);
        }
    }

    coordinate_sync();
    do_requests();
}

void Eventloop::update_chain(RollbackData&& rd)
{
    // update consensus
    const auto msg { chains.update_consensus(rd) };
    if (msg) {
        log_chain_length();
        for (auto c : connections.all()) {
            if (c.initialized())
                c->chain.on_consensus_shrink(chains);
            c.send(*msg);
        }
    }
    headerDownload.on_signed_snapshot_update();

    // update stage
    if (!rd.signedSnapshot.compatible(chains.stage_headers())) {
        blockDownload.reset();
    }

    //  broadcast new snapshot
    for (auto c : connections.initialized())
        consider_send_snapshot(c);

    coordinate_sync();
    syncdebug_log().info("init blockdownload update_chain");
    initialize_block_download();
    do_requests();
}

void Eventloop::coordinate_sync()
{
    auto cons { chains.consensus_state().headers().total_work() };
    auto blk { blockDownload.get_reachable_totalwork() };
    auto max { std::max(cons, blk) };
    for (auto& co : headerDownload.set_min_worksum(max)) {
        close(co);
    };
    blockDownload.set_min_worksum(cons);
}

void Eventloop::initialize_block_download()
{
    if (auto d { headerDownload.pop_data() }; d) {
        auto offenders = blockDownload.init(std::move(*d));
        for (ChainOffender& o : offenders) {
            close(o);
        };
        process_blockdownload_stage();
    }
}

ForkHeight Eventloop::set_stage_headers(Headerchain&& hc)
{
    spdlog::info("Syncing... (height {} of {})", chains.consensus_length().value(), hc.length().value());
    auto forkHeight { chains.update_stage(std::move(hc)) };
    return forkHeight;
}

void Eventloop::log_chain_length()
{
    auto synced { chains.consensus_length().value() };
    auto total { chains.stage_headers().length().value() };
    if (synced < total)
        spdlog::info("Syncing... (height {} of {})", synced, total);
    else if (synced == total)
        spdlog::info("Synced. (height {}).", synced);
}

void Eventloop::handle_event(PeersCb&& cb)
{
    std::vector<API::Peerinfo> out;
    for (auto cr : connections.initialized()) {
        out.push_back({
            .endpoint { cr->c->peer_address() },
            .initialized = cr.initialized(),
            .chainstate = cr.chain(),
            .theirSnapshotPriority = cr->theirSnapshotPriority,
            .acknowledgedSnapshotPriority = cr->acknowledgedSnapshotPriority,
            .since = cr->c->connected_since,
        });
    }
    cb(out);
}

void Eventloop::handle_event(SyncedCb&& cb)
{
    cb(!blockDownload.is_active());
}

void Eventloop::handle_event(SignedSnapshotCb&& cb)
{
    if (signed_snapshot()) {
        cb(*signed_snapshot());
    } else {
        cb(tl::make_unexpected(ENOTFOUND));
    }
}

void Eventloop::handle_event(stage_operation::Result&& r)
{
    auto offenders { blockDownload.on_stage_result(std::move(r)) };
    for (auto& o : offenders)
        close(o);
    process_blockdownload_stage();
    do_requests();
}

void Eventloop::handle_event(OnForwardBlockrep&& m)
{
    if (auto cr { connections.find(m.conId) }; cr) {
        BlockrepMsg msg(cr->lastNonce, std::move(m.blocks));
        cr.send(msg);
    }
}

void Eventloop::handle_event(OnFailedAddressEvent&& e)
{
    if (connections.on_failed_outbound(e.a))
        update_wakeup();
    connect_scheduled();
}

void Eventloop::handle_event(InspectorCb&& cb)
{
    cb(*this);
}

void Eventloop::handle_event(GetHashrate&& e)
{
    e.cb(API::HashrateInfo {
        .nBlocks = e.n,
        .estimate = consensus().headers().hashrate(e.n) });
}

void Eventloop::handle_event(GetHashrateChart&& e)
{
    e.cb(consensus().headers().hashrate_chart(e.from, e.to, e.window));
}

void Eventloop::handle_event(OnPinAddress&& e)
{
    connections.pin(e.a);
    update_wakeup();
}
void Eventloop::handle_event(OnUnpinAddress&& e)
{
    connections.unpin(e.a);
    update_wakeup();
}
void Eventloop::handle_event(mempool::Log&& log)
{
    mempool.apply_log(log);

    // build vector of mempool entries
    std::vector<mempool::Entry> entries;
    for (auto& action : log) {
        if (std::holds_alternative<mempool::Put>(action)) {
            entries.push_back(std::get<mempool::Put>(action).entry);
        }
    }
    std::sort(entries.begin(), entries.end(),
        [](const mempool::Entry& e1, const mempool::Entry& e2) {
            if (e1.second.transactionHeight == e2.second.transactionHeight)
                return e1.first < e2.first;
            return e1.second.transactionHeight < e2.second.transactionHeight;
        });

    // construct subscription bounds per connection
    auto eiter = entries.begin();
    std::vector<std::pair<decltype(entries)::iterator, Conref>> bounds;
    auto miter = mempoolSubscriptions.cbegin();
    if (mempoolSubscriptions.size() > 0) {
        while (eiter != entries.end()) {
            while (!(eiter->second.transactionHeight < miter->first.transactionHeight)) {
                bounds.push_back({ eiter, miter->second });
                ++miter;
                if (miter == mempoolSubscriptions.cend())
                    goto finished;
            }
            ++eiter;
        }
        while (miter != mempoolSubscriptions.end()) {
            bounds.push_back({ entries.end(), miter->second });
            ++miter;
        }
    finished:

        // send subscription individually
        for (auto& [end, cr] : bounds) {
            cr.send(TxnotifyMsg::direct_send(entries.begin(), end));
        }
    }
}

void Eventloop::erase(Conref c, int32_t error)
{
    if (c->c->eventloop_erased)
        return;
    c->c->eventloop_erased = true;
    bool doRequests = false;
    c.job().unref_active_requests(activeRequests);
    if (c.ping().has_timerref(timer))
        timer.cancel(c.ping().timer());
    if (c.job().has_timerref(timer)) {
        timer.cancel(c.job().timer());
    }
    assert(c.valid());
    if (headerDownload.erase(c) && !closeReason) {
        spdlog::info("Connected to {} peers (disconnected {}, v{} reason: {})",
            headerDownload.size(), c->c->peer_endpoint().to_string(), c->c->peer_version().to_string(), Error(error).err_name());
    }
    if (blockDownload.erase(c))
        coordinate_sync();
    if (connections.erase(c.iterator()))
        update_wakeup();
    if (doRequests) {
        do_requests();
    }
}

bool Eventloop::insert(Conref c, const InitMsg& data)
{
    bool doRequests = true;

    c->chain.initialize(data, chains);
    for (auto& co : headerDownload.insert(c)) {
        close(co);
    };
    blockDownload.insert(c);
    spdlog::info("Connected to {} peers (connected {}, v{})", headerDownload.size(), c->c->peer_address().to_string(), c->c->peer_version().to_string());
    send_ping_await_pong(c);
    // LATER: return whether doRequests is necessary;
    return doRequests;
}

void Eventloop::close(Conref cr, Error reason)
{
    if (!cr->c->eventloop_registered)
        return;
    cr->c->async_close(reason.e);
    erase(cr, reason.e); // do not consider this connection anymore
}

void Eventloop::close_by_id(uint64_t conId, int32_t reason)
{
    if (auto cr { connections.find(conId) }; cr)
        close(cr, reason);
    // LATER: report offense to peerserver
}

void Eventloop::close(const ChainOffender& o)
{
    assert(o);
    if (auto cr { connections.find(o.conId) }; cr) {
        close(cr, o.e);
    } else {
        report(o);
    }
}
void Eventloop::close(Conref cr, ChainError e)
{
    assert(e);
    close(cr, e.e);
}

void Eventloop::process_connection(std::shared_ptr<Connection> c)
{
    if (c->eventloop_erased)
        return;
    if (!c->eventloop_registered) {
        // fresh connection

        c->eventloop_registered = true;
        auto [error, cr] = connections.insert(
            c, headerDownload, blockDownload, timer);
        update_wakeup();
        connect_scheduled();
        if (error != 0) {
            c->async_close(error);
            c->eventloop_erased = true;
            return;
        }
        if (config().node.logCommunication)
            spdlog::info("{} connected", c->to_string());

        send_init(cr);
    }
    auto messages = c->extractMessages();
    Conref cr { c->dataiter };
    for (auto& msg : messages) {
        try {
            dispatch_message(cr, msg);
            // active
        } catch (Error e) {
            close(cr, e.e);
            do_requests();
            break;
        }
        if (c->eventloop_erased) {
            return;
        }
    }
}

void Eventloop::send_ping_await_pong(Conref c)
{
    if (config().node.logCommunication)
        spdlog::info("{} Sending Ping", c.str());
    auto t = timer.insert(
        (config().localDebug ? 10min : 1min),
        Timer::CloseNoPong { c.id() });
    PingMsg p(signed_snapshot() ? signed_snapshot()->priority : SignedSnapshot::Priority {});
    c.ping().await_pong(p, t);
    c.send(p);
}

void Eventloop::received_pong_sleep_ping(Conref c)
{
    auto t = timer.insert(10s, Timer::SendPing { c.id() });
    auto old_t = c.ping().sleep(t);
    cancel_timer(old_t);
}

void Eventloop::update_wakeup()
{
    auto wakeupTime = connections.wakeup_time();
    if (wakeupTimer && (wakeupTime == (*wakeupTimer)->first))
        return; // no change
    if (wakeupTimer) {
        timer.cancel(*wakeupTimer);
        wakeupTimer.reset();
    }
    if (!wakeupTime)
        return;
    wakeupTimer = timer.insert(*wakeupTime, Timer::Connect {});
}

void Eventloop::send_requests(Conref cr, const std::vector<Request>& requests)
{
    for (auto& r : requests) {
        std::visit([&](auto& req) {
            send_request(cr, req);
        },
            r);
    }
}

void Eventloop::do_requests()
{
    while (true) {
        auto offenders { headerDownload.do_header_requests(sender()) };
        if (offenders.size() == 0)
            break;
        for (auto& o : offenders)
            close(o);
    };
    blockDownload.do_block_requests(sender());
    headerDownload.do_probe_requests(sender());
    blockDownload.do_probe_requests(sender());
}

template <typename T>
void Eventloop::send_request(Conref c, const T& req)
{
    if (config().node.logCommunication)
        spdlog::info("{} send {}", c.str(), req.log_str());
    auto t = timer.insert(req.expiry_time, Timer::Expire { c.id() });
    c.job().assign(t, timer, req);
    if (req.isActiveRequest) {
        assert(activeRequests < maxRequests);
        activeRequests += 1;
    }
    c.send(req);
}

void Eventloop::send_init(Conref cr)
{
    cr.send(InitMsg::serialize_chainstate(consensus()));
}

template <typename T>
requires std::derived_from<T, Timer::WithConnecitonId>
void Eventloop::handle_timeout(T&& t)
{
    Conref cr { connections.find(t.conId) };
    if (cr) {
        handle_connection_timeout(cr, std::move(t));
    }
}
void Eventloop::handle_connection_timeout(Conref cr, Timer::CloseNoReply&&)
{
    cr.job().reset_expired(timer);
    close(cr, ETIMEOUT);
}
void Eventloop::handle_connection_timeout(Conref cr, Timer::CloseNoPong&&)
{
    cr.ping().reset_expired(timer);
    close(cr, ETIMEOUT);
}

void Eventloop::handle_connection_timeout(Conref cr, Timer::SendPing&&)
{
    cr.ping().timer_expired(timer);
    return send_ping_await_pong(cr);
}
void Eventloop::handle_connection_timeout(Conref cr, Timer::Expire&&)
{
    cr.job().restart_expired(timer.insert(
                                 (config().localDebug ? 10min : 2min), Timer::CloseNoReply { cr.id() }),
        timer);
    assert(!cr.job().data_v.valueless_by_exception());
    std::visit(
        [&]<typename T>(T& v) {
            if constexpr (std::is_base_of_v<IsRequest, T>) {
                v.unref_active_requests(activeRequests);
                on_request_expired(cr, v);
            } else {
                assert(false);
            }
        },
        cr.job().data_v);
    assert(!cr.job().data_v.valueless_by_exception());
}

void Eventloop::on_request_expired(Conref cr, const Proberequest&)
{
    headerDownload.on_probe_request_expire(cr);
    blockDownload.on_probe_expire(cr);
    do_requests();
}

void Eventloop::on_request_expired(Conref cr, const Batchrequest& req)
{
    headerDownload.on_request_expire(cr, req);
    do_requests();
}

void Eventloop::on_request_expired(Conref cr, const Blockrequest&)
{
    blockDownload.on_blockreq_expire(cr);
    do_requests();
}

void Eventloop::handle_timeout(Timer::Connect&&)
{
    wakeupTimer.reset();
    auto connect = connections.pop_connect();
    for (auto& a : connect) {
        global().pcm->async_connect(a);
    }
    update_wakeup();
}

void Eventloop::dispatch_message(Conref cr, Rcvbuffer& msg)
{
    using namespace messages;
    if (msg.verify() == false)
        throw Error(ECHECKSUM);

    auto m = msg.parse();
    // first message must be of type INIT (is_init() is only initially true)
    if (cr.job().awaiting_init()) {
        if (!std::holds_alternative<InitMsg>(m)) {
            auto msgcode { std::visit([](auto a) { return a.msgcode; }, m) };
            spdlog::error("Debug info: Expected init message from {} but got message of type {}", cr->c->peer_address().to_string(), msgcode);
            throw Error(ENOINIT);
        }
    } else {
        if (std::holds_alternative<InitMsg>(m))
            throw Error(EINVINIT);
    }

    std::visit([&](auto&& e) {
        handle_msg(cr, std::move(e));
    },
        m);
}

void Eventloop::handle_msg(Conref cr, InitMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle init: height {}, work {}", cr.str(), m.chainLength.value(), m.worksum.getdouble());
    cr.job().reset_notexpired<AwaitInit>(timer);
    if (insert(cr, m))
        do_requests();
}

void Eventloop::handle_msg(Conref cr, AppendMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle append", cr.str());
    cr->chain.on_peer_append(m, chains);
    for (auto& co : headerDownload.on_append(cr)) {
        close(co);
    };
    blockDownload.on_append(cr);
    do_requests();
}

void Eventloop::handle_msg(Conref c, SignedPinRollbackMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle rollback ", c.str());
    verify_rollback(c, m);
    c->chain.on_peer_shrink(m, chains);
    for (auto& co : headerDownload.on_rollback(c)) {
        close(co);
    };
    blockDownload.on_rollback(c);
    do_requests();
}

void Eventloop::handle_msg(Conref c, ForkMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle fork", c.str());
    c->chain.on_peer_fork(m, chains);
    for (auto& co : headerDownload.on_fork(c)) {
        close(co);
    };
    blockDownload.on_fork(c);
    do_requests();
}

void Eventloop::handle_msg(Conref c, PingMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle ping", c.str());
    size_t nAddr { std::min(uint16_t(20), m.maxAddresses) };
    auto addresses = connections.sample_verified(nAddr);
    c->ratelimit.ping();
    PongMsg msg(m.nonce, std::move(addresses), mempool.sample(m.maxTransactions));
    spdlog::debug("{} Sending {} addresses", c.str(), msg.addresses.size());
    if (c->theirSnapshotPriority < m.sp)
        c->theirSnapshotPriority = m.sp;
    c.send(msg);
    consider_send_snapshot(c);
}

void Eventloop::handle_msg(Conref cr, PongMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle pong", cr.str());
    auto& pingMsg = cr.ping().check(m);
    received_pong_sleep_ping(cr);
    spdlog::debug("{} Received {} addresses", cr.str(), m.addresses.size());
    connections.queue_verification(m.addresses);
    spdlog::debug("{} Got {} transaction Ids in pong message", cr.str(), m.txids.size());

    // update acknowledged priority
    if (cr->acknowledgedSnapshotPriority < pingMsg.sp) {
        cr->acknowledgedSnapshotPriority = pingMsg.sp;
    }

    // request new txids
    auto txids = mempool.filter_new(m.txids);
    if (txids.size() > 0)
        cr.send(TxreqMsg(txids));

    // connect scheduled (in case new addresses were added)
    connect_scheduled();
}

void Eventloop::handle_msg(Conref cr, BatchreqMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle batchreq [{},{}]", cr.str(), m.selector.startHeight.value(), (m.selector.startHeight + m.selector.length - 1).value());
    auto& s = m.selector;
    Batch batch = [&]() {
        if (s.descriptor == consensus().descriptor()) {
            return consensus().headers().get_headers(s.startHeight, s.end());
        } else {
            return stateServer.get_headers(s);
        }
    }();

    BatchrepMsg rep(m.nonce, std::move(batch));
    rep.nonce = m.nonce;
    cr.send(rep);
}

void Eventloop::handle_msg(Conref cr, BatchrepMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle_batchrep", cr.str());
    // check nonce and get associated data
    auto req = cr.job().pop_req(m, timer, activeRequests);

    // save batch
    if (m.batch.size() < req.minReturn || m.batch.size() > req.max_return()) {
        close(ChainOffender(EBATCHSIZE3, req.selector.startHeight, cr.id()));
        return;
    }
    auto offenders = headerDownload.on_response(cr, std::move(req), std::move(m.batch));
    for (auto& o : offenders) {
        close(o);
    }

    syncdebug_log().info("init blockdownload batch_rep");
    initialize_block_download();

    // assign work
    do_requests();
}

void Eventloop::handle_msg(Conref cr, ProbereqMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle_probereq d:{}, h:{}", cr.str(), m.descriptor.value(), m.height.value());
    ProberepMsg rep(m.nonce, consensus().descriptor().value());
    auto h = consensus().headers().get_header(m.height);
    if (h)
        rep.current = *h;
    if (m.descriptor == consensus().descriptor()) {
        auto h = consensus().headers().get_header(m.height);
        if (h)
            rep.requested = h;
    } else {
        auto h = stateServer.get_descriptor_header(m.descriptor, m.height);
        if (h)
            rep.requested = *h;
    }
    cr.send(rep);
}

void Eventloop::handle_msg(Conref cr, ProberepMsg&& rep)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle_proberep", cr.str());
    auto req = cr.job().pop_req(rep, timer, activeRequests);
    if (!rep.requested.has_value() && !req.descripted->expired()) {
        throw ChainError { EEMPTY, req.height };
    }
    cr->chain.on_proberep(req, rep, chains);
    headerDownload.on_proberep(cr, req, rep);
    blockDownload.on_probe_reply(cr, req, rep);
    do_requests();
}

void Eventloop::handle_msg(Conref cr, BlockreqMsg&& m)
{
    using namespace std::placeholders;
    BlockreqMsg req(m);
    if (config().node.logCommunication)
        spdlog::info("{} handle_blockreq [{},{}]", cr.str(), req.range.lower.value(), req.range.upper.value());
    cr->lastNonce = req.nonce;
    stateServer.async_get_blocks(req.range, std::bind(&Eventloop::async_forward_blockrep, this, cr.id(), _1));
}

void Eventloop::handle_msg(Conref cr, BlockrepMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle blockrep", cr.str());
    auto req = cr.job().pop_req(m, timer, activeRequests);

    try {
        blockDownload.on_blockreq_reply(cr, std::move(m), req);
        process_blockdownload_stage();
    } catch (Error e) {
        close(cr, e);
    }
    do_requests();
}

void Eventloop::handle_msg(Conref cr, TxnotifyMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle Txnotify", cr.str());
    auto txids = mempool.filter_new(m.txids);
    if (txids.size() > 0)
        cr.send(TxreqMsg(txids));
    do_requests();
}

void Eventloop::handle_msg(Conref cr, TxreqMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle TxreqMsg", cr.str());
    std::vector<std::optional<TransferTxExchangeMessage>> out;
    for (auto& e : m.txids) {
        out.push_back(mempool[e]);
    }
    if (out.size() > 0)
        cr.send(TxrepMsg(out));
}

void Eventloop::handle_msg(Conref cr, TxrepMsg&& m)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle TxrepMsg", cr.str());
    std::vector<TransferTxExchangeMessage> txs;
    for (auto& o : m.txs) {
        if (o)
            txs.push_back(*o);
    };
    stateServer.async_put_mempool(std::move(txs));
    do_requests();
}

void Eventloop::handle_msg(Conref cr, LeaderMsg&& msg)
{
    if (config().node.logCommunication)
        spdlog::info("{} handle LeaderMsg", cr.str());
    // ban if necessary
    if (msg.signedSnapshot.priority <= cr->acknowledgedSnapshotPriority) {
        close(cr, ELOWPRIORITY);
        return;
    }

    // update knowledge about sender
    cr->acknowledgedSnapshotPriority = msg.signedSnapshot.priority;
    if (cr->theirSnapshotPriority < msg.signedSnapshot.priority) {
        cr->theirSnapshotPriority = msg.signedSnapshot.priority;
    }

    stateServer.async_set_signed_checkpoint(msg.signedSnapshot);
}

void Eventloop::consider_send_snapshot(Conref c)
{
    // spdlog::info("
    if (signed_snapshot().has_value()) {
        auto theirPriority = c->theirSnapshotPriority;
        auto snapshotPriority = signed_snapshot()->priority;
        if (theirPriority < snapshotPriority) {
            c.send(LeaderMsg(*signed_snapshot()));
            c->theirSnapshotPriority = signed_snapshot()->priority;
        }
    }
}

void Eventloop::process_blockdownload_stage()
{
    auto r { blockDownload.pop_stage() };
    if (r)
        stateServer.async_stage_request(*r);
}

void Eventloop::async_stage_action(stage_operation::Result r)
{
    defer(std::move(r));
}

void Eventloop::cancel_timer(Timer::iterator& ref)
{
    timer.cancel(ref);
    ref = timer.end();
}

void Eventloop::connect_scheduled()
{
    auto as = connections.pop_connect();
    for (auto& a : as) {
        global().pcm->async_connect(a);
    }
}

void Eventloop::verify_rollback(Conref cr, const SignedPinRollbackMsg& m)
{
    if (cr.chain().descripted()->chain_length() <= m.shrinkLength)
        throw Error(EBADROLLBACKLEN);
    auto& ss = m.signedSnapshot;
    if (cr.chain().stage_fork_range().lower() > ss.priority.height) {
        if (ss.compatible(chains.stage_headers()))
            throw Error(EBADROLLBACK);
    } else if (cr.chain().consensus_fork_range().lower() > ss.priority.height) {
        if (ss.compatible(chains.consensus_state().headers()))
            throw Error(EBADROLLBACK);
    }
}

void Eventloop::update_sync_state()
{
    syncState.set_has_connections(!connections.initialized().empty());
    syncState.set_block_download(blockDownload.is_active());
    syncState.set_header_download(headerDownload.is_active());
    if (auto c { syncState.detect_change() }; c) {
        global().pcs->async_set_synced(c.value());
    }
}
