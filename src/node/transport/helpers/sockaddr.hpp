#pragma once
#include "general/serializer_fwd.hxx"
#include "transport/helpers/ip.hpp"
extern "C" {
struct sockaddr_storage;
struct sockaddr;
}
struct Sockaddr4 {
    Sockaddr4(Reader& r);
    void serialize(RawSerializer auto& s) const
    {
        s << ip << port;
    }
    static constexpr size_t byte_size() { return IPv4::byte_size() + sizeof(port); }
    constexpr Sockaddr4(IPv4 ipv4, uint16_t port)
        : ip(ipv4)
        , port(port)
    {
    }
    static Sockaddr4 from_sql_id(int64_t id)
    {
        return Sockaddr4(
            IPv4(uint64_t(id & 0x0000FFFFFFFF0000) >> 16),
            uint16_t(0x000000000000FFFF & id));
    };
    int64_t to_sql_id()
    {
        return (int64_t(ip.data) << 16) + (int64_t(port));
    };
    IP host() const { return ip; }
    auto operator<=>(const Sockaddr4&) const = default;
#ifndef DISABLE_LIBUV
    operator sockaddr() const;
    sockaddr sock_addr() const;
#endif

    IPv4 ip;
    uint16_t port = 0;
};
struct Sockaddr6 {
    Sockaddr6();
    IPv6 ip;
    uint16_t port;
    Sockaddr6(IPv6 ip, uint16_t port)
        : ip(std::move(ip))
        , port(port)
    {
    }
};

struct Sockaddr {
    IP ip;
    uint16_t port;
    auto operator<=>(const Sockaddr&) const = default;

    IP host() const { return ip; }
    Sockaddr(Sockaddr4 s)
        : ip(std::move(s.ip))
        , port(s.port)
    {
    }
    Sockaddr(Sockaddr6 s)
        : ip(std::move(s.ip))
        , port(s.port)
    {
    }
    Sockaddr(IP ip, uint16_t port)
        : ip(std::move(ip))
        , port(port)
    {
    }
    static wrt::optional<Sockaddr> from_sockaddr_storage(const sockaddr_storage&);
};
