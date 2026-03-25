#pragma once
#include "ban_cache.hpp"
#include "db/peer_db.hpp"
#include "wrt/expected.hpp"
#include "general/result.hpp"
#include "transport/connection_base.hpp"
#include "transport/helpers/transport_types.hpp"
#include <condition_variable>
#include <thread>

struct Inspector;
struct ConfigParams;

class PeerServer {
public:
    struct OnClose {
        std::shared_ptr<peerserver::ConnectionData> con;
        Error offense;
    };
    using banned_callback_t = std::function<void(const std::vector<PeerDB::BanEntry>&)>;
    using offenses_callback_t = std::function<void(const Result<std::vector<OffenseEntry>>&)>;
    using result_callback_t = std::function<void(const Error&)>;

private:
    friend struct Inspector;
    struct LogOutboundIPv4 {
        IPv4 ip;
        std::shared_ptr<ConnectionBase> c;
    };
    struct AuthenticateInbound {
        IP ip;
        TransportType transportType;
        // std::variant<std::shared_ptr<TCPConnection>, std::shared_ptr<WS
        std::shared_ptr<AuthenticatableConnection> c;
    };
    struct Unban {
        result_callback_t cb;
    };

    struct GetOffenses {
        Page page;
        offenses_callback_t cb;
    };

public:
    bool on_close(std::shared_ptr<peerserver::ConnectionData> con, Error offense)
    {
        if (offense.code == EREFUSED)
            return false;
        return async_event(OnClose { std::move(con), offense });
    }
    void shutdown()
    {
        std::unique_lock<std::mutex> l(mutex);
        _shutdown = true;
        hasWork = true;
        cv.notify_one();
    }
    void wait_for_shutdown();

    bool log_outbound(IPv4 ip, std::shared_ptr<ConnectionBase> c)
    {
        return async_event(LogOutboundIPv4 { std::move(ip), std::move(c) });
    }
    bool authenticate_inbound(IP ip, TransportType type, std::shared_ptr<AuthenticatableConnection> c)
    {
        return async_event(AuthenticateInbound { std::move(ip), type, std::move(c) });
    }

    bool async_get_banned(banned_callback_t cb)
    {
        return async_event(cb);
    };
    bool async_unban(result_callback_t cb)
    {
        return async_event(Unban { std::move(cb) });
    }
    bool async_get_offenses(Page page, offenses_callback_t cb)
    {
        return async_event(GetOffenses { page, std::move(cb) });
    }
    bool async_register_peer(TCPPeeraddr a)
    {
        return async_event(RegisterPeer { a });
    }
    bool async_seen_peer(TCPPeeraddr a)
    {
        return async_event(SeenPeer { a });
    }
    bool async_get_recent_peers(
        std::function<void(std::vector<std::pair<TCPPeeraddr, Timestamp>>&&)>&& cb,
        size_t maxEntries = 100)
    {
        return async_event(GetRecentPeers { std::move(cb), maxEntries });
    }
    bool async_inspect(std::function<void(const PeerServer&)>&& cb)
    {
        return async_event(Inspect { std::move(cb) });
    }

    PeerServer(PeerDB& db, const ConfigParams&);
    ~PeerServer()
    {
        shutdown();
        wait_for_shutdown();
    }
    void start();

private:
    struct RegisterPeer {
        TCPPeeraddr a;
    };
    struct SeenPeer {
        TCPPeeraddr a;
    };
    struct GetRecentPeers {
        std::function<void(std::vector<std::pair<TCPPeeraddr, Timestamp>>&&)> cb;
        size_t maxEntries;
    };
    struct Inspect {
        std::function<void(const PeerServer&)> cb;
    };
    using Event = std::variant<OnClose, LogOutboundIPv4, AuthenticateInbound, GetOffenses, Unban, banned_callback_t, RegisterPeer, SeenPeer, GetRecentPeers, Inspect>;
    bool async_event(Event e)
    {
        std::unique_lock<std::mutex> l(mutex);
        if (_shutdown)
            return false;
        events.push(std::move(e));
        hasWork = true;
        cv.notify_one();
        return true;
    }
    void work();
    void accept_connection();
    ////////////////
    //
    // private variables
    PeerDB& db;
    uint32_t now;
    BanCache bancache;
    void handle_event(OnClose&&);
    void handle_event(Unban&&);
    void handle_event(GetOffenses&&);
    void handle_event(AuthenticateInbound&&);
    void handle_event(LogOutboundIPv4&&);
    void handle_event(banned_callback_t&&);
    void handle_event(RegisterPeer&&);
    void handle_event(SeenPeer&&);
    void handle_event(GetRecentPeers&&);
    void handle_event(Inspect&&);

    void on_close_internal(const OnClose&, const WebRTCPeeraddr&);
#ifndef DISABLE_LIBUV
    void on_close_internal(const OnClose&, const Sockaddr&);
#else
    void on_close_internal(const OnClose&, const WSUrladdr&);
#endif

    ////////////////
    // Mutex protected variables
    std::mutex mutex;
    bool hasWork = false;
    bool enableBan;
    bool _shutdown = false;
    std::queue<Event> events;
    std::condition_variable cv;

    // worker
    std::thread worker;
};
