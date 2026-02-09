#pragma once
#ifndef DISABLE_LIBUV
#include "connect_request.hpp"
#include "transport/dns/request.hpp"
#include "general/move_only_function.hpp"
#include "peerserver/peerserver.hpp"
#include "uvw.hpp"
#include <set>

struct ConfigParams;
struct Inspector;
class TCPConnection;
class PeerServer;

class TCPConnectionManager : public std::enable_shared_from_this<TCPConnectionManager> {
    static constexpr size_t max_conn_per_ip = 3;
    friend class TCPConnection;
    friend class Reconnecter;
    friend class PeerServer;
    friend struct Inspector;

private:
    //////////////////////////////
    // Callers (static libuv callback functions)
    static void new_connection_caller(uv_stream_t* server, int status);
    static void close_caller(uv_handle_t* handle);
    static void reconnect_caller(uv_timer_t* handle);
    static void reconnect_closed_cb(uv_handle_t* handle);

    //////////////////////////////
    // Private methods
    std::shared_ptr<TCPConnection> insert_connection(std::shared_ptr<uvw::tcp_handle> h, const TCPConnectRequest& r);
    void on_wakeup();

    // ip counting
    bool count(IPv4);
    void uncount(IPv4);

    // reference counting
    struct APIPeerdata {
        TCPPeeraddr address;
        uint32_t since;
    };
    using PeersCB = MoveOnlyFunction<void(std::vector<APIPeerdata>)>;

    struct Token {
    };
    struct GetPeers {
        PeersCB cb;
    };
    using Connect = TCPConnectRequest;
    struct Inspect {
        MoveOnlyFunction<void(const TCPConnectionManager&)> callback;
    };
    struct DeferFunc {
        MoveOnlyFunction<void()> callback;
    };
    using Event = std::variant<GetPeers, DnsResolveRequest, Connect, Inspect, DeferFunc>;

public:
    void async_get_peers(PeersCB cb)
    {
        async_add_event(GetPeers { std::move(cb) });
    }
    void async_dns_resolve(DnsResolveRequest r)
    {
        async_add_event(std::move(r));
    }
    void connect(const TCPConnectRequest& cr)
    {
        async_add_event(Connect { cr });
    }
    void async_inspect(MoveOnlyFunction<void(const TCPConnectionManager&)>&& cb)
    {
        async_add_event(Inspect { std::move(cb) });
    }
    // auto& loop() { return tcp->parent(); }

    TCPConnectionManager(Token, std::shared_ptr<uvw::loop> l, PeerServer& peerdb, const ConfigParams&);
    [[nodiscard]] static auto make_shared(std::shared_ptr<uvw::loop> l, PeerServer& peerdb, const ConfigParams& cp)
    {
        return std::make_shared<TCPConnectionManager>(Token {}, std::move(l), peerdb, cp);
    }
    void shutdown(Error reason);

private:
    void async_call(MoveOnlyFunction<void()>&& cb)
    {
        async_add_event(DeferFunc { std::move(cb) });
    }
    auto& loop() const { return listener->parent(); }

    const TCPPeeraddr bindAddress;
    //--------------------------------------
    // data accessed by libuv thread
    std::set<std::shared_ptr<TCPConnection>> tcpConnections;
    bool closing = false;
    std::shared_ptr<uvw::tcp_handle> listener;
    std::shared_ptr<uvw::async_handle> wakeup;

    void async_add_event(Event e)
    {
        std::unique_lock<std::mutex> lock(mutex);
        events.push(std::move(e));
        wakeup->send();
    }
    //--------------------------------------
    // uv-mutex  locked shared members (message queues for uv thread)
    std::mutex mutex;
    std::queue<Event> events;

    // handle_event functions
    void handle_event(GetPeers&&);
    void handle_event(DnsResolveRequest&&);
    void handle_event(Connect&&);
    void handle_event(Inspect&&);
    void handle_event(DeferFunc&&);
};
#endif
