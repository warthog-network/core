#include "address_manager.hpp"
#include "api/types/shared.hpp"
#include "global/globals.hpp"
#include <nlohmann/json.hpp>
#ifndef DISABLE_LIBUV
#include "transport/tcp/connection.hpp"
#endif
#include <algorithm>
#include <random>

AddressManager::OutboundClosedEvent::OutboundClosedEvent(std::shared_ptr<ConnectionBase> c, Error reason)
    : c(std::move(c))
    , reason(reason)
{
    assert(!this->c->inbound());
}

#ifndef DISABLE_LIBUV
AddressManager::AddressManager(InitArg ia)
    : tcpConnectionSchedule(std::move(ia))
#else
AddressManager::AddressManager(InitArg ia)
    : wsConnectionSchedule(std::move(ia))
#endif
{
}

auto AddressManager::start() -> StartResult
{
    StartResult out;
#ifndef DISABLE_LIBUV
    out.dnsRequests = tcpConnectionSchedule.initialize();
#endif
    start_scheduled_connections();
    return out;
};

void AddressManager::outbound_closed(OutboundClosedEvent e)
{
    if (auto cr { e.c->connect_request() }) {
#ifndef DISABLE_LIBUV
        tcpConnectionSchedule.on_outbound_disconnected(*cr,
            e.reason,
            e.c->addedToSchedule);
#else
        wsConnectionSchedule.outbound_closed(*cr,
            e.reason,
            e.c->addedToSchedule);
#endif
    }
}

void AddressManager::on_dns_resolve(const DnsResolveResult& r){
    tcpConnectionSchedule.on_dns_resolve(r);
}

auto AddressManager::to_json() const -> json
{
    return {
#ifndef DISABLE_LIBUV
        { "tcpAddresses", tcpConnectionSchedule.to_json() },
#else
    // {"wsAddresses",wsConnectionSchedule.to_json()},
#endif
    };
}

void AddressManager::start_scheduled_connections()
{
    if (config().node.isolated)
        return;
#ifndef DISABLE_LIBUV
    tcpConnectionSchedule.connect_expired();
#else
    wsConnectionSchedule.connect_expired();
#endif
}

#ifndef DISABLE_LIBUV
void AddressManager::verify(std::vector<TCPPeeraddr> v, IPv4 source)
{
    for (auto& ea : v)
        tcpConnectionSchedule.add_feeler(ea, source);
}

void AddressManager::outbound_failed(const TCPConnectRequest& r, Error e)
{
    tcpConnectionSchedule.on_outbound_failed(r, e);
}

#else

void AddressManager::outbound_failed(const WSBrowserConnectRequest& r, Error)
{
    wsConnectionSchedule.outbound_failed(r);
}
#endif

wrt::optional<Conref> AddressManager::find(uint64_t id) const
{
    auto iter = conndatamap.find(id);
    if (iter == conndatamap.end())
        return {};
    ConrefIter cr { iter };
    if (cr->second.c->eventloop_erased) // don't return erased connections
        return {};
    return cr;
}

auto AddressManager::insert(ConnectionBase::ConnectionVariant& convar, const ConnectionInserter& h) -> Result<Conref>
{
    auto c { convar.base() };
    auto ip { c->peer_addr().ip() };
    if (ip && !ip->is_loopback()) {
#ifndef DISABLE_LIBUV
        if (convar.is_tcp()) {
            auto& tcp_con { convar.get_tcp() };
            auto ipv4 { tcp_con->peer_addr_native().ip };
            if (!c->inbound()) {
                tcpConnectionSchedule.on_outbound_connected(*tcp_con);
                c->addedToSchedule = true;
            } else
                tcpConnectionSchedule.add_feeler(tcp_con->claimed_peer_addr(), ipv4);
        }
#endif
        if (ipCounter.contains(*ip))
            return Error(EDUPLICATECONNECTION);
    }

    if (auto c { eviction_candidate() })
        h.evict(*c);

    // insert into conndatamap
    auto p = conndatamap.try_emplace(c->id, c->get_shared(), h);
    assert(p.second);
    Conref cr { p.first };
    c->dataiter = cr.iterator();

    assert(!ip || ip->is_loopback() || ipCounter.insert(*ip, 1) == 1);

    if (c->inbound()) {
        inboundConnections.push_back(cr);
    } else {
        outboundEndpoints.push_back(c->peer_addr());
    }
    return cr;
}

api::IPCounter AddressManager::api_count_ips() const
{
    api::IPCounter ipv;
    for (auto& [k, v] : ipCounter.data()) {
        ipv.vector.push_back({ k, v });
    }
    return ipv;
}

void AddressManager::erase(Conref cr)
{
    if (auto ip { cr.peer().ip() })
        ipCounter.erase(*ip);
    if (cr->c->inbound())
        std::erase(inboundConnections, cr);
    else
        std::erase(outboundEndpoints, cr.peer());
    delayedDelete.push_back(cr);
}

void AddressManager::garbage_collect()
{
    for (auto cr : delayedDelete) {
        conndatamap.erase(cr.iterator());
    }
    delayedDelete.clear();
}

wrt::optional<std::chrono::steady_clock::time_point> AddressManager::pop_scheduled_connect_time()
{
    if (config().node.isolated)
        return {};
#ifndef DISABLE_LIBUV
    return tcpConnectionSchedule.updated_wakeup_time();
#else
    return wsConnectionSchedule.pop_wakeup_time();
#endif
}

template <typename T>
[[nodiscard]] static auto sample_from_vec(const std::vector<T>& v, size_t N)
{
    std::vector<std::remove_cv_t<T>> out;
    std::sample(v.begin(), v.end(), std::back_inserter(out),
        N, std::mt19937 { std::random_device {}() });
    return out;
}

auto AddressManager::eviction_candidate() const -> wrt::optional<Conref>
{
    if (conndatamap.size() < 200)
        return {};
    auto sampled { sample_from_vec(inboundConnections, 1) };
    assert(sampled.size() == 1);
    return Conref { sampled[0] };
}
