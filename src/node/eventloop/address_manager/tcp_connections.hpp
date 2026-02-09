#pragma once
#include "general/errors_forward.hpp"
#include "init_arg.hpp"
#include "net/forward.hpp"
#include "peerserver/connection_data.hpp"
#include "transport/helpers/ipv4.hpp"
#include "wakeup_time.hpp"
#include <cassert>
#include <nlohmann/json_fwd.hpp>
#include <set>
#include <vector>

struct TCPConnectRequest;

namespace connection_schedule {

using json = nlohmann::json;
using Source = IPv4;
using time_point = std::chrono::steady_clock::time_point;
using duration = std::chrono::steady_clock::duration;
using steady_clock = std::chrono::steady_clock;

class PinnedUrls {
private:
    struct PinEntry {
        wrt::optional<IPv4> pinnedIp;
        uint16_t port;
        PinEntry(uint16_t port)
            : port(port)
        {
        }
    };
    struct Pinned : public std::vector<PinEntry> {
        auto find(uint16_t port)
        {
            return std::ranges::find_if(*this, [&](const PinEntry& p) { return p.port == port; });
        }
        auto find(uint16_t port) const
        {
            return std::ranges::find_if(*this, [&](const PinEntry& p) { return p.port == port; });
        }
    };
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    struct Mapval {
        time_point lastDnsLookup;
        time_point nextDnsLookup;
        wrt::optional<time_point> activeDnsLookup {};
        std::vector<IPv4> ips;
        Pinned pinned;
    };
    using Map = std::map<Hostname, Mapval>;
    Map map;
    size_t size_ { 0 };
    time_point wakeupTime = time_point::max();

    void consider_wakeup(time_point);
    [[nodiscard]] std::pair<Map::iterator, bool> get_or_insert(const Hostname&);
    bool erase_if_necessary(Map::iterator);

public:
    using value_type = Map::value_type;
    [[nodiscard]] std::vector<Hostname> pop_scheduled_dns_lookups();
    [[nodiscard]] size_t size() const { return size_; }
    struct OnDnsResult {
        value_type* byHost;
        std::vector<TCPPeeraddr> pinPeers;
    };
    [[nodiscard]] std::optional<OnDnsResult> on_dns_lookup(const DnsResolveResult& r);
    struct PinPeeraddr {
        value_type* byHost;
        TCPPeeraddr pinPeer;
    };
    struct OnInsertResult {
        bool resolveDns { false };
        std::optional<PinPeeraddr> pinPeeraddr;
    };
    [[nodiscard]] OnInsertResult insert(const HostnamePort&);
    std::optional<TCPPeeraddr erase(const HostnamePort&);
    const value_type* pinned_ip(const HostnamePort&) const;
    void for_each(auto lambda) const
    {
        for (auto& [hostname, e] : map) {
            for (auto& p : e.pinned) {
                lambda(hostname, p.port);
            }
        }
    }
};

template <typename addr_t>
struct WithSource {
    addr_t address;
    wrt::optional<Source> source;
    explicit WithSource(addr_t addr)
        : address(std::move(addr)) { };
    WithSource(addr_t addr, Source source)
        : address(std::move(addr))
        , source(std::move(source)) { };
};
using TCPWithSource = WithSource<TCPPeeraddr>;

// data structure to encode success of recent connection tries
class ConnectionLog {
public:
    [[nodiscard]] size_t consecutive_failures() const;
    [[nodiscard]] bool last_connection_failed() const;
    void log_failure(); // returns number of repeated failures
    void log_success();

private:
    uint32_t active_bits() const
    {
        return bits & 0x1F;
    }
    uint32_t bits { 1 << 5 };
};

template <typename T>
class SockaddrVectorBase;

class FeelerVector;
class VectorEntry {
public:
    template <typename T>
    friend class SockaddrVectorBase;
    friend class FeelerVector;
    auto sockaddr() const { return address; };
    VectorEntry(const TCPWithSource& i)
        : address(i.address)
    {
        if (i.source.has_value()) {
            sources.insert(*i.source);
        }
    }
    void add_source(Source);

    // returns expiration time point

    void log_success();
    void log_failure();
    json to_json() const;

    Error lastError { 0 };

protected:
    ConnectionLog connectionLog;
    std::set<Source> sources;
    TCPPeeraddr address;
};

class EntryWithTimer : public VectorEntry {
public:
    struct Timer {
        auto sleep_duration() const { return _sleepDuration; }
        auto wakeup_time() const { return wakeupTime; }
        bool active() const { return wakeupTime.has_value(); }
        void deactivate() { wakeupTime.reset(); }
        bool expired_at(time_point tp) const { return wakeupTime && wakeupTime < tp; }
        json to_json() const;
        void set(duration d)
        {
            _sleepDuration = d;
            wakeupTime = steady_clock::now() + d;
        }
        Timer(duration d = duration::zero())
        { // new timers wake up immediately
            set(d);
        }

    private:
        duration _sleepDuration;
        wrt::optional<time_point> wakeupTime;
    };
    // [[nodiscard]] time_point outbound_connected_ended(const ReconnectContext&);
    // [[nodiscard]] time_point update_timer(const ReconnectContext&);

    wrt::optional<time_point> make_expired_pending(time_point, std::vector<ConnectRequest>& outpending);
    void wakeup_after(duration);
    json to_json() const;
    [[nodiscard]] wrt::optional<time_point> wakeup_time() const;
    [[nodiscard]] auto sleep_duration() const { return timer.sleep_duration(); }
    using VectorEntry::VectorEntry;

protected:
    Timer timer;
};

class VerifiedEntry : public EntryWithTimer {
public:
    using tp = std::chrono::steady_clock::time_point;

    json to_json() const;
    operator TCPPeeraddr() const { return this->sockaddr(); }
    VerifiedEntry(EntryWithTimer e, tp lastVerified)
        : EntryWithTimer(std::move(e))
        , lastVerified(lastVerified)
    {
    }
    VerifiedEntry(const WithSource<TCPPeeraddr>& i, tp lastVerified)
        : EntryWithTimer(i)
        , lastVerified(lastVerified)
    {
    }
    void on_connected()
    {
        // don't need sources because it we could connect
        sources.clear();
        log_success();
        connections += 1;
    }
    void on_disconnected()
    {
        assert(connections != 0);
        connections -= 1;
    }
    tp lastVerified;
    ssize_t connections { 0 };
};
using ConnectedEntry = VerifiedEntry;

struct Timeout : public wrt::optional<time_point> {
    void set_min_of(const wrt::optional<time_point>&);
};

struct Found {
    // void set_
    VectorEntry& match;
    connection_schedule::Timeout& timeout;
    bool verified;
};

struct FoundDisconnected {
    void wakeup_after(duration);
    EntryWithTimer& match;
    connection_schedule::Timeout& timeout;
    bool verified;
};

template <typename T>
class SockaddrVectorBase {
    friend class FeelerVector;
    friend class ConnectionSchedule;

public:
    using elem_t = T;

    [[nodiscard]] elem_t* find(const TCPPeeraddr&) const;
    size_t erase(const TCPPeeraddr& a, auto lambda);
    size_t erase(const TCPPeeraddr& a)
    {
        return erase(a, [](auto) { });
    }
    void take_expired(time_point now, std::vector<ConnectRequest>&);
    elem_t& push_back(elem_t);
    json to_json() const;
    size_t size() const { return data.size(); }
    const auto& elements() const { return data; }

    Timeout timeout;

protected:
    mutable std::vector<elem_t> data;
};

class FeelerVector : public SockaddrVectorBase<EntryWithTimer> {

public:
    std::pair<elem_t&, bool> insert(const WithSource<TCPPeeraddr>&);
    std::pair<elem_t&, bool> insert(const EntryWithTimer&);
    using SockaddrVectorBase::SockaddrVectorBase;
};

class ConnectedVector : public SockaddrVectorBase<VectorEntry> {

public:
    std::pair<elem_t&, bool> insert(const WithSource<TCPPeeraddr>&);
    using SockaddrVectorBase::SockaddrVectorBase;
};

class VerifiedVector : public SockaddrVectorBase<VerifiedEntry> {
public:
    using tp = typename VerifiedEntry::tp;
    std::pair<VectorEntry&, bool> insert(const TCPWithSource&, tp lastVerified);
    void prune(auto&& pred, size_t N);
    using SockaddrVectorBase::SockaddrVectorBase;
};

class TCPConnectionSchedule {
    using json = nlohmann::json;
    using InitArg = address_manager::InitArg;
    using ConnectionData = peerserver::ConnectionData;

public:
    TCPConnectionSchedule(InitArg);

    void pin(const TcpPin&);
    void unpin(const TcpPin&);
    [[nodiscard]] std::vector<Hostname> initialize();

    wrt::optional<ConnectRequest> add_feeler(TCPPeeraddr, Source);
    void connect_expired();

    // connection callbacks
    void on_outbound_connected(const TCPConnection&);
    void on_outbound_disconnected(const TCPConnectRequest&, Error reason, bool registered);
    void on_outbound_failed(const TCPConnectRequest&, Error reason);
    void on_inbound_disconnected(const IPv4& ip);
    void on_dns_resolve(const DnsResolveResult&);

    json to_json() const;
    size_t size() const;

    [[nodiscard]] wrt::optional<time_point> updated_wakeup_time();

    auto pop_scheduled_dns_lookups()
    {
        return std::move(scheduledDnsLookups);
    }
    std::vector<TCPPeeraddr> sample_verified(size_t N) const;

private:
    void insert_freshly_pinned(const TCPPeeraddr&);
    void prune_verified();
    [[nodiscard]] std::vector<TCPConnectRequest> pop_expired(time_point now = steady_clock::now());
    void refresh_wakeup_time();
    [[nodiscard]] auto find(const TCPPeeraddr& a) -> wrt::optional<Found>;
    [[nodiscard]] auto find_disconnected(const TCPPeeraddr& a) -> wrt::optional<FoundDisconnected>;

    VerifiedVector connectedVerified;
    VerifiedVector disconnectedVerified;
    FeelerVector feelers; // Candidates to test connection to
    PeerServer& peerServer;
    struct ComparatorPinned {
        using is_transparent = void;
        bool operator()(const TCPPeeraddr& a, IPv4 ip) const
        {
            return a.ip < ip;
        }
        bool operator()(IPv4 ip, const TCPPeeraddr& a) const
        {
            return ip < a.ip;
        }
        bool operator()(const TCPPeeraddr& a1, const TCPPeeraddr& a2) const
        {
            return a1 < a2;
        }
    };
    struct PinOrigin {
        PinnedUrls::value_type* byHost { nullptr };
        bool explicitly { false };
        bool none() const { return !byHost && !explicitly; }
    };
    size_t softboundVerified { 1000 };
    PinnedUrls urlPinned;
    using TcpPinned = std::map<TCPPeeraddr, PinOrigin, ComparatorPinned>;
    TcpPinned tcpPinned;
    size_t explicitIpPinned{0};
    connection_schedule::WakeupTime wakeup_tp;
    std::vector<Hostname> scheduledDnsLookups;

private: // private methods
    void pin_ipport(const TCPPeeraddr&, PinnedUrls::value_type* byHost = nullptr);
    bool unpin_ipport(const TCPPeeraddr&, bool explicitly);
    bool unpin_ipport(TcpPinned::iterator, bool explicitly);

    void pin_internal(const TcpPin&);
    void pin_internal(const HostnamePort&);
    void pin_internal(const TCPPeeraddr&);
    void unpin_internal(const HostnamePort&);
    void unpin_internal(const TCPPeeraddr&);
};
}

using TCPConnectionSchedule
    = connection_schedule::TCPConnectionSchedule;
