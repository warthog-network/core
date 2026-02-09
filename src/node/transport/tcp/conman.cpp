#include "conman.hpp"
#include "connection.hpp"
#include "eventloop/eventloop.hpp"
#include "global/globals.hpp"
#include "uvw.hpp"
#include <memory>
namespace {

[[nodiscard]] TCPPeeraddr get_ipv4_endpoint(const sockaddr_in& addr_in4)
{
    return TCPPeeraddr(IPv4(ntoh32(uint32_t(addr_in4.sin_addr.s_addr))), addr_in4.sin_port);
}
[[nodiscard]] wrt::optional<TCPPeeraddr> get_ipv4_endpoint(const uvw::tcp_handle& handle)
{
    sockaddr_storage storage;
    int alen = sizeof(storage);
    if (auto e { uv_tcp_getpeername(handle.raw(), (struct sockaddr*)&storage, &alen) }; e != 0) {
        spdlog::error("Bad uv_tcp_getpeername result: {}", e);
        return {};
    }
    if (storage.ss_family != AF_INET)
        return {};
    return get_ipv4_endpoint(*(struct sockaddr_in*)&storage);
}
}

std::shared_ptr<TCPConnection> TCPConnectionManager::insert_connection(std::shared_ptr<uvw::tcp_handle> tcpHandle, const TCPConnectRequest& r)
{
    assert(tcpHandle != nullptr);
    using sh_con_t = std::shared_ptr<TCPConnection>;
    sh_con_t con { TCPConnection::make_new(std::move(tcpHandle), r, *this) };
    auto& tcp { con->tcpHandle };
    auto iter { tcpConnections.insert(con).first };
    tcp->data(con);
    tcp->on<uvw::close_event>([this, iter](const uvw::close_event&, uvw::tcp_handle& client) {
        client.data(nullptr);
        tcpConnections.erase(iter);
    });
    tcp->on<uvw::end_event>([](const uvw::end_event&, uvw::tcp_handle& client) {
        client.data<TCPConnection>()->close_internal(UV_EOF);
    });
    tcp->on<uvw::error_event>([](const uvw::error_event& e, uvw::tcp_handle& client) {
        client.data<TCPConnection>()->close_internal(e.code());
    });
    tcp->on<uvw::shutdown_event>([](const uvw::shutdown_event&, uvw::tcp_handle& client) { client.close(); });
    tcp->on<uvw::data_event>([](const uvw::data_event& de, uvw::tcp_handle& client) {
        client.data<TCPConnection>()->on_message({ reinterpret_cast<uint8_t*>(de.data.get()), de.length });
    });
    return con;
};

TCPConnectionManager::TCPConnectionManager(Token, std::shared_ptr<uvw::loop> loop, PeerServer& ps, const ConfigParams& cfg)
    : bindAddress(cfg.node.bind)
{
    listener = loop->resource<uvw::tcp_handle>();
    assert(listener);
    listener->on<uvw::error_event>([](const uvw::error_event& e, uvw::tcp_handle&) {
        spdlog::error("TCP listener error {}", e.name());
    });
    listener->on<uvw::listen_event>([this, &ps](const uvw::listen_event&, uvw::tcp_handle& server) {
        if (config().node.isolated)
            return;
        std::shared_ptr<uvw::tcp_handle> tcpHandle = server.parent().resource<uvw::tcp_handle>();
        assert(server.accept(*tcpHandle) == 0);
        auto endpoint { get_ipv4_endpoint(*tcpHandle) };
        if (endpoint) {
            auto connectRequest { TCPConnectRequest::make_inbound(*endpoint) };
            auto connection { insert_connection(tcpHandle, connectRequest) };
            ps.authenticate_inbound(endpoint.value().ip, TransportType::TCP, std::move(connection));
        }
    });
    wakeup = loop->resource<uvw::async_handle>();
    assert(wakeup);
    wakeup->on<uvw::async_event>([&](uvw::async_event&, uvw::async_handle&) {
        on_wakeup();
    });

    spdlog::info("P2P endpoint is {}.", bindAddress.to_string());
    int i = 0;
    if ((i = listener->bind(bindAddress)) || (i = listener->listen()))
        throw std::runtime_error(
            "Cannot start connection manager: " + std::string(Error(i).err_name()));
    assert(0 == listener->listen());
}
void TCPConnectionManager::on_wakeup()
{
    decltype(events) tmp;
    { // lock for very short time (swap)
        std::unique_lock<std::mutex> lock(mutex);
        std::swap(tmp, events);
    }

    while (!tmp.empty()) {
        std::visit([&](auto& e) { handle_event(std::move(e)); }, tmp.front());
        tmp.pop();
    }
}

void TCPConnectionManager::handle_event(DnsResolveRequest&& e)
{

    auto req = loop().resource<uvw::get_addr_info_req>();

    struct addrinfo hints {};
    hints.ai_family = AF_INET; // IPv4 only
    hints.ai_socktype = SOCK_STREAM;

    auto hostname { e.hostname };
    req->data(std::make_shared<DnsResolveRequest>(std::move(e)));

    req->on<uvw::error_event>([](const uvw::error_event& e, const uvw::get_addr_info_req& h) {
        auto& r { (*h.data<DnsResolveRequest>()) };
        r.callback({ r.hostname, Error(e.code()) });
        spdlog::error("DNS lookup error for {}:  {}", r.hostname, e.name());
    });
    req->on<uvw::addr_info_event>(
        [](uvw::addr_info_event& addrInfo, uvw::get_addr_info_req& h) {
            auto d { h.data<DnsResolveRequest>() };
            std::vector<IPv4> matches;
            for (auto p { addrInfo.data.get() }; p != nullptr; p = p->ai_next) {
                if (p->ai_family == AF_INET) {
                    auto ep { get_ipv4_endpoint(*reinterpret_cast<const sockaddr_in*>(p->ai_addr)) };
                    matches.push_back(ep.ip);
                }
            }
            if (matches.size() == 0) {
                d->callback({ d->hostname, Error(ENOIPV4RESOLVABLE) });
            } else {
                d->callback(DnsResolveResult { d->hostname, std::move(matches) });
            }
        });

    req->node_addr_info(hostname, &hints);
}

void TCPConnectionManager::handle_event(GetPeers&& e)
{
    std::vector<APIPeerdata> data;
    for (auto c : tcpConnections) {
        data.push_back({ c->peer_addr_native(), c->created_at_timestmap() });
    }
    e.cb(std::move(data));
}

void TCPConnectionManager::handle_event(Connect&& c)
{
    TCPConnectRequest& r { c };
    auto& loop { listener->parent() };
    auto tcp { loop.resource<uvw::tcp_handle>() };
    auto con { insert_connection(tcp, r) };
    connection_log().info("{} connecting", con->tag_string());
    tcp->on<uvw::connect_event>([req = r, w = weak_from_this()](const uvw::connect_event&, uvw::tcp_handle& tcp) {
        auto cm { w.lock() };
        if (!cm)
            return;
        auto connection { tcp.data<TCPConnection>() };
        connection->start_read();
        global().peerServer->log_outbound(req.address().ip, std::move(connection));
    });

    if (auto err { tcp->connect(r.address().sock_addr()) }; err) {
        Error e(err);
        connection_log().info("{} cannot connect: {} ({})", con->tag_string(), e.err_name(), e.strerror());
        global().core->on_failed_connect(r, e);
        return;
    }
}

void TCPConnectionManager::handle_event(Inspect&& e)
{
    e.callback(*this);
}

void TCPConnectionManager::handle_event(DeferFunc&& f)
{
    f.callback();
}

void TCPConnectionManager::shutdown(Error reason)
{
    if (closing == true)
        return;
    closing = true;
    wakeup->close();
    listener->close();
    for (auto& c : tcpConnections)
        c->close_internal(reason);
}
