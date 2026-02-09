#include "tcp_connections.hpp"
#include "general/errors.hpp"
#include "global/globals.hpp"
#include "peerserver/peerserver.hpp"
#include "spdlog/spdlog.h"
#include "transport/connect_request.hpp"
#include "transport/tcp/connection.hpp"
#include <algorithm>
#include <cassert>
#include <functional>
#include <future>
#include <nlohmann/json.hpp>
#include <random>

using namespace std::chrono;
using namespace std::chrono_literals;

using sc = std::chrono::steady_clock;
namespace {
auto seconds_from_now(sc::time_point tp)
{
    return duration_cast<seconds>(tp - sc::now()).count();
}

template <class _PopulationIterator,
    class _PopulationSentinel,
    class _SampleIterator,
    class _Distance,
    class _UniformRandomNumberGenerator>
_SampleIterator sample2ranges(
    _PopulationIterator __first,
    _PopulationSentinel __last,
    _PopulationIterator __first2,
    _PopulationSentinel __last2,
    _SampleIterator __output_iter,
    _Distance __n,
    _UniformRandomNumberGenerator&& __g)
{
    _Distance __unsampled_sz = std::distance(__first, __last)
        + std::distance(__first2, __last2);
    for (__n = std::min(__n, __unsampled_sz); __n != 0; ++__first) {
        if (__first == __last) {
            __first = __first2;
            __last = __last2;
        }
        _Distance __r = std::uniform_int_distribution<_Distance>(0, --__unsampled_sz)(__g);
        if (__r < __n) {
            *__output_iter++ = *__first;
            --__n;
        }
    }
    return __output_iter;
}
std::string dns_health_key(const std::string& hostname)
{
    return "DNSLookup_" + hostname;
}
}

namespace connection_schedule {

auto PinnedUrls::get_or_insert(const Hostname& hostname) -> std::pair<Map::iterator, bool>
{
    return map.try_emplace(hostname);
}

bool PinnedUrls::erase_if_necessary(Map::iterator it)
{
    if (it->second.activeDnsLookup || it->second.pinned.size() > 0)
        return false;
    map.erase(it);
    return true;
}

auto PinnedUrls::on_dns_lookup(const DnsResolveResult& r) -> std::optional<OnDnsResult>
{
    auto it { map.find(r.host) };
    if (it == map.end())
        return {};
    OnDnsResult ret;
    ret.byHost = &*it;
    auto& val { it->second };
    val.activeDnsLookup.reset();
    val.lastDnsLookup = clock::now();

    const auto healthKey { dns_health_key(r.host) };
    if (r.result.has_value()) {
        health().remove(healthKey);
        auto& ips { r.result.value() };
        assert(!ips.empty());
        bool firstSuccessfulLookup { val.ips.empty() };
        if (firstSuccessfulLookup) {
            // now all corresponding hostname-pinned must be ip pinned.
            val.ips = std::move(ips);
            auto ip { val.ips.front() };
            for (auto& p : val.pinned) {
                ret.pinPeers.push_back({ ip, p.port });
                p.pinnedIp = ip;
            }
        }
    } else {
        auto err { r.result.error() };
        auto msg { "Cannot resolve pinned peer hostname: {}" + err.format() };
        health().set(healthKey, msg);
    }
    return ret;
}

auto PinnedUrls::insert(const HostnamePort& hp) -> OnInsertResult
{
    OnInsertResult out;
    auto p { get_or_insert(hp.host) };

    auto& val { p.first->second };
    auto& pinned { val.pinned };
    if (p.second) { // was actually inserted
        out.resolveDns = true;
        consider_wakeup(p.first->second.nextDnsLookup);
    }
    auto it { pinned.find(hp.port) };
    if (it == pinned.end()) {
        pinned.push_back(hp.port);
        size_ += 1;
        if (!val.ips.empty()) {
            auto ip { val.ips.front() };
            it->pinnedIp = ip;
            out.pinPeeraddr = PinPeeraddr { &*p.first, TCPPeeraddr(ip, hp.port) };
        }
    }
    return out;
}

bool PinnedUrls::erase(const HostnamePort& hp)
{
    bool found { false };
    auto it { map.find(hp.host) };
    if (it != map.end()) {
        auto& pinned { it->second.pinned };
        std::optional<TCPPeeraddr> unpinAddress;
        std::erase_if(pinned, [&](const PinEntry& e) {
            if (e.port == hp.port) {
                assert(!found);
                found = true;
                if (e.pinnedIp)
                    unpinAddress = TCPPeeraddr { *e.pinnedIp, e.port };
                size_ -= 1;
                return true;
            }
            return false;
        });
        erase_if_necessary(it);
    }
    return found;
}

auto PinnedUrls::pinned_ip(const HostnamePort& hp) const -> const value_type*
{
    auto it { map.find(hp.host) };
    if (it == map.end())
        return nullptr;
    auto it2 { it->second.pinned.find(hp.port) };
    if (it2 == it->second.pinned.end())
        return nullptr;
    return &*it;
}

void PinnedUrls::consider_wakeup(time_point tp)
{
    if (wakeupTime > tp)
        wakeupTime = tp;
}

std::vector<Hostname> PinnedUrls::pop_scheduled_dns_lookups()
{
    const auto now { std::chrono::steady_clock::now() };
    wakeupTime = wakeupTime.max();
    std::vector<Hostname> out;
    for (auto& [hostname, v] : map) {
        if (v.nextDnsLookup < now) {
            out.push_back(hostname);
            v.nextDnsLookup = v.nextDnsLookup.max();
            v.activeDnsLookup = now;
        } else {
            consider_wakeup(v.nextDnsLookup);
        }
    }
    return out;
}

size_t ConnectionLog::consecutive_failures() const
{
    size_t z(std::countr_zero(bits >> 5));
    size_t a { active_bits() };
    return std::min(z, a);
}

bool ConnectionLog::last_connection_failed() const
{
    return (bits & (1 << 5)) == 0;
}

void ConnectionLog::log_failure()
{
    uint32_t active { active_bits() + 1 };
    if ((active >> 5) > 0) {
        active = 0x0000001Fu;
    }
    const auto logbits { bits >> 5 };
    bits = (logbits << 6) | active;
}

void ConnectionLog::log_success()
{
    uint32_t active { active_bits() + 1 };
    if ((active >> 5) > 0) {
        active = 0x0000001Fu;
    }
    bits = (((bits >> 4) | 0x00000001) << 5) | active;
}

void VectorEntry::add_source(Source s)
{
    sources.insert(s);
}

void VectorEntry::log_success()
{
    connectionLog.log_success();
}

void VectorEntry::log_failure()
{
    connectionLog.log_failure();
}

json VectorEntry::to_json() const
{
    return {
        { "address", address.to_string() },
        { "lastError", lastError.is_error() ? json(lastError.format()) : json(nullptr) }
    };
}

json EntryWithTimer::Timer::to_json() const
{
    using namespace std::chrono;
    return {
        { "sleepDuration", duration_cast<seconds>(_sleepDuration).count() },
        { "expiresIn", wakeupTime ? json(seconds_from_now(*wakeupTime)) : json(nullptr) }
    };
}

void EntryWithTimer::wakeup_after(duration d)
{
    timer = Timer(d);
}

json EntryWithTimer::to_json() const
{
    auto j(VectorEntry::to_json());
    j["timer"] = timer.to_json();
    return j;
}

wrt::optional<time_point> EntryWithTimer::wakeup_time() const
{
    return timer.wakeup_time();
}

wrt::optional<time_point> EntryWithTimer::make_expired_pending(time_point now, std::vector<ConnectRequest>& outpending)
{
    if (!timer.active())
        return {};
    if (!timer.expired_at(now))
        return timer.wakeup_time();
    outpending.push_back(TCPConnectRequest::make_outbound(address, timer.sleep_duration()));
    timer.deactivate();
    return {};
}

std::pair<VectorEntry&, bool> VerifiedVector::insert(const TCPWithSource& i, tp lastVerified)
{
    auto p { this->find(i.address) };
    if (p)
        return { *p, false };
    auto& e { this->push_back(VerifiedEntry { i, lastVerified }) };
    timeout.set_min_of(e.wakeup_time());
    return { e, true };
}

void VerifiedVector::prune(auto&& pred, size_t n)
{
    if (data.size() <= n)
        return;
    size_t d { data.size() - n };
    std::erase_if(data, [&](VerifiedEntry& e) {
        return (d-- != 0)
            && e.wakeup_time().has_value() // no pending connection
            && pred(e);
    });
}

json VerifiedEntry::to_json() const
{
    auto json(EntryWithTimer::to_json());
    json["lastVerified"] = seconds_from_now(lastVerified);
    return json;
}

void Timeout::set_min_of(const wrt::optional<time_point>& tp)
{
    if (tp && (!has_value() || **this > *tp))
        *static_cast<wrt::optional<time_point>*>(this) = tp;
}

void FoundDisconnected::wakeup_after(duration d)
{
    match.wakeup_after(d);
    timeout.set_min_of(match.wakeup_time());
}

template <typename T>
size_t SockaddrVectorBase<T>::erase(const TCPPeeraddr& a, auto lambda)
{
    auto iter = std::partition(data.begin(), data.end(), [&](elem_t& e) {
        return e.address != a;
    });
    std::for_each(iter, data.end(), [&](auto& e) { lambda(std::move(e)); });
    auto n { data.end() - iter };
    data.erase(iter, data.end());
    return n;
}

template <typename T>
json SockaddrVectorBase<T>::to_json() const
{
    json j(json::array());
    for (auto& d : data) {
        j.push_back(d.to_json());
    }
    return j;
}

template <typename T>
auto SockaddrVectorBase<T>::push_back(elem_t e) -> elem_t&
{
    data.push_back(std::move(e));
    return data.back();
}

template <typename T>
void SockaddrVectorBase<T>::take_expired(time_point now, std::vector<ConnectRequest>& outpending)
{
    if (!timeout || *timeout > now)
        return;
    timeout.reset();
    for (auto& e : data)
        timeout.set_min_of(e.make_expired_pending(now, outpending));
}

template <typename T>
auto SockaddrVectorBase<T>::find(const TCPPeeraddr& address) const -> elem_t*
{
    auto iter { std::find_if(data.begin(), data.end(), [&](auto& elem) { return elem.address == address; }) };
    if (iter == data.end())
        return nullptr;
    return &*iter;
}

auto FeelerVector::insert(const EntryWithTimer& e1) -> std::pair<elem_t&, bool>
{
    auto p { find(e1.address) };
    if (p)
        return { *p, false };
    elem_t& e { push_back(e1) };
    timeout.set_min_of(e.wakeup_time());
    return { e, true };
}
auto FeelerVector::insert(const WithSource<TCPPeeraddr>& i) -> std::pair<elem_t&, bool>
{
    auto p { find(i.address) };
    if (p)
        return { *p, false };
    elem_t& e { push_back(elem_t { i }) };
    timeout.set_min_of(e.wakeup_time());
    return { e, true };
}

std::vector<TCPPeeraddr> TCPConnectionSchedule::sample_verified(size_t N) const
{
    std::vector<TCPPeeraddr> out;
    out.reserve(N);
    sample2ranges(
        connectedVerified.elements().begin(),
        connectedVerified.elements().end(),
        disconnectedVerified.elements().begin(),
        disconnectedVerified.elements().end(),
        std::back_inserter(out),
        N, std::mt19937 { std::random_device {}() });
    return out;
}

TCPConnectionSchedule::TCPConnectionSchedule(InitArg ia)
    : peerServer(ia.peerServer)
{
    for (auto& p : ia.pin)
        pin_internal(p);
    spdlog::info("Pinned {} peer{}.", size(), (size() == 1 ? "" : "s"));
}

size_t TCPConnectionSchedule::size() const
{
    return explicitIpPinned + urlPinned.size();
}

[[nodiscard]] auto TCPConnectionSchedule::find_disconnected(const TCPPeeraddr& a) -> wrt::optional<FoundDisconnected>
{
    EntryWithTimer* p = disconnectedVerified.find(a);
    if (p)
        return FoundDisconnected { *p, disconnectedVerified.timeout, true };
    if (p = feelers.find(a); p)
        return FoundDisconnected { *p, feelers.timeout, false };
    return {};
}
auto TCPConnectionSchedule::find(const TCPPeeraddr& a) -> wrt::optional<Found>
{
    VectorEntry* p { connectedVerified.find(a) };
    if (p)
        return Found { *p, connectedVerified.timeout, true };
    p = disconnectedVerified.find(a);
    if (p)
        return Found { *p, disconnectedVerified.timeout, true };
    if (p = feelers.find(a); p)
        return Found { *p, feelers.timeout, false };
    return {};
}

wrt::optional<ConnectRequest> TCPConnectionSchedule::add_feeler(TCPPeeraddr addr, Source src)
{
    if (auto o { find(addr) }) {
        // only track sources of addresses that are not verified
        if (o->verified)
            o->match.add_source(src);
        return {};
    } else {
        feelers.insert({ addr, src });
        wakeup_tp.consider(feelers.timeout);
        return ConnectRequest::make_outbound(addr, 0s);
    }
}

void TCPConnectionSchedule::on_outbound_connected(const TCPConnection& c)
{
    if (c.inbound())
        return;
    const TCPPeeraddr& a { c.peer_addr_native() };
    VerifiedVector::elem_t* p = nullptr;
    feelers.erase(a, [&](auto&& deleted) {
        p = &connectedVerified.push_back({ std::move(deleted), sc::now() });
    });
    if (!p)
        disconnectedVerified.erase(a,
            [&](auto&& deleted) {
                p = &connectedVerified.push_back({ std::move(deleted), sc::now() });
            });
    if (!p)
        p = connectedVerified.find(a);
    if (!p)
        ;
    p->on_connected();
    prune_verified();
}

void TCPConnectionSchedule::pin_internal(const HostnamePort& hostport)
{
    auto res { urlPinned.insert(hostport) };
    if (res.resolveDns) {
        std::ranges::copy(urlPinned.pop_scheduled_dns_lookups(), std::back_inserter(scheduledDnsLookups));
    }
    if (auto& p { res.pinPeeraddr }) { // was not pinned before
        pin_ipport(p->pinPeer, p->byHost);
    }
}
bool TCPConnectionSchedule::unpin_ipport(const TCPPeeraddr& a, bool explicitly)
{
    return unpin_ipport(tcpPinned.find(a), explicitly);
}

bool TCPConnectionSchedule::unpin_ipport(TcpPinned::iterator it, bool explicitly)
{
    if (it == tcpPinned.end())
        return false;
    auto& origin { it->second };
    if (explicitly) {
        if (origin.explicitly) {
            assert(explicitIpPinned > 0);
            explicitIpPinned -= 1;
            origin.explicitly = false;
        }
    } else
        origin.byHost = nullptr;
    if (origin.none()) {
        tcpPinned.erase(it);
        prune_verified();
    }
    return true;
}

void TCPConnectionSchedule::pin_ipport(const TCPPeeraddr& a, PinnedUrls::value_type* byHost)
{
    auto p { tcpPinned.try_emplace(a) };
    if (p.second) { // was not pinned before
        insert_freshly_pinned(a);
    }
    auto& pinOrigin { p.first->second };
    if (byHost)
        pinOrigin.byHost = byHost;
    else {
        if (pinOrigin.explicitly == false) {
            explicitIpPinned += 1;
            pinOrigin.explicitly = true;
        }
    }
}
void TCPConnectionSchedule::pin_internal(const TCPPeeraddr& a)
{
    // nullptr means pin explicitly (i.e. pin is on ip, not by hostname lookup)
    pin_ipport(a, nullptr);
}

void TCPConnectionSchedule::pin(const TcpPin& a)
{
    pin_internal(a);
}
void TCPConnectionSchedule::pin_internal(const TcpPin& a)
{
    return a.visit([&](auto& p) { return pin_internal(p); });
    if (scheduledDnsLookups.size() > 0) {
    }
}

void TCPConnectionSchedule::unpin_internal(const HostnamePort& hp)
{
    urlPinned.erase(hp);
// bool PinnedUrls::erase(const HostnamePort& hp)
}
void TCPConnectionSchedule::unpin_internal(const TCPPeeraddr& addr)
{
    unpin_ipport(addr, true);
}

void TCPConnectionSchedule::unpin(const TcpPin& a)
{
    return a.visit([&](auto& p) { return unpin_internal(p); });
}

std::vector<Hostname> TCPConnectionSchedule::initialize()
{
    constexpr size_t maxRecent = 100;

    // get recently seen peers from db
    std::promise<std::vector<std::pair<TCPPeeraddr, Timestamp>>> p;
    auto future { p.get_future() };
    auto cb = [&p](std::vector<std::pair<TCPPeeraddr, Timestamp>>&& v) {
        p.set_value(std::move(v));
    };
    peerServer.async_get_recent_peers(std::move(cb), maxRecent);
    auto db_peers { future.get() };

    // load verified addresses
    const int64_t nowts { now_timestamp() };
    constexpr connection_schedule::Source startup_source { 0 };
    for (const auto& [a, timestamp] : db_peers) {
        auto lastVerified = sc::now() - seconds((nowts - int64_t(timestamp.value())));
        auto [_, wasInserted] = disconnectedVerified.insert({ a, startup_source }, lastVerified);
        assert(wasInserted);
    }

    // load pinned addresses
    for (auto& p : tcpPinned)
        insert_freshly_pinned(p.first);

    refresh_wakeup_time();
    return std::move(scheduledDnsLookups);
};

void TCPConnectionSchedule::on_outbound_disconnected(const TCPConnectRequest& r, Error err, bool established)
{
    auto a { r.address() };
    if (established) { // an established outgoing connection was closed
        auto e { connectedVerified.find(a) };
        e->lastError = err;
        assert(e != nullptr);
        e->on_disconnected();
        if (e->connections == 0) {
            // all outgoing connections were closed,
            // now move entry from connectedVerified to to disconnectedVerified.
            using entry_t = connection_schedule::ConnectedEntry;
            std::vector<entry_t> tmp;
            auto deleted { connectedVerified.erase(a,
                [&](entry_t&& e) { tmp.push_back(std::move(e)); }) };
            assert(deleted == 1 && tmp.size() == 1);
            auto& el { disconnectedVerified.push_back(tmp.front()) };

            connection_schedule::duration dur { [&]() -> connection_schedule::duration {
                if (err.code == EDUPLICATECONNECTION || err.code == EEVICTED) {
                    // we wanted to close the connection
                    return 10min;
                } else if (tcpPinned.contains(a) || !err.triggers_ban()) {
                    return 0s; // set timer to reconnect immediately
                } else {
                    return seconds(err.bantime());
                }
            }() };

            el.wakeup_after(dur);
            disconnectedVerified.timeout.set_min_of(el.wakeup_time());
            wakeup_tp.consider(disconnectedVerified.timeout);
        }
    } else { // non-established outgoing connection was closed
        on_outbound_failed(r, err);
    }
}
[[nodiscard]] std::optional<TCPPeeraddr> next_address(PinnedUrls::value_type& t, TCPPeeraddr a)
{
    auto& ips { t.second.ips };
    if (ips.empty())
        return {};
    auto it { std::ranges::find(ips, a.ip) };
    auto nextIp {
        [&] {
            if (it == ips.end() || ++it == ips.end())
                return ips.front();
            return *it;
        }()
    };
    auto& pinned { t.second.pinned };
    auto it2 { pinned.find(a.port) };
    assert(it2 != pinned.end());
    assert(it2->pinnedIp == a.ip);
    if (it2->pinnedIp == nextIp)
        return {};
    it2->pinnedIp = nextIp;
    return TCPPeeraddr { nextIp, a.port };
}

void TCPConnectionSchedule::on_outbound_failed(const TCPConnectRequest& cr, Error err)
{
    auto a { cr.address() };

    auto increase_sleeptime { [this](EntryWithTimer& item, Timeout& timeout) {
        auto d { item.sleep_duration() };
        if (d < 200ms) {
            d = 200ms;
        } else if (d < 1min)
            d *= 2; // exponential backoff
        item.wakeup_after(d);
        timeout.set_min_of(item.wakeup_time());
        wakeup_tp.consider(timeout);
    } };
    bool isPinned { false };
    if (auto it { tcpPinned.find(a) }; it != tcpPinned.end()) {
        isPinned = true;
        // We might try a different IP if it is pinned by hostname.
        if (auto p { it->second.byHost }) {
            // Is pinned by hostname. We might try a different IP of that host
            if (auto na { next_address(*p, a) }) {
                // We do have a different IP of that host in `na`
                auto deleted = !unpin_ipport(it, false); // Unpin address with old IP
                isPinned = !deleted;
                pin_ipport(*na, p); // Pin address with new ip
            }
        }
    }

    if (auto f { feelers.find(a) }) {
        // Failed connection is feeler connection.
        if (isPinned) {
            // Pinned feelers should not be deleted.
            f->lastError = err;
            increase_sleeptime(*f, feelers.timeout);
        } else { // delete from feelers
            assert(feelers.erase(a) == 1);
            return;
        }
    } else {
        // Failed connection must be verified but disconnected.
        if (disconnectedVerified.size() > softboundVerified) {
            // downgrade connection from verified to feeler
            // i.e. move entry from disconnectedVerified to feelers
            auto n { disconnectedVerified.erase(a, [&](VerifiedEntry&& e) {
                auto [elem, inserted] { feelers.insert(std::move(e)) };
                elem.lastError = err;
                assert(inserted);
                increase_sleeptime(elem, feelers.timeout);
            }) };
            assert(n <= 1); // at most one element should exist for each address
        } else { // just exponential backoff reconnect
            if (auto f { disconnectedVerified.find(a) })
                increase_sleeptime(*f, disconnectedVerified.timeout);
        }
    }
}

void TCPConnectionSchedule::on_inbound_disconnected(const IPv4& ip)
{
    auto [begin, end] { tcpPinned.equal_range(ip) };
    for (auto iter = begin; iter != end; ++iter) {
        auto& addr { iter->first };
        auto found { find(addr) };
        assert(found.has_value()); // pinned should always be kept in the list
        // found->timeout.wakeup_tp
    }
}

void TCPConnectionSchedule::on_dns_resolve(const DnsResolveResult& r)
{
    if (auto res { urlPinned.on_dns_lookup(r) }) {
        for (auto& a : res->pinPeers) {
            pin_ipport(a, res->byHost);
        }
    }
}

auto TCPConnectionSchedule::to_json() const -> json
{
    return {
        { "connectedVerified", connectedVerified.to_json() },
        { "disconnectedVerified", disconnectedVerified.to_json() },
        { "feelers", feelers.to_json() },
    };
}

auto TCPConnectionSchedule::updated_wakeup_time() -> wrt::optional<time_point>
{
    return wakeup_tp.pop_new_min();
}

void TCPConnectionSchedule::connect_expired()
{
    for (auto& r : pop_expired())
        r.connect();
}

void TCPConnectionSchedule::insert_freshly_pinned(const TCPPeeraddr& a)
{
    if (connectedVerified.find(a))
        return;
    if (auto f { find_disconnected(a) }) {
        f->wakeup_after(0s);
        wakeup_tp.consider(f->timeout);
    } else {
        constexpr connection_schedule::Source startup_source { 0 };
        feelers.insert({ a, startup_source });
        wakeup_tp.consider(feelers.timeout);
    }
}

void TCPConnectionSchedule::prune_verified()
{
    disconnectedVerified.prune([&](const connection_schedule::VerifiedEntry& e) {
        assert(feelers.insert(e).second);
        return true;
    },
        softboundVerified);
    wakeup_tp.consider(feelers.timeout);
}

std::vector<TCPConnectRequest> TCPConnectionSchedule::pop_expired(time_point now)
{
    if (!wakeup_tp.expired())
        return {};

    // pop expired requests
    std::vector<ConnectRequest> outPending;
    disconnectedVerified.take_expired(now, outPending);
    feelers.take_expired(now, outPending);

    refresh_wakeup_time();
    return outPending;
}

void TCPConnectionSchedule::refresh_wakeup_time()
{
    wakeup_tp.reset();
    wakeup_tp.consider(disconnectedVerified.timeout);
    wakeup_tp.consider(feelers.timeout);
}

}
