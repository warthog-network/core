#include "eventloop.hpp"
#include "address_manager/address_manager_impl.hpp"
#include "api/events/emit.hpp"
#include "api/events/subscription.hpp"
#include "api/types/shared.hpp"
#include "block/chain/header_chain.hpp"
#include "block/header/batch.hpp"
#include "block/header/view.hpp"
#include "chainserver/server.hpp"
#include "communication/buffers/sndbuffer.hpp"
#include "communication/messages_impl.hpp"
#include "config/browser.hpp"
#include "connection_inserter.hpp"
#include "eventloop/connection_inserter.hpp"
#include "general/logging.hpp"
#include "global/globals.hpp"
#include "mempool/order_key.hpp"
#include "peerserver/peerserver.hpp"
#include "spdlog/spdlog.h"
#include "sync/sync_state.hpp"
#include "transport/tcp/conman.hpp"
#include "transport/webrtc/rtc_connection.hxx"
#include "transport/webrtc/sdp_util.hpp"
#include "types/conref_impl.hpp"
#include "types/peer_requests.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <random>

using SubscriptionEvent = subscription::events::Event;

using namespace std::chrono_literals;
namespace TimerEvent = eventloop::timer_events;
namespace {
bool rtc_enabled(Conref c)
{
    return config().node.enableWebRTC && c->rtcState.enanabled;
}
void throw_if_rtc_disabled(Conref c)
{
    if (!rtc_enabled(c))
        throw Error(ERTCDISABLED);
}

}

Eventloop::Eventloop(Token, PeerServer& ps, ChainServer& cs, const ConfigParams& cfg)
    : startedAt(std::chrono::steady_clock::now())
    , stateServer(cs)
    , chains(cs.get_chainstate())
    , mempool()
#ifndef DISABLE_LIBUV
    , connections({ ps, cfg.peers.connect })
#else
    , connections({ ps, cfg.peers.connect })
#endif
    , headerDownload(chains, consensus().total_work())
    , blockDownload(*this)
{
    spdlog::info("Experimental WebRTC support {}", config().node.enableWebRTC ? "enabled" : "disabled");
    auto& ss = consensus().get_signed_snapshot();
    spdlog::info("Chain info: length {}, work {}, ", consensus().headers().length().value(), consensus().total_work().getdouble());
    if (ss.has_value()) {
        bool valid { ss->compatible(consensus().headers()) };
        spdlog::info("Chain snapshot is {}: priority {}, height {}", (valid ? "valid" : "invalid"), ss->priority.importance, ss->height().value());
    } else {
        spdlog::info("Chain snapshot not present");
    }
}
std::shared_ptr<Eventloop> Eventloop::create(PeerServer& ps, ChainServer& ss, const ConfigParams& config)
{
    return std::make_shared<Eventloop>(Token {}, ps, ss, config);
}

Eventloop::~Eventloop()
{
    wait_for_shutdown();
}
void Eventloop::start()
{
    assert(!worker.joinable());
    worker = std::thread(&Eventloop::loop, this);
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

bool Eventloop::async_process(std::shared_ptr<ConnectionBase> c)
{
    return defer(OnProcessConnection { std::move(c) });
}

bool Eventloop::on_handshake_completed(ConnectionBase::ConnectionVariant con)
{
    return defer(OnHandshakeCompleted(std::move(con)));
}

void Eventloop::shutdown(Error reason)
{
    std::unique_lock<std::mutex> l(mutex);
    haswork = true;
    closeReason = reason;
    cv.notify_one();
}
void Eventloop::wait_for_shutdown()
{
    if (worker.joinable())
        worker.join();
}

void Eventloop::erase(std::shared_ptr<ConnectionBase> c, Error reason)
{
    defer(Erase { std::move(c), reason });
}

void Eventloop::on_outbound_closed(std::shared_ptr<ConnectionBase> c, Error reason)
{
    defer(OutboundClosed { std::move(c), reason });
}

void Eventloop::on_dns_resolve(DnsResolveResult r)
{
    defer(std::move(r));
}

void Eventloop::async_state_update(StateUpdate&& s)
{
    defer(std::move(s));
}

void Eventloop::async_mempool_update(mempool::Updates&& s)
{
    defer(std::move(s));
}

void Eventloop::start_timer(StartTimer st)
{
    defer(std::move(st));
}

void Eventloop::cancel_timer(TimerSystem::key_t k)
{
    defer(CancelTimer { k });
}

void Eventloop::on_failed_connect(const ConnectRequest& r, Error reason)
{
    defer(FailedConnect { r, reason });
}

void Eventloop::api_get_peers(PeersCb&& cb)
{
    defer(GetPeers { cb });
}

void Eventloop::api_get_throttled(ThrottledCb&& cb)
{
    defer(GetThrottled { cb });
}

void Eventloop::api_disconnect_peer(uint64_t id, ErrorCb&& cb)
{
    defer(DisconnectPeer { id, std::move(cb) });
}

void Eventloop::api_is_synced(SyncedCb&& cb)
{
    defer(std::move(cb));
}

void Eventloop::api_inspect(InspectorCb&& cb)
{
    defer(std::move(cb));
}
void Eventloop::api_count_ips(IpCounterCb&& cb)
{
    defer(std::move(cb));
}
void Eventloop::subscribe_connection_event(SubscriptionRequest r)
{
    defer(SubscribeConnections(std::move(r)));
}
void Eventloop::destroy_subscriptions(subscription_data_ptr p)
{
    defer(DestroySubscriptions(p));
}

void Eventloop::api_get_hashrate(HashrateCb&& cb, size_t n)
{
    defer(GetHashrate { std::move(cb), n });
}

void Eventloop::api_get_connection_schedule(JSONCb&& cb)
{
    defer(GetConnectionSchedule(std::move(cb)));
}
void Eventloop::api_sample_verified_peers(size_t n, SampledPeersCb cb)
{
    defer(SampleVerifiedPeers { n, std::move(cb) });
}

void Eventloop::api_get_hashrate_time_chart(uint32_t from, uint32_t to, size_t window, HashrateTimeChartCb&& cb)
{
    defer(GetHashrateTimeChart { std::move(cb), from, to, window });
}

void Eventloop::api_get_hashrate_block_chart(NonzeroHeight from, NonzeroHeight to, size_t window, HashrateBlockChartCb&& cb)
{
    defer(GetHashrateBlockChart { std::move(cb), from, to, window });
}

void Eventloop::api_loadtest_block(uint64_t conId, ErrorCb cb)
{
    defer(Loadtest { conId, RequestType::make<BlockRequest>(), std::move(cb) });
}
void Eventloop::api_loadtest_header(uint64_t conId, ErrorCb cb)
{
    defer(Loadtest { conId, RequestType::make<HeaderRequest>(), std::move(cb) });
}
void Eventloop::api_loadtest_disable(uint64_t conId, ErrorCb cb)
{
    defer(Loadtest { conId, wrt::nullopt, std::move(cb) });
}

void Eventloop::async_forward_blockrep(uint64_t conId, std::vector<BodyData>&& blocks)
{
    defer(OnForwardBlockrep { conId, std::move(blocks) });
}

void Eventloop::notify_closed_rtc(std::shared_ptr<RTCConnection> rtc)
{
    defer(RTCClosed { std::move(rtc) });
}

bool Eventloop::has_work()
{
    auto now = std::chrono::steady_clock::now();
    return haswork || (now > timerSystem.next());
}

void Eventloop::loop()
{
    if (config().node.enableWebRTC) {
        RTCConnection::fetch_id([w = weak_from_this()](IdentityIps&& ips) {
            if (auto p { w.lock() })
                p->defer(std::move(ips));
        },
            true);
    }

    auto r { connections.start() };

#ifndef DISABLE_LIBUV
    // start DNS requests
    for (auto& r : r.dnsRequests) {
        glo global
    }
#endif
    while (true) {
        {
            std::unique_lock<std::mutex> ul(mutex);
            while (!has_work()) {
                auto until = timerSystem.next();
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
    std::vector<TimerSystem::Event> expired;
    {
        std::unique_lock<std::mutex> l(mutex);
        std::swap(tmp, events);
        expired = timerSystem.pop_expired();
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
    set_scheduled_connect_timer();
}

bool Eventloop::check_shutdown()
{

    {
        std::unique_lock<std::mutex> l(mutex);
        if (!closeReason.has_value())
            return false;
    }

    spdlog::debug("Shutdown connectionManager.size() {}", connections.size());
    for (auto cr : connections.all()) {
        if (cr->erased())
            continue;
        erase_internal(cr, *closeReason);
    }
    return true;
}

void Eventloop::handle_event(Erase&& m)
{
    bool erased { m.c->eventloop_erased };
    bool registered { m.c->eventloop_registered };
    if ((!erased) && registered)
        erase_internal(m.c->dataiter, m.reason);
}

void Eventloop::handle_event(OutboundClosed&& e)
{
    connections.outbound_closed(std::move(e));
}

void Eventloop::handle_event(DnsResolveResult&& e)
{
    connections.on_dns_resolve(e);
}

void Eventloop::handle_event(OnHandshakeCompleted&& m)
{
    if (auto r { try_insert_connection(std::move(m)) }) {
        send_init(r.value());
    } else {
        auto c { m.convar.base() };
        c->close(r.error());
        c->eventloop_erased = true;
    }
}

void Eventloop::handle_event(OnProcessConnection&& m)
{
    process_connection(std::move(m.c));
}

void Eventloop::handle_event(StateUpdate&& e)
{
    // mempool
    mempool.replay_updates(std::move(e.mempoolUpdates));

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
                c->chain.on_consensus_fork(msg.forkHeight(), chains);
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
    headerDownload.set_min_worksum(max);
    blockDownload.set_min_worksum(cons);
}

void Eventloop::try_start_sync_timing()
{
    if (syncTiming.has_value())
        return;
    syncTiming.emplace();
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
    try_start_sync_timing();
    spdlog::info("Syncing... (height {} of {})", chains.consensus_length().value(), hc.length().value());
    auto forkHeight { chains.update_stage(std::move(hc)) };
    return forkHeight;
}

void Eventloop::log_chain_length()
{
    auto synced { chains.consensus_length().value() };
    auto total { chains.stage_headers().length().value() };
    if (synced < total) {
        spdlog::info("Syncing... (height {} of {})", synced, total);
        try_start_sync_timing();
    } else if (synced == total) {
        assert(syncTiming);
        spdlog::info("Synced in {}. (height {}).", syncTiming->startedAt.elapsed().format(), synced);
        syncTiming.reset();
    }
}

void Eventloop::handle_event(GetThrottled&& e)
{
    std::vector<api::ThrottledPeer> out;
    for (auto cr : connections.initialized()) {
        if (cr->throttleQueue.reply_delay() != 0s) {
            out.push_back({ .endpoint { cr->c->peer_addr() },
                .id = cr.id(),
                .throttle { cr->throttleQueue } });
        }
    }
    e.callback(out);
}

void Eventloop::handle_event(GetPeers&& e)
{
    std::vector<api::Peerinfo> out;
    for (auto cr : connections.initialized()) {
        out.push_back(api::Peerinfo {
            .endpoint { cr.peer() },
            .id = cr.id(),
            .initialized = cr.initialized(),
            .chainstate = cr.chain(),
            .theirSnapshotPriority = cr->theirSnapshotPriority,
            .acknowledgedSnapshotPriority = cr->acknowledgedSnapshotPriority,
            .since = cr->c->created_at_timestmap(),
            .throttle { cr->throttleQueue } });
    }
    e.callback(out);
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
        cb(Error(ENOTFOUND));
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
        BlockrepMsg msg((*cr)->lastNonce, std::move(m.blocks));
        cr->send(msg);
    }
}

void Eventloop::handle_event(InspectorCb&& cb)
{
    cb(*this);
}

void Eventloop::handle_event(IpCounterCb&& cb)
{
    cb(connections.api_count_ips());
}

void Eventloop::handle_event(GetHashrate&& e)
{
    e.cb(api::HashrateInfo {
        .nBlocks = e.n,
        .estimate = consensus().headers().hashrate(e.n) });
}

void Eventloop::handle_event(GetHashrateBlockChart&& e)
{
    e.cb(consensus().headers().hashrate_block_chart(e.from, e.to, e.window));
}

void Eventloop::handle_event(GetHashrateTimeChart&& e)
{
    e.cb(consensus().headers().hashrate_time_chart(e.from, e.to, e.window));
}

void Eventloop::handle_event(GetConnectionSchedule&& e)
{
    e.cb(connections.to_json());
}

void Eventloop::handle_event(FailedConnect&& e)
{
    spdlog::warn("Cannot connect to {}: {}", e.connectRequest.address().to_string(), Error(e.reason).err_name());
    connections.outbound_failed(e.connectRequest, e.reason);
}

void Eventloop::handle_event(mempool::Updates&& log)
{
    mempool.replay_updates(log);

    // build vector of mempool entries
    std::vector<mempool::Entry> entries;
    for (auto& action : log) {
        if (std::holds_alternative<mempool::Put>(action)) {
            entries.push_back(std::get<mempool::Put>(action).entry);
        }
    }
    std::sort(entries.begin(), entries.end(),
        [](const mempool::Entry& e1, const mempool::Entry& e2) {
            if (e1.txHeight == e2.txHeight)
                return e1.txid() < e2.txid();
            return e1.txHeight < e2.txHeight;
        });

    // construct subscription bounds per connection
    auto eiter = entries.begin();
    std::vector<std::pair<decltype(entries)::iterator, Conref>> bounds;
    auto miter = mempoolSubscriptions.cbegin();
    if (mempoolSubscriptions.size() > 0) {
        while (eiter != entries.end()) {
            while (!(eiter->txHeight < miter->first.transactionHeight)) {
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
    finished:;

        // TODO
        // send subscription individually
        // for (auto& [end, cr] : bounds) {
        //     cr.send(TxnotifyMsg::direct_send(entries.begin(), end));
        // }
    }
}

void Eventloop::handle_event(PushRogue&& pr)
{
    auto disconnectList { headerDownload.on_rogue_header(pr.rogueHeaderData) };
    for (ChainOffender& o : disconnectList) {
        close(o);
    };
}

void Eventloop::handle_event(StartTimer&& st)
{
    auto now = std::chrono::steady_clock::now();
    if (st.wakeup < now) {
        st.on_expire();
        return;
    }
    auto t { timerSystem.insert(st.wakeup, TimerEvent::CallFunction(std::move(st.on_expire))) };
    st.on_timerstart(t.key());
}

void Eventloop::handle_event(CancelTimer&& ct)
{
    timerSystem.erase(ct.key);
}

void Eventloop::handle_event(RTCClosed&& ct)
{
    assert(config().node.enableWebRTC);
    if (auto conId { ct.con->verification_con_id() }; conId != 0) { // conId id verified in this RTC connection
        if (auto con { connections.find(conId) }) {
            assert(rtc_enabled(*con));
            con->rtc().our.pendingVerification.done();
            rtc.verificationSchedule.add(*con);
        }
    }
    rtc.connections.erase(ct.con);
    try_verify_rtc_identities();
}

void Eventloop::handle_event(IdentityIps&& ips)
{
    assert(config().node.enableWebRTC);
    log_rtc("WebRTC identity IPv4: {}", ips.get_ip4() ? ips.get_ip4().value().to_string() : "N/A");
    log_rtc("WebRTC identity IPv6: {}", ips.get_ip6() ? ips.get_ip6().value().to_string() : "N/A");

    assert(rtc.ips.has_value() == false);
    for (auto cr : connections.initialized()) {
        // if (cr.version().v2()) { TODO
        if (rtc_enabled(cr)) {
            log_rtc("Sending own identity");
            cr.send(RTCIdentity(ips));
        }
    }
    rtc.ips = std::move(ips);

    send_schedule_signaling_lists();
    for (auto c : connections.initialized()) {
        rtc.verificationSchedule.add(c);
    }
    try_verify_rtc_identities();
}

void Eventloop::handle_event(GeneratedVerificationSdpAnswer&& m)
{
    assert(config().node.enableWebRTC);
    auto l { m.con.lock() };
    if (!l)
        return;
    auto& con { *l };
    auto orig { connections.find(m.originConId) };
    if (!orig.has_value()) {
        con.close(ERTCNOSIGNAL2);
        return;
    }
    auto& originCon { orig.value() };

    // filter
    auto filtered { sdp_filter::only_udp_ip(m.ownIp, m.sdp) };
    if (!filtered) {
        // We cannot offer what we promised
        close(originCon, ERTCOWNIP);
        return;
    }
    auto& sdp = *filtered;
    log_rtc("send RTCVerificationAnswer, ip: {}", m.ownIp.to_string());
    assert(sdp.length() < std::numeric_limits<uint16_t>::max());
    originCon.send(RTCVerificationAnswer { String16(sdp) });
}

void Eventloop::handle_event(GeneratedSdpAnswer&& m)
{
    assert(config().node.enableWebRTC);
    auto l { m.con.lock() };
    if (!l)
        return;
    auto& con { *l };
    auto sig { connections.find(m.signalingServerId) };
    if (!sig.has_value()) {
        con.close(ERTCNOSIGNAL2);
        return;
    }
    auto& signalingServer { sig.value() };
    assert(rtc_enabled(signalingServer));

    // filter
    auto filtered { sdp_filter::only_udp_ip(m.ownIp, m.sdp) };
    if (!filtered) {
        // We cannot offer what we promised, TODO: think about other ways to handle this
        close(signalingServer, ERTCOWNIP);
        return;
    }
    auto& sdp = *filtered;
    assert(sdp.length() < std::numeric_limits<uint16_t>::max());

    signalingServer.send(RTCRequestForwardAnswer { String16(std::move(sdp)), m.key });
}

void Eventloop::emit_disconnect(size_t n, uint64_t id)
{
    using namespace subscription;
    api::event::emit_disconnect(n, id);
    events::Event { events::ConnectionsRemove { id, n } }.send(connectionSubscriptions);
}
void Eventloop::emit_connect(size_t n, Conref c)
{
    using namespace subscription;
    api::event::emit_connect(n, c);
    events::Event { events::ConnectionsAdd {
                        .connection {
                            .id = c.id(),
                            .since = c->c->startTimePoints,
                            .peerAddr { c->c->peer_addr().to_string() },
                            .inbound = c->c->inbound() },
                        .total = n } }
        .send(connectionSubscriptions);
}
void Eventloop::handle_event(SubscribeConnections&& c)
{
    using enum subscription::Action;
    auto ptr { c.sptr.get() };
    switch (c.action) {
    case Unsubscribe:
        std::erase_if(connectionSubscriptions, [&](subscription_ptr& p) {
            return p.get() == ptr;
        });
        return;
    case Subscribe:
        for (auto& p : connectionSubscriptions) {
            if (p.get() == c.sptr.get()) {
                if (c.action == subscription::Action::Unsubscribe) {
                }
                goto subscribed;
            }
        }
        connectionSubscriptions.push_back(c.sptr);
    subscribed:
        std::vector<subscription::events::Connection> v;
        for (auto c : connections.initialized()) {
            v.push_back({ .id = c.id(),
                .since = c->c->startTimePoints,
                .peerAddr { c->c->peer_addr().to_string() },
                .inbound = c->c->inbound() });
        }
        SubscriptionEvent { subscription::events::ConnectionsState {
                                .connections { std::move(v) },
                                .total = v.size() } }
            .send(std::move(c.sptr));
    }
}
void Eventloop::handle_event(DestroySubscriptions&& d)
{
    std::erase_if(connectionSubscriptions, [&](subscription_ptr& p) {
        return p.get() == d.p;
    });
}

void Eventloop::handle_event(GeneratedVerificationSdpOffer&& m)
{
    auto l { m.con.lock() };
    assert(config().node.enableWebRTC);
    if (!l)
        return;
    auto& rtcCon { *l };

    auto o { connections.find(m.peerId) };
    if (!o.has_value()) {
        rtcCon.close(ERTCNOPEER);
        return;
    }
    auto& c { o.value() };
    assert(rtc_enabled(c));
    const auto& verifyIp { rtcCon.native_peer_addr().ip };

    auto ips { IdentityIps::from_sdp(m.sdp) };

    wrt::optional<IP> selected { ips.get_ip_with_type(verifyIp.type()) };
    if (!selected.has_value()) {
        rtcCon.close(ERTCNOPEER);
        return;
    }

    log_rtc("GeneratedVerificationSdpOffer: with IP {}", selected->to_string());
    auto filtered { sdp_filter::only_udp_ip(*selected, m.sdp) };
    assert(filtered.has_value());
    assert(filtered->length() < std::numeric_limits<uint16_t>::max());
    c.send(RTCVerificationOffer { verifyIp, String16(filtered.value()) });
}

void Eventloop::handle_event(GeneratedSdpOffer&& m)
{
    auto l { m.con.lock() };
    if (!config().node.enableWebRTC || !l)
        return;
    auto& rtcCon { *l };

    auto sig { connections.find(m.signalingServerId) };
    if (!sig.has_value()) {
        rtcCon.close(ERTCNOSIGNAL);
        return;
    }
    auto& signalingServer { sig.value() };
    assert(rtc_enabled(signalingServer));

    auto key { m.signalingListKey };
    // only send if the signaling server still supports the same list where
    // the key is taken from (no new RTCSignalingList message received since then)
    if (signalingServer.rtc().their.signalingList.covers(key)) {
        signalingServer.rtc().our.pendingOutgoing.insert(std::move(m.con));
        assert(m.sdp.length() < std::numeric_limits<uint16_t>::max());
        signalingServer.send(RTCRequestForwardOffer { key, String16(std::move(m.sdp)) });
    }
}

void Eventloop::handle_event(DisconnectPeer&& dp)
{
    if (auto o { connections.find(dp.id) }) {
        close(*o, EAPICMD);
        dp.cb({});
    }
    dp.cb(Error(ENOTFOUND));
}

void Eventloop::handle_event(SampleVerifiedPeers&& p)
{
#ifndef DISABLE_LIBUV
    p.cb(connections.sample_verified_tcp(p.n));
#else
    p.cb({});
#endif
}

void Eventloop::handle_event(Loadtest&& e)
{
    if (auto cr { connections.find(e.connId) }) {
        // start load test
        cr->loadtest().job = e.requestType;
        do_loadtest_requests();
        e.callback({});
    } else {
        e.callback(Error(ENOTFOUND));
    }
}

size_t Eventloop::ratelimit_spare()
{
    using namespace std::chrono;
    auto now { steady_clock::now() };

    // 1 extra spare per minute
    return consensus().ratelimit_spare()
        + duration_cast<minutes>(now - startedAt).count();
}

void Eventloop::erase_internal(Conref c, Error error)
{
    if (c->c->eventloop_erased)
        return;
    c->c->eventloop_erased = true;
    bool doRequests = false;
    c.job().unref_active_requests(activeRequests);
    timerSystem.erase(c.ping().timer);
    timerSystem.erase(c.job().timer);
    c->throttleQueue.reset_timer(timerSystem);
    if (headerDownload.erase(c) && !closeReason) {
        spdlog::info("Connected to {} peers (disconnected {}, {} reason: {})",
            headerDownload.size(), c.peer().to_string(), c.version().to_string(), Error(error).err_name());
        emit_disconnect(headerDownload.size(), c.id());
    }
    if (blockDownload.erase(c))
        coordinate_sync();
    rtc.erase(c);
    connections.erase(c.iterator());
    if (doRequests) {
        do_requests();
    }
}

void Eventloop::close(Conref cr, Error reason)
{
    if (!cr->c->eventloop_registered)
        return;
    cr->c->close(reason);
    erase_internal(cr, reason); // do not consider this connection anymore
}

void Eventloop::close_by_id(uint64_t conId, Error reason)
{
    if (auto cr { connections.find(conId) }; cr)
        close(*cr, reason);
    // LATER: report offense to peerserver
}

void Eventloop::close(const ChainOffender& o)
{
    assert(o);
    if (auto cr { connections.find(o.conId) }; cr) {
        close(*cr, o.code);
    } else {
        report(o);
    }
}
void Eventloop::close(Conref cr, ChainError e)
{
    assert(e);
    close(cr, e.code);
}

void Eventloop::process_connection(std::shared_ptr<ConnectionBase> c)
{
    if (c->eventloop_erased)
        return;
    assert(c->eventloop_registered);
    auto messages = c->pop_messages();
    for (auto& msg : messages) {
        Conref cr { c->dataiter };
        try {
            process_message(cr, msg);
        } catch (Error e) {
            close(cr, e.code);
            do_requests();
            break;
        }
        if (c->eventloop_erased)
            return;
    }
}

void Eventloop::send_ping_await_pong(Conref c)
{
    // do periodic tasks
    c.rtc().our.pendingForwards.prune([&](const rtc_state::PendingForwards::Entry& e) {
        if (auto con { connections.find(e.fromConId) }) {
            if (con->rtc().their.forwardRequests.is_accepted_key(e.fromKey)) {
                assert(rtc_enabled(*con));
                con->send(RTCForwardOfferDenied(e.fromKey, 1));
            }
        }
    });

    // send
    log_communication("{} Sending Ping", c.str());
    auto t = timerSystem.insert(
        (config().localDebug ? 10min : 2min),
        TimerEvent::CloseNoPong { c.id() });

    uint16_t maxTCPAddressess { 0 };
#ifndef DISABLE_LIBUV
    if (c.is_tcp())
        maxTCPAddressess = 5; // only accept TCP addresses from TCP peers and only if we are TCP node ourselves
#endif

    if (c.protocol().v1()) {
        PingMsg p(signed_snapshot() ? signed_snapshot()->priority : SignedSnapshot::Priority {}, maxTCPAddressess);
        c.ping().await_pong(p, t);
        c.send(p);
    } else {
        PingV2Msg p(signed_snapshot() ? signed_snapshot()->priority : SignedSnapshot::Priority {},
            { .maxAddresses = maxTCPAddressess, .ndiscard = c.rtc().our.pendingOutgoing.schedule_discard() });
        c.ping().await_pong_v2({ .extraData { c.rtc().our.signalingList.offset_scheduled() },
                                   .msg = std::move(p) },
            t);
        c.send(p);
    }
}

void Eventloop::received_pong_sleep_ping(Conref c)
{
    auto t = timerSystem.insert(10s, TimerEvent::SendPing { c.id() });
    timerSystem.erase(c.ping().sleep(t));
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

void Eventloop::do_loadtest_requests()
{
    for (auto cr : connections.initialized()) {
        if (cr.job())
            continue;
        auto l { cr->loadtest.generate_load(cr) };
        if (!l)
            continue;
        spdlog::info("Sending loadtest request to peer {}.", cr->c->peer_addr().to_string());
        std::visit([&](auto& request) {
            sender().send(cr, request);
        },
            *l);
    }
}

void Eventloop::do_requests()
{
    do_loadtest_requests();
    while (true) {
        auto offenders { headerDownload.do_header_requests(sender()) };
        if (offenders.empty())
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
    log_communication("{} send {}", c.str(), req.log_str());
    auto t = timerSystem.insert(req.expiry_time, TimerEvent::Expire { c.id() });
    c.job().assign(t, req);
    if (req.isActiveRequest) {
        assert(activeRequests < maxRequests);
        activeRequests += 1;
    }
    c.send(req);
}

void Eventloop::send_init(Conref cr)
{
    if (cr.protocol().v1() || cr.protocol().v2()) {
        cr.send(InitMsgGeneratorV1(consensus()));
    } else {
        cr.send(InitMsgGeneratorV3(consensus(), config().node.enableWebRTC));
    }
}

template <typename T>
requires std::derived_from<T, TimerEvent::WithConnecitonId>
void Eventloop::handle_timeout(T&& t)
{
    if (auto cr { connections.find(t.conId) }; cr.has_value()) {
        handle_connection_timeout(*cr, std::move(t));
    }
}
void Eventloop::handle_connection_timeout(Conref cr, TimerEvent::CloseNoReply&&)
{
    cr.job().timer.reset();
    close(cr, ETIMEOUT);
}

void Eventloop::handle_connection_timeout(Conref cr, TimerEvent::CloseNoPong&&)
{
    close(cr, ETIMEOUT);
}

void Eventloop::handle_timeout(TimerEvent::ScheduledConnect&&)
{
    connections.start_scheduled_connections();
}

void Eventloop::handle_timeout(TimerEvent::CallFunction&& cf)
{
    cf.callback();
}

void Eventloop::handle_timeout(TimerEvent::SendIdentityIps&&)
{
    send_schedule_signaling_lists();
}

void Eventloop::handle_connection_timeout(Conref cr, TimerEvent::SendPing&&)
{
    cr.ping().reset_timer();
    return send_ping_await_pong(cr);
}

void Eventloop::handle_connection_timeout(Conref cr, TimerEvent::ThrottledProcessMsg&&)
{
    try {
        dispatch_message(cr, cr->throttleQueue.reset_timer_pop_msg());
        cr->throttleQueue.update_timer(timerSystem, cr.id());
    } catch (Error e) {
        close(cr, e.code);
        do_requests();
    }
}

void Eventloop::handle_connection_timeout(Conref cr, TimerEvent::Expire&&)
{
    if (!cr.job().active())
        return;
    cr.job().set_timer(timerSystem.insert(
        (config().localDebug ? 10min : 2min), TimerEvent::CloseNoReply { cr.id() }));
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
}

void Eventloop::on_request_expired(Conref cr, const Proberequest&)
{
    headerDownload.on_probe_request_expire(cr);
    blockDownload.on_probe_expire(cr);
    do_requests();
}

void Eventloop::on_request_expired(Conref cr, const HeaderRequest& req)
{
    headerDownload.on_request_expire(cr, req);
    do_requests();
}

void Eventloop::on_request_expired(Conref cr, const BlockRequest&)
{
    blockDownload.on_blockreq_expire(cr);
    do_requests();
}

void Eventloop::process_message(Conref cr, Rcvbuffer& msg)
{
    using namespace messages;

    auto m { msg.parse() };
    using namespace std;
    bool isInit = holds_alternative<InitMsgV1>(m) || holds_alternative<InitMsgV3>(m);
    // first message must be of type INIT (is_init() is only initially true)
    if (cr.job().awaiting_init()) {
        if (!isInit)
            throw Error(ENOINIT);
        dispatch_message(cr, std::move(m));
    } else {
        if (isInit)
            throw Error(EINVINIT);
        cr->throttleQueue.insert(std::move(m), timerSystem, cr.id());
    }
}

void Eventloop::dispatch_message(Conref cr, messages::Msg&& msg)
{
    std::visit([&](auto&& e) { 
        communication_log().info("IN  {}: {}", cr.str(), e.log_str());
        handle_msg(cr, std::move(e)); },
        std::move(msg));
}

void Eventloop::handle_msg(Conref c, InitMsgV1&& m)
{
    log_communication("{} handle init: height {}, work {}", c.str(), m.chainLength.value(), m.worksum.getdouble());
    c.job().reset_notexpired<AwaitInit>(timerSystem);

    if (!c.protocol().v1() && !c.protocol().v2()) // must have at least version 3 for this message type
        throw Error(EINITV1);
    c->chain.initialize(m, chains);
    headerDownload.insert(c);
    blockDownload.insert(c);
    emit_connect(headerDownload.size(), c);
    spdlog::info("Connected to {} peers (new peer {}, {})", headerDownload.size(), c.peer().to_string(), c.version().to_string());
    if (rtc_enabled(c)) {
        if (rtc.ips && c.protocol().v2()) {
            c->rtcState.enanabled = true; // v2 has rtc enabled by default
            log_rtc("Sending own identity");
            c.send(RTCIdentity(*rtc.ips));
        } else
            log_rtc("NOT sending own identity");
    }
    send_ping_await_pong(c);
    // LATER: return whether doRequests is necessary;
    do_requests();
}

void Eventloop::handle_msg(Conref c, InitMsgV3&& m)
{
    log_communication("{} handle init: height {}, work {}", c.str(), m.chain_length().value(), m.worksum().getdouble());
    if (c.protocol().v1() || c.protocol().v2()) // must have at least version 3 for this message type
        throw Error(EINITV3);
    c.job().reset_notexpired<AwaitInit>(timerSystem);
    c->rtcState.enanabled = m.rtc_enabled();
    c->chain.initialize(m, chains);
    headerDownload.insert(c);
    blockDownload.insert(c);
    emit_connect(headerDownload.size(), c);
    spdlog::info("Connected to {} peers (new peer {}, {})", headerDownload.size(), c.peer().to_string(), c.version().to_string());
    if (rtc_enabled(c)) {
        if (rtc.ips) { // TODO V2
            log_rtc("Sending own identity");
            c.send(RTCIdentity(*rtc.ips));
        } else
            log_rtc("NOT sending own identity");
    }
    send_ping_await_pong(c);
    // LATER: return whether doRequests is necessary;
    do_requests();
}

void Eventloop::handle_msg(Conref cr, AppendMsg&& m)
{
    log_communication("{} handle append", cr.str());
    cr->chain.on_peer_append(m, chains);
    headerDownload.on_append(cr);
    blockDownload.on_append(cr);
    do_requests();
}

void Eventloop::handle_msg(Conref c, SignedPinRollbackMsg&& m)
{
    log_communication("{} handle rollback ", c.str());
    verify_rollback(c, m);
    c->chain.on_peer_shrink(m, chains);
    headerDownload.on_rollback(c);
    blockDownload.on_rollback(c);
    do_requests();
}

void Eventloop::handle_msg(Conref c, ForkMsg&& m)
{
    log_communication("{} handle fork", c.str());
    c->chain.on_peer_fork(m, chains);
    headerDownload.on_fork(c);
    blockDownload.on_fork(c);
    do_requests();
}

void Eventloop::handle_msg(Conref c, PingMsg&& m)
{
    log_communication("{} handle ping", c.str());
    c->ratelimit.ping();
#ifndef DISABLE_LIBUV
    size_t nAddr { std::min(uint16_t(20), m.maxAddresses()) };
    if (!c.is_tcp())
        nAddr = 0;
    auto addresses { connections.sample_verified_tcp(nAddr) };
    PongMsg msg { m.nonce(), std::move(addresses), mempool.sample(m.maxTransactions(), c.version().protocol().before_defi()) };
    spdlog::debug("{} Sending {} addresses", c.str(), msg.addresses().size());
#else
    PongMsg msg(m.nonce(), {}, {});
#endif
    if (c->theirSnapshotPriority < m.sp())
        c->theirSnapshotPriority = m.sp();
    c.send(msg);
    consider_send_snapshot(c);
}

void Eventloop::handle_msg(Conref c, PingV2Msg&& m)
{

    log_communication("{} handle ping", c.str());
    c->ratelimit.ping();
#ifndef DISABLE_LIBUV
    size_t nAddr { std::min(uint16_t(20), m.maxAddresses()) };
    if (!c.is_tcp())
        nAddr = 0;
    auto addresses = connections.sample_verified_tcp(nAddr);
    PongV2Msg msg { m.nonce(), std::move(addresses), mempool.sample(m.maxTransactions(), c.version().protocol().before_defi()) };
#else
    PongV2Msg msg(m.nonce(), {}, {});
#endif
    spdlog::debug("{} Sending {} addresses", c.str(), msg.addresses().size());
    if (c->theirSnapshotPriority < m.sp())
        c->theirSnapshotPriority = m.sp();
    c.send(msg);
    consider_send_snapshot(c);
    c.rtc().their.forwardRequests.discard(m.discarded_forward_requests());
};

// only process received addresses when we are a native node (not browser nodes)
#ifndef DISABLE_LIBUV
void Eventloop::on_received_addresses(Conref cr, const serialization::Vector16<TCPPeeraddr>& addresses)
{
    if (auto ip { cr.peer().ip() }; ip.has_value() && cr.is_tcp()) {
        spdlog::debug("{} Received {} addresses", cr.str(), addresses.size());
        if (ip->is_v4())
            connections.verify(addresses, ip->get_v4());
    }
#else
void Eventloop::on_received_addresses(Conref, const serialization::Vector16<TCPPeeraddr>&)
{
#endif
}

void Eventloop::handle_msg(Conref cr, PongMsg&& m)
{
    log_communication("{} handle pong", cr.str());
    auto& pingMsg = cr.ping().check(m);
    on_received_addresses(cr, m.addresses());
    received_pong_sleep_ping(cr);
    spdlog::debug("{} Got {} transaction Ids in pong message", cr.str(), m.txids().size());

    // update acknowledged priority
    if (cr->acknowledgedSnapshotPriority < pingMsg.sp()) {
        cr->acknowledgedSnapshotPriority = pingMsg.sp();
    }

    // request new txids
    auto txids = mempool.filter_new(m.txids());
    if (txids.size() > 0)
        cr.send(TxreqMsg(txids));
}

void Eventloop::handle_msg(Conref cr, PongV2Msg&& m)
{
    log_communication("{} handle pong", cr.str());
    auto& pingData = cr.ping().check(m);
    auto& pingMsg = pingData.msg;
    on_received_addresses(cr, m.addresses());
    received_pong_sleep_ping(cr);
    spdlog::debug("{} Got {} transaction Ids in pong message", cr.str(), m.txids().size());

    // update acknowledged priority
    if (cr->acknowledgedSnapshotPriority < pingMsg.sp()) {
        cr->acknowledgedSnapshotPriority = pingMsg.sp();
    }

    // request new txids
    auto txids = mempool.filter_new(m.txids());
    if (txids.size() > 0)
        cr.send(TxreqMsg(txids));

    // peer has seen the ping message and we can be sure it must have
    // acknowledged discarding, we replay.
    cr.rtc().our.pendingOutgoing.discard(pingMsg.discarded_forward_requests());
    cr.rtc().our.signalingList.discard_up_to(pingData.extraData.signalingListDiscardIndex);
}

void Eventloop::handle_msg(Conref cr, BatchreqMsg&& m)
{
    log_communication("{} handle batchreq [{},{}]", cr.str(), m.selector().startHeight.value(), (m.selector().startHeight + m.selector().length - 1).value());
    auto& s = m.selector();
    // get batch
    Batch batch = [&]() {
        if (s.descriptor == consensus().descriptor()) {
            return consensus().headers().get_headers(s.header_range());
        } else {
            return stateServer.get_headers(s);
        }
    }();

    BatchrepMsg rep(m.nonce(), std::move(batch));

    // get throttle
    auto duration {
        cr->throttleQueue.headerreq.register_request(m.selector().header_range(), ratelimit_spare())
    };
    cr->throttleQueue.add_throttle(duration);
    cr.send(rep);
}

void Eventloop::handle_msg(Conref cr, BatchrepMsg&& m)
{
    log_communication("{} handle_batchrep", cr.str());
    // check nonce and get associated data
    auto req = cr.job().pop_req(m, timerSystem, activeRequests);

    // save batch
    if (m.batch().size() < req.minReturn || m.batch().size() > req.max_return()) {
        close(ChainOffender(EBATCHSIZE3, req.selector().startHeight, cr.id()));
        return;
    }
    if (!req.isLoadtest) {
        auto offenders = headerDownload.on_response(cr, std::move(req), std::move(m.batch()));
        for (auto& o : offenders) {
            close(o);
        }
        syncdebug_log().info("init blockdownload batch_rep");
        initialize_block_download();
    }

    // assign work
    do_requests();
}

void Eventloop::handle_msg(Conref cr, ProbereqMsg&& m)
{
    log_communication("{} handle_probereq d:{}, h:{}", cr.str(), m.descriptor().value(), m.height().value());
    ProberepMsg rep(m.nonce(), consensus().descriptor().value());
    auto h = consensus().headers().get_header(m.height());
    if (h)
        rep.current() = *h;
    if (m.descriptor() == consensus().descriptor()) {
        auto h = consensus().headers().get_header(m.height());
        if (h)
            rep.requested() = h;
    } else {
        auto h = stateServer.get_descriptor_header(m.descriptor(), m.height());
        if (h)
            rep.requested() = *h;
    }

    cr.send(rep);
}

void Eventloop::handle_msg(Conref cr, ProberepMsg&& rep)
{
    log_communication("{} handle_proberep", cr.str());
    auto req = cr.job().pop_req(rep, timerSystem, activeRequests);
    if (!rep.requested().has_value() && !req.descripted->expired()) {
        throw ChainError { EEMPTY, req.height() };
    }
    cr->chain.on_proberep(req, rep, chains);
    headerDownload.on_proberep(cr, req, rep);
    blockDownload.on_probe_reply(cr, req, rep);
    do_requests();
}

void Eventloop::handle_msg(Conref cr, BlockreqMsg&& m)
{
    auto duration { cr->throttleQueue.blockreq.register_request(m.range(), ratelimit_spare()) };
    cr->throttleQueue.add_throttle(duration);

    BlockreqMsg req(m);
    log_communication("{} handle_blockreq [{},{}]", cr.str(), req.range().first().value(), req.range().last().value());
    cr->lastNonce = req.nonce();
    stateServer.async_get_blocks(req.range(), [&, id = cr.id()](auto blocks) { Eventloop::async_forward_blockrep(id, std::move(blocks)); });
}

void Eventloop::handle_msg(Conref cr, BlockrepMsg&& m)
{
    log_communication("{} handle blockrep", cr.str());
    auto req = cr.job().pop_req(m, timerSystem, activeRequests);

    if (!req.isLoadtest) {
        try {
            blockDownload.on_blockreq_reply(cr, std::move(m), req);
            process_blockdownload_stage();
        } catch (Error e) {
            close(cr, e);
        }
    }
    do_requests();
}

void Eventloop::handle_msg(Conref cr, TxnotifyMsg&& m)
{
    log_communication("{} handle Txnotify", cr.str());
    auto txids = mempool.filter_new(m.txids());
    if (txids.size() > 0)
        cr.send(TxreqMsg(txids));
    do_requests();
}

void Eventloop::handle_msg(Conref cr, TxreqMsg&& m)
{
    log_communication("{} handle TxreqMsg", cr.str());
    TxrepMsg::vector_t out;
    for (auto& e : m.txids()) {
        out.push_back(mempool[e]);
    }
    if (out.size() > 0)
        cr.send(TxrepMsg(m.nonce(), out));
}

void Eventloop::handle_msg(Conref cr, LegacyTxrepMsg&& m)
{
    log_communication("{} handle LegacyTxrepMsg", cr.str());
    // TODO: check that sent transactions were actually requested ones
    std::vector<TransactionMessage> txs;
    for (auto& o : m.txs()) {
        if (o)
            txs.push_back(*o);
    };
    stateServer.async_put_mempool(std::move(txs));
    do_requests();
}

void Eventloop::handle_msg(Conref cr, TxrepMsg&& m)
{
    log_communication("{} handle TxrepMsg", cr.str());
    // TODO: check that sent transactions were actually requested ones
    std::vector<TransactionMessage> txs;
    for (auto& o : m.txs()) {
        if (o)
            txs.push_back(*o);
    };
    stateServer.async_put_mempool(std::move(txs));
    do_requests();
}

void Eventloop::handle_msg(Conref cr, LeaderMsg&& msg)
{
    log_communication("{} handle LeaderMsg", cr.str());
    // ban if necessary
    if (msg.signedSnapshot().priority <= cr->acknowledgedSnapshotPriority) {
        close(cr, ELOWPRIORITY);
        return;
    }

    // update knowledge about sender
    cr->acknowledgedSnapshotPriority = msg.signedSnapshot().priority;
    if (cr->theirSnapshotPriority < msg.signedSnapshot().priority) {
        cr->theirSnapshotPriority = msg.signedSnapshot().priority;
    }

    stateServer.async_set_signed_checkpoint(msg.signedSnapshot());
}

void Eventloop::send_schedule_signaling_lists()
{
    if (!config().node.enableWebRTC)
        return;
    // build map
    std::vector<std::pair<Conref, size_t>> quotasVec;
    std::vector<Conref> conrefs;
    for (auto c : connections.initialized()) {
        if (c.protocol().v1())
            continue;
        auto avail { c.rtc().their.quota.available() };
        quotasVec.push_back({ c, avail });
        conrefs.push_back(c);
    }
    log_rtc("send_schedule_signaling_lists to {} peers", conrefs.size());

    // shuffle connections
    std::random_device rd;
    std::mt19937 g(rd());
    std::ranges::shuffle(conrefs, g);

    // for random boolean sampling
    std::uniform_int_distribution<> d(0, 1);

    // send and save
    for (auto& c : conrefs) {
        if (!rtc_enabled(c))
            continue;
        auto& identity { c.rtc().their.identity };
        auto v4 { identity.verified_ip4() };
        auto v6 { identity.verified_ip6() };

        std::vector<IP> ips;
        std::vector<std::pair<uint64_t, IP>> conIds;

        for (auto& [c, quota] : quotasVec) {
            if (quota == 0)
                continue;
            quota -= 1;
            auto use_ip = [&](IP ip) {
                ips.push_back(ip);
                conIds.push_back({ c.id(), ip });
            };
            if (v4) {
                if (v6) { // if we verified both, ipv6 and ipv6, then we sample
                    bool randBool = d(g) == 1;
                    if (randBool)
                        use_ip(*v4);
                    else
                        use_ip(*v6);
                } else {
                    use_ip(*v4);
                }
            } else if (v6) {
                use_ip(*v6);
            }
        }
        c.rtc().our.signalingList.set(conIds);
        c.send(RTCSignalingList(std::move(ips)));
    }
    timerSystem.insert(1min, TimerEvent::SendIdentityIps {});
}

void Eventloop::handle_msg(Conref c, RTCIdentity&& msg)
{
    throw_if_rtc_disabled(c);
    log_rtc("Received rtc identity");
    // TODO: restrict number of identity messages
    c.rtc().their.identity.set(msg.ips());

    rtc.verificationSchedule.add(c);
    try_verify_rtc_identities();
}

void Eventloop::handle_msg(Conref c, RTCQuota&& msg)
{
    throw_if_rtc_disabled(c);
    log_rtc("Received RTCQuota increase {} bytes", msg.increase());
    c.rtc().their.quota.increase_allowed(msg.increase());
}

void Eventloop::handle_msg(Conref c, RTCSignalingList&& s)
{
    throw_if_rtc_disabled(c);
    log_rtc("Received RTCSignalingList");
    const auto& ips { s.ips() };
    const auto offset {
        c.rtc().their.signalingList.set_new_list_size(ips.size())
    };
    for (size_t i = 0; i < ips.size(); ++i) {
        if (!c.rtc().our.pendingOutgoing.can_connect())
            break;

        auto& ip = s.ips()[i];
        if (connections.ip_count(ip) > 0)
            continue;

        uint64_t signalingListKey { offset + i };
        rtc.connections.insert(
            RTCConnection::connect_new(
                *this,
                [&, id = c.id(), signalingListKey](RTCConnection& con, std::string sdp) {
                    defer(GeneratedSdpOffer {
                        .con { con.weak_from_this() },
                        .signalingServerId = id,
                        .signalingListKey = signalingListKey,
                        .sdp { std::move(sdp) } });
                },
                ip));
    }
}

void Eventloop::handle_msg(Conref cr, RTCRequestForwardOffer&& r)
{
    throw_if_rtc_disabled(cr);
    auto dstId { cr.rtc().our.signalingList.get_con_id(r.signaling_list_key()) };
    auto key { cr.rtc().their.forwardRequests.create() };
    if (!dstId) {
        cr.send(RTCForwardOfferDenied { key, 0 });
        return;
    }
    auto ip { sdp_filter::load_ip(r.offer()) };
    if (!ip)
        throw Error(ERTCUNIQUEIP_RFO);
    if (!cr.rtc().their.identity.ip_is_verified(*ip))
        throw Error(ERTCUNVERIFIEDIP);

    auto dst_opt { connections.find(*dstId) };
    if (!dst_opt)
        return;
    auto& dstCon { *dst_opt };
    dstCon.rtc().our.pendingForwards.add(*ip, key, cr.id());
    assert(dstCon.rtc().their.quota.take_one());

    assert(r.offer().length() < std::numeric_limits<uint16_t>::max());
    dstCon.send(RTCForwardedOffer { String16(r.offer()) });
}

void Eventloop::handle_msg(Conref cr, RTCForwardedOffer&& m)
{
    throw_if_rtc_disabled(cr);
    // check our quota assigned to that peeer
    auto key { cr.rtc().our.quota.take_one() };
    OneIpSdp oneIpSdp { m.offer() };
    auto inType { oneIpSdp.ip().type() };
    auto ownIp { rtc.get_ip(inType) };
    if (!ownIp)
        throw Error(ERTCWRONGIP_FO);
    // TODO: authenticate also RTCConnections
    rtc.connections.insert(
        RTCConnection::accept_new(
            *this, [w = weak_from_this(), id = cr.id(), key, ownIp = *ownIp](RTCConnection& con, std::string sdp) {
                if (auto e { w.lock() }; e) {
                    e->defer(GeneratedSdpAnswer {
                        .ownIp { ownIp },
                        .con { con.weak_from_this() },
                        .signalingServerId = id,
                        .key = key,
                        .sdp { std::move(sdp) } });
                }
            },
            std::move(oneIpSdp)));
}

void Eventloop::handle_msg(Conref cr, RTCVerificationOffer&& m)
{
    throw_if_rtc_disabled(cr);
    OneIpSdp oneIpSdp { m.offer() };
    if (!cr.rtc().their.identity.contains(oneIpSdp.ip()))
        throw Error(ERTCIDIP);
    // TODO: check m.ip() ip was indeed offered before by us as identity
    // TODO: rate limit this function
    rtc.connections.insert(
        RTCConnection::accept_new_verification(
            *this, [w = weak_from_this(), ip = m.ip(), id = cr.id()](RTCConnection& con, std::string sdp) {
                if (auto e { w.lock() }; e) {
                    // TODO: make to always reply to cr
                    e->defer(GeneratedVerificationSdpAnswer {
                        .ownIp { ip },
                        .con { con.weak_from_this() },
                        .originConId = id,
                        .sdp { std::move(sdp) } });
                }
            },
            std::move(oneIpSdp), cr.id()));
}

void Eventloop::handle_msg(Conref cr, RTCRequestForwardAnswer&& r)
{
    throw_if_rtc_disabled(cr);
    auto fwdInfo { cr.rtc().our.pendingForwards.get(r.key()) };
    auto ip { sdp_filter::load_ip(r.answer().data) };
    if (!ip)
        throw Error(ERTCUNIQUEIP_RFA);
    if (fwdInfo.ip != ip)
        throw Error(ERTCWRONGIP_RFA);

    if (auto o { connections.find(fwdInfo.fromConId) }; o.has_value()) {
        Conref& origin { *o };
        if (origin.rtc().their.forwardRequests.is_accepted_key(fwdInfo.fromKey)) {
            origin.send(RTCForwardedAnswer(fwdInfo.fromKey, std::move(r.answer())));
        }
    }
}

void Eventloop::handle_msg(Conref cr, RTCForwardOfferDenied&& m)
{
    throw_if_rtc_disabled(cr);
    auto wcon { cr.rtc().our.pendingOutgoing.get_rtc_con(m.key()) };
    if (auto pcon { wcon.lock() })
        pcon->close(ERTCFWDREJECT); // TODO count pending per node
}

void Eventloop::handle_msg(Conref cr, RTCForwardedAnswer&& a)
{
    throw_if_rtc_disabled(cr);
    auto w { cr.rtc().our.pendingOutgoing.get_rtc_con(a.key()) };
    auto c { w.lock() };
    if (!c)
        return;
    OneIpSdp ois(a.answer());

    if (auto error { c->set_sdp_answer(ois) })
        throw Error(*error);
}

void Eventloop::handle_msg(Conref cr, RTCVerificationAnswer&& m)
{
    throw_if_rtc_disabled(cr);
    log_rtc("Received RTCVerificationAnswer");
    // TODO:
    // - clear pending on main connection close
    // - callback on connection fail
    auto& pv { cr.rtc().our.pendingVerification };
    if (!pv.has_value())
        throw Error(ERTCUNEXP_VA);
    const auto& rtcCon { pv.value().con };
    OneIpSdp ois { m.answer() };
    if (auto e { rtcCon->set_sdp_answer(ois) })
        throw Error(*e);
}

void Eventloop::try_verify_rtc_identities()
{
    assert(config().node.enableWebRTC);
    log_rtc("try_verify_rtc_identities {}", rtc.connections.can_insert_feeler());
    if (!rtc.ips.has_value() // only verify peers when you know your identity
        || rtc.verificationSchedule.empty()
        || !rtc.ips
        || rtc.ips->has_value() == false)
        return;
    while (rtc.connections.can_insert_feeler()) {
        auto p { rtc.ips->pattern() };
        auto o { rtc.verificationSchedule.pop(p) };
        bool b { o.has_value() };
        if (!b)
            return;
        auto& [c, ip] = *o;
        assert(rtc_enabled(c));
        auto newCon { RTCConnection::connect_new_verification(
            *this,
            [this, peerId = c.id()](RTCConnection& con, std::string sdp) {
                defer(GeneratedVerificationSdpOffer {
                    .con { con.weak_from_this() },
                    .peerId = peerId,
                    .sdp { std::move(sdp) } });
            },
            ip, c.id()) };
        rtc.connections.insert(newCon);
        c.rtc().our.pendingVerification.start(std::move(newCon));
    }
}

void Eventloop::consider_send_snapshot(Conref c)
{
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
    if (auto r { blockDownload.pop_stage() })
        stateServer.async_stage_request(*r);
}

void Eventloop::async_push_rogue(const RogueHeaderData& rogueHeaderData)
{
    defer(PushRogue { std::move(rogueHeaderData) });
}

void Eventloop::async_stage_action(stage_operation::Result r)
{
    defer(std::move(r));
}

void Eventloop::verify_rollback(Conref cr, const SignedPinRollbackMsg& m)
{
    if (cr.chain().descripted()->chain_length() <= m.shrinkLength())
        throw Error(EBADROLLBACKLEN);
    auto& ss = m.signedSnapshot();
    if (cr.chain().stage_fork_range().lower() > ss.priority.height) {
        if (ss.compatible(chains.stage_headers()))
            throw Error(EBADROLLBACK);
    } else if (cr.chain().consensus_fork_range().lower() > ss.priority.height) {
        if (ss.compatible(chains.consensus_state().headers()))
            throw Error(EBADROLLBACK);
    }
}

Result<Conref> Eventloop::try_insert_connection(OnHandshakeCompleted&& m)
{
    auto c { m.convar.base() };
    c->eventloop_registered = true;

    if (m.convar.is_rtc()) {
        if (config().node.enableWebRTC)
            tl::make_unexpected(ERTCDISABLED);
        auto& c { m.convar.get_rtc() };
        auto& conId { c->verification_con_id() };
        if (conId != 0) { // conId id verified in this RTC connection
            if (auto o { connections.find(conId) }) {
                auto ip { c->native_peer_addr().ip };
                auto& parent = *o;
                parent.rtc().their.identity.set_verified(ip);
                log_rtc("verified RTC ip {} for {}", ip.to_string(), parent.peer().to_string());
            }
            conId = 0;
            return Error(ERTCFEELER);
        }
    }

    ConnectionInserter h(*this);
    return connections.insert(m.convar, h);
}
void Eventloop::update_sync_state()
{
    syncState.set_has_connections(!connections.initialized().empty());
    syncState.set_block_download(blockDownload.is_active());
    syncState.set_header_download(headerDownload.is_active());
    if (auto c { syncState.detect_change() }; c) {
        global().chainServer->async_set_synced(c.value());
    }
}

void Eventloop::set_scheduled_connect_timer()
{
    auto t { connections.pop_scheduled_connect_time() };
    if (!t)
        return;
    auto& tp { *t };

    if (wakeupTimer) {
        if (wakeupTimer->wakeup_tp() <= tp)
            return;
        timerSystem.erase(*wakeupTimer);
    }
    timerSystem.insert(tp, TimerEvent::ScheduledConnect {});
}

// Functions that don't exist in browser nodes
#ifndef DISABLE_LIBUV
void Eventloop::start_dns_request(const Hostname& hostname)
{
    auto callback {
        [w = weak_from_this()](DnsResolveResult r) {
            if (auto e { w.lock() })
                e->on_dns_resolve(std::move(r));
        }
    };
    global().conman->async_dns_resolve({ hostname, std::move(callback) });
}
#endif
