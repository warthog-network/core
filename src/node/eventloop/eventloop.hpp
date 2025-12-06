#pragma once
#include "address_manager/address_manager.hpp"
#include "serialization/vector.hpp"
#include "api/callbacks.hpp"
#include "api/events/subscription_fwd.hpp"
#include "block/chain/height_header_work.hpp"
#include "block/chain/offender.hpp"
#include "block/chain/signed_snapshot.hpp"
#include "chain_cache.hpp"
#include "chainserver/state/update/update.hpp"
#include "communication/stage_operation/result.hpp"
#include "eventloop/sync/block_download/block_download.hpp"
#include "eventloop/sync/header_download/header_download.hpp"
#include "eventloop/sync/request_sender_declaration.hpp"
#include "eventloop/timer.hpp"
#include "eventloop/types/rtc/rtc_state.hpp"
#include "general/move_only_function.hpp"
#include "general/time_utils.hpp"
#include "mempool/mempool.hpp"
#include "mempool/subscription_declaration.hpp"
#include "peerserver/peerserver.hpp"
#include "sync/sync_state.hpp"
#include "timer_element.hpp"
#include "transport/connection_base.hpp"
#include "types/chainstate.hpp"
#include "types/conndata.hpp"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

class RTCPendingOutgoing;
class RTCPendingIncoming;
class TCPConnection;
class WSConnection;
class RTCConnection;
class Rcvbuffer;
class Reader;
class Eventprocessor;
class EndAttorney;
class IP;
struct ConfigParams;

struct ForkMsg;
struct AppendMsg;
struct Inspector;

class ChainServer;
namespace BlockDownload {
class Attorney;
}

class RTCData {
    friend class RTCConnection;

protected:
    RTCState rtc;
};
class ConnectionInserter;
class ConState;
namespace TimerEvent = eventloop::timer_events;
class Eventloop final : public std::enable_shared_from_this<Eventloop>, public RTCData {
    using duration = std::chrono::steady_clock::duration;
    using seconds = std::chrono::seconds;
    using StateUpdate = chainserver::state_update::StateUpdate;
    using TimerSystem = eventloop::TimerSystem;
    using Timer = eventloop::Timer;
    friend class BlockDownload::Attorney;
    friend class ConnectionInserter;
    friend class EndAttorney;
    friend class ConState;

    struct SyncTiming {
        timing::Tic startedAt;
    };
    struct Token { };
    struct GeneratedVerificationSdpOffer {
        std::weak_ptr<RTCConnection> con;
        uint64_t peerId;
        std::string sdp;
    };
    struct GeneratedVerificationSdpAnswer {
        IP ownIp;
        std::weak_ptr<RTCConnection> con;
        uint64_t originConId;
        std::string sdp;
    };
    struct GeneratedSdpOffer {
        std::weak_ptr<RTCConnection> con;
        uint64_t signalingServerId;
        uint64_t signalingListKey;
        std::string sdp;
    };
    struct GeneratedSdpAnswer {
        IP ownIp;
        std::weak_ptr<RTCConnection> con;
        uint64_t signalingServerId;
        size_t key;
        std::string sdp;
    };

public:
    Eventloop(Token, PeerServer& ps, ChainServer& ss, const ConfigParams& config);
    struct StartTimer {
        std::chrono::steady_clock::time_point wakeup;
        MoveOnlyFunction<void()> on_expire;
        MoveOnlyFunction<void(TimerElement)> on_timerstart;
    };
    friend struct Inspector;
    static std::shared_ptr<Eventloop> create(PeerServer& ps, ChainServer& ss, const ConfigParams& config);
    ~Eventloop();

    // API callbacks
    using SignedSnapshotCb = std::function<void(const Result<SignedSnapshot>&)>;
    using InspectorCb = std::function<void(const Eventloop&)>;

    /////////////////////
    // Async functions
    // called by other threads

    bool async_process(std::shared_ptr<ConnectionBase> c);
    bool on_handshake_completed(ConnectionBase::ConnectionVariant con);
    void api_get_hashrate(HashrateCb&& cb, size_t n = 100);
    void api_get_hashrate_block_chart(NonzeroHeight from, NonzeroHeight to, size_t window, HashrateBlockChartCb&& cb);
    void api_get_hashrate_time_chart(uint32_t from, uint32_t to, size_t window, HashrateTimeChartCb&& cb);
    void async_push_rogue(const RogueHeaderData&);
    void api_get_peers(PeersCb&& cb);
    void api_get_throttled(ThrottledCb&& cb);
    void api_loadtest_block(uint64_t conId, ErrorCb);
    void api_loadtest_header(uint64_t conId, ErrorCb);
    void api_loadtest_disable(uint64_t conId, ErrorCb);
    void api_disconnect_peer(uint64_t id, ErrorCb&& cb);
    void api_is_synced(SyncedCb&& cb);
    void api_inspect(InspectorCb&&);
    void api_count_ips(IpCounterCb&&);
    void api_get_connection_schedule(JSONCb&& cb);
    void api_sample_verified_peers(size_t n, SampledPeersCb cb);
    void start_timer(StartTimer);
    void cancel_timer(TimerSystem::key_t);

    // subscription methods
    void subscribe_connection_event(SubscriptionRequest r);
    void destroy_subscriptions(subscription_data_ptr p);

    void async_mempool_update(mempool::Updates&& s);
    void shutdown(Error reason);
    void wait_for_shutdown();
    void async_stage_action(stage_operation::Result);
    void async_state_update(StateUpdate&& s);
    void notify_closed_rtc(std::shared_ptr<RTCConnection> rtc);

    void erase(std::shared_ptr<ConnectionBase> c, Error);
    void on_failed_connect(const ConnectRequest& r, Error reason);
    void on_outbound_closed(std::shared_ptr<ConnectionBase>, Error reason);

    void start();

private:
    std::vector<TCPPeeraddr> get_db_peers(size_t num);
    //////////////////////////////
    // Important event loop functions
    void loop();
    bool has_work();
    void work();
    bool check_shutdown();
    void process_connection(std::shared_ptr<ConnectionBase> c);

    //////////////////////////////
    // Private async functions

    void async_forward_blockrep(uint64_t conId, std::vector<BodyData>&& blocks);

    //////////////////////////////
    // Connection related functions
    void erase_internal(Conref cr, Error);
    void close(Conref cr, Error reason);
    void close_by_id(uint64_t connectionId, Error reason);
    void close(const ChainOffender&);
    void close(Conref cr, ChainError);
    void report(const ChainOffender&) { };

    ////////////////////////
    // Handling incoming messages
    void dispatch_message(Conref cr, messages::Msg&& rb);
    void process_message(Conref cr, Rcvbuffer& rb);
    void handle_msg(Conref cr, PingMsg&&);
    void handle_msg(Conref cr, PingV2Msg&&);
    void handle_msg(Conref cr, PongMsg&&);
    void handle_msg(Conref cr, PongV2Msg&&);
    void handle_msg(Conref cr, BatchreqMsg&&);
    void handle_msg(Conref cr, BatchrepMsg&&);
    void handle_msg(Conref cr, ProbereqMsg&&);
    void handle_msg(Conref cr, ProberepMsg&&);
    void handle_msg(Conref cr, BlockreqMsg&&);
    void handle_msg(Conref cr, BlockrepMsg&&);
    void handle_msg(Conref cr, InitMsgV1&&);
    void handle_msg(Conref cr, InitMsgV3&&);
    void handle_msg(Conref cr, AppendMsg&&);
    void handle_msg(Conref cr, SignedPinRollbackMsg&&);
    void handle_msg(Conref cr, ForkMsg&&);
    void handle_msg(Conref cr, TxnotifyMsg&&);
    void handle_msg(Conref cr, TxreqMsg&&);
    void handle_msg(Conref cr, TxrepMsg&&);
    void handle_msg(Conref cr, LegacyTxrepMsg&&);
    void handle_msg(Conref cr, LeaderMsg&&);
    void handle_msg(Conref cr, RTCIdentity&&);
    void handle_msg(Conref cr, RTCQuota&&);
    void handle_msg(Conref cr, RTCSignalingList&&);
    void handle_msg(Conref cr, RTCRequestForwardOffer&&);
    void handle_msg(Conref cr, RTCForwardedOffer&&);
    void handle_msg(Conref cr, RTCRequestForwardAnswer&&);
    void handle_msg(Conref cr, RTCForwardedAnswer&&);
    void handle_msg(Conref cr, RTCForwardOfferDenied&&);
    void handle_msg(Conref cr, RTCVerificationOffer&&);
    void handle_msg(Conref cr, RTCVerificationAnswer&&);

    ////////////////////////
    // convenience functions
    void consider_send_snapshot(Conref);
    void on_received_addresses(Conref cr, const serialization::Vector16<TCPPeeraddr>&);

    void send_schedule_signaling_lists();

    ////////////////////////
    // assign work to connections
    void do_loadtest_requests();
    void do_requests();
    void send_requests(Conref cr, const std::vector<Request>&);

    ////////////////////////
    // send functions
    template <typename T>
    void send_request(Conref cr, const T& req);

    friend class RequestSender;
    RequestSender sender() { return RequestSender(*this); };
    void send_init(Conref cr);

    /////////////////////////////
    /// RTC verification
    void try_verify_rtc_identities();

    ////////////////////////
    // Handling timeout events
    void handle_expired(TimerSystem::Event&& data);
    void expired_pingsleep(Conref cr);
    void expired_init(Conref cr);

    ////////////////////////
    // Timer functions
    void send_ping_await_pong(Conref cr);
    void received_pong_sleep_ping(Conref cr);

    ////////////////////////
    // Timeout callbacks
    template <typename T>
    requires std::derived_from<T, TimerEvent::WithConnecitonId>
    void handle_timeout(T&&);
    void handle_connection_timeout(Conref, TimerEvent::SendPing&&);
    void handle_connection_timeout(Conref, TimerEvent::ThrottledProcessMsg&&);
    void handle_connection_timeout(Conref, TimerEvent::Expire&&);
    void handle_connection_timeout(Conref, TimerEvent::CloseNoReply&&);
    void handle_connection_timeout(Conref, TimerEvent::CloseNoPong&&);
    void handle_timeout(TimerEvent::ScheduledConnect&&);
    void handle_timeout(TimerEvent::CallFunction&&);
    void handle_timeout(TimerEvent::SendIdentityIps&&);
    void on_request_expired(Conref cr, const Proberequest&);
    void on_request_expired(Conref cr, const HeaderRequest&);
    void on_request_expired(Conref cr, const BlockRequest&);

    ////////////////////////
    // blockdownload result
    void process_blockdownload_stage();

    ////////////////////////
    // event types
    struct Erase {
        std::shared_ptr<ConnectionBase> c;
        Error reason;
    };
    using OutboundClosed = AddressManager::OutboundClosedEvent;
    struct OnHandshakeCompleted {
        ConnectionBase::ConnectionVariant convar;
    };
    struct OnProcessConnection {
        std::shared_ptr<ConnectionBase> c;
    };
    struct OnForwardBlockrep {
        uint64_t conId;
        std::vector<BodyData> blocks;
    };
    struct GetHashrateBlockChart {
        HashrateBlockChartCb cb;
        NonzeroHeight from;
        NonzeroHeight to;
        size_t window;
    };
    struct GetHashrateTimeChart {
        HashrateTimeChartCb cb;
        uint32_t from;
        uint32_t to;
        size_t window;
    };
    struct GetHashrate {
        HashrateCb cb;
        size_t n;
    };
    struct GetConnectionSchedule {
        JSONCb cb;
    };
    struct FailedConnect {
        ConnectRequest connectRequest;
        Error reason;
    };
    struct CancelTimer {
        TimerSystem::key_t key;
    };
    struct RTCClosed { // RTC connection closed
        std::shared_ptr<RTCConnection> con;
    };
    struct SubscribeConnections : public SubscriptionRequest {
    };
    struct DestroySubscriptions {
        subscription_data_ptr p;
    };

    struct DisconnectPeer {
        uint64_t id;
        ErrorCb cb;
    };
    struct SampleVerifiedPeers {
        size_t n;
        SampledPeersCb cb;
    };

    struct GetPeers {
        PeersCb callback;
    };
    struct GetThrottled {
        ThrottledCb callback;
    };
    struct Loadtest {
        uint64_t connId;
        wrt::optional<RequestType> requestType;
        ErrorCb callback;
    };
    struct PushRogue {
        RogueHeaderData rogueHeaderData;
    };

    // event queue
    using Event = std::variant<Erase, OutboundClosed, OnHandshakeCompleted, OnProcessConnection,
        StateUpdate, SignedSnapshotCb, GetPeers, GetThrottled, SyncedCb, IpCounterCb, stage_operation::Result,
        OnForwardBlockrep, InspectorCb, GetHashrate, GetHashrateBlockChart, GetHashrateTimeChart, GetConnectionSchedule, FailedConnect,
        mempool::Updates, StartTimer, CancelTimer, RTCClosed, IdentityIps, GeneratedVerificationSdpOffer, GeneratedVerificationSdpAnswer, GeneratedSdpOffer, GeneratedSdpAnswer, SubscribeConnections, DestroySubscriptions, DisconnectPeer, SampleVerifiedPeers, Loadtest, PushRogue>;

public:
    bool defer(Event e);

private:
    // event handlers
    void handle_event(Erase&&);
    void handle_event(OutboundClosed&&);
    void handle_event(OnHandshakeCompleted&&);
    void handle_event(OnProcessConnection&&);
    void handle_event(StateUpdate&&);
    void handle_event(SignedSnapshotCb&&);
    void handle_event(GetPeers&&);
    void handle_event(GetThrottled&&);
    void handle_event(SyncedCb&&);
    void handle_event(stage_operation::Result&&);
    void handle_event(OnForwardBlockrep&&);
    void handle_event(InspectorCb&&);
    void handle_event(IpCounterCb&&);
    void handle_event(GetHashrate&&);
    void handle_event(GetHashrateBlockChart&&);
    void handle_event(GetHashrateTimeChart&&);
    void handle_event(GetConnectionSchedule&&);
    void handle_event(FailedConnect&&);
    void handle_event(mempool::Updates&&);
    void handle_event(StartTimer&&);
    void handle_event(CancelTimer&&);
    void handle_event(RTCClosed&&);
    void handle_event(IdentityIps&&);
    void handle_event(GeneratedVerificationSdpOffer&&);
    void handle_event(GeneratedVerificationSdpAnswer&&);
    void handle_event(GeneratedSdpOffer&&);
    void handle_event(GeneratedSdpAnswer&&);
    void handle_event(SubscribeConnections&&);
    void handle_event(DestroySubscriptions&&);
    void handle_event(DisconnectPeer&&);
    void handle_event(SampleVerifiedPeers&&);
    void handle_event(Loadtest&&);
    void handle_event(PushRogue&&);

    // throttling
    size_t ratelimit_spare();

    // chain updates
    using Append = chainserver::state_update::Append;
    using Fork = chainserver::state_update::Fork;
    using RollbackData = chainserver::state_update::SignedSnapshotApply;
    void update_chain(Append&&);
    void update_chain(Fork&&);
    void update_chain(RollbackData&&);
    void coordinate_sync();

    // load test
    void try_start_loadtest(Conref cr);

    void try_start_sync_timing();
    void initialize_block_download();
    ForkHeight set_stage_headers(Headerchain&&);

    // log
    void log_chain_length();

    // checkers
    void verify_rollback(Conref, const SignedPinRollbackMsg&); // throws

    ////////////////////////
    // convenience functions
    const ConsensusSlave& consensus() { return chains.consensus_state(); }
    Result<Conref> try_insert_connection(OnHandshakeCompleted&&);

    ////////////////////////
    // register sync state
    void update_sync_state();

    void set_scheduled_connect_timer();

private: // private data
    std::chrono::steady_clock::time_point startedAt;
    ChainServer& stateServer;
    // Conndatamap connections;
    StageAndConsensus chains;
    mempool::MempoolTransactions mempool; // copy of chainserver mempool
    wrt::optional<SyncTiming> syncTiming;

    AddressManager connections;

    TimerSystem timerSystem;
    wrt::optional<Timer> wakeupTimer;

    // Request related
    size_t activeRequests = 0;
    size_t maxRequests = 10;

    //
    auto signed_snapshot() const { return chains.signed_snapshot(); };
    HeaderDownload::Downloader headerDownload;
    BlockDownload::Downloader blockDownload;
    mempool::SubscriptionMap mempoolSubscriptions;
    SyncState syncState;
    std::vector<subscription_ptr> connectionSubscriptions;
    void emit_disconnect(size_t, uint64_t);
    void emit_connect(size_t, Conref);

    ////////////////////////////
    // mutex protected varibales
    ////////////////////////////
    std::condition_variable cv;
    std::mutex mutex;
    bool haswork = false;
    wrt::optional<Error> closeReason {};
    bool blockdownloadHalted = false;
    std::queue<Event> events;
    std::thread worker; // worker (constructed last)
};

template <typename T>
requires std::derived_from<T, IsRequest>
void RequestSender::send(Conref cr, const T& req)
{
    e.send_request(cr, req);
}

inline bool RequestSender::finished()
{
    return e.maxRequests <= e.activeRequests;
}
