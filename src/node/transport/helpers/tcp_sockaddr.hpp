#pragma once

#include "transport/helpers/sockaddr.hpp"
#include "wrt/format.hpp"
#include "wrt/str_utils.hpp"
#include "wrt/variant.hpp"
#include <limits>
#include <stdexcept>
#ifndef DISABLE_LIBUV
struct sockaddr;
#endif

class Reader;

constexpr wrt::optional<uint16_t> parse_port(const std::string_view& s)
{
    uint16_t out;
    if (s.size() > 5)
        return {};
    uint32_t port = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9')
            return {};
        uint8_t digit = s[i] - '0';
        if (i == 5) {
            if (port > std::numeric_limits<uint16_t>::max())
                return {};
            if (digit >= 6)
                return {};
        }
        port *= 10;
        port += digit;
    }
    out = port;
    return out;
}

struct TCPPeeraddr : public Sockaddr4 {
    using Sockaddr4::Sockaddr4;
    TCPPeeraddr(Sockaddr4 b)
        : Sockaddr4(std::move(b))
    {
    }
    std::string to_string() const;
    std::string to_string_with_protocol() const
    {
        return "tcp://" + to_string();
    }
    std::string_view type_str() const
    {
        return "TCP";
    }
    static TCPPeeraddr from_sql_id(int64_t id)
    {
        return { Sockaddr4::from_sql_id(id) };
    }
    [[nodiscard]] static constexpr wrt::optional<TCPPeeraddr> parse(const std::string_view& s)
    {
        auto [ipstr, portstr] { split(s, ':') };
        auto ip { IPv4::parse(ipstr) };
        auto port { parse_port(portstr) };
        if (ip && port)
            return TCPPeeraddr(*ip, *port);
        return {};
    }
    constexpr TCPPeeraddr(std::string_view s)
        : TCPPeeraddr([&] {
            if (auto p { parse(s) })
                return *p;
            throw std::runtime_error(fmt_lib::format("Cannot parse endpoint {}", s));
        }())
    {
    }
};

struct Hostname : public std::string {
    Hostname(std::string s)
        : std::string(std::move(s))
    {
    }
};
template <typename CharT>
struct std::formatter<Hostname, CharT> : std::formatter<std::string, CharT> { };

struct HostnamePort {
    Hostname host;
    uint16_t port;
    [[nodiscard]] std::string to_string() const noexcept
    {
        return fmt_lib::format("{}:{}", host, port);
    }
    bool operator==(const HostnamePort&) const = default;
    HostnamePort(Hostname host, uint16_t port)
        : host(host)
        , port(port)
    {
    }
};

class TcpPin : public wrt::variant<TCPPeeraddr, HostnamePort> {
private:
    using wrt::variant<TCPPeeraddr, HostnamePort>::variant;

public:
    static constexpr wrt::optional<TcpPin> parse(std::string_view hostport) noexcept
    {
        auto [host, port] { split(hostport, ':') };

        auto p { parse_port(port) };
        if (!p)
            return {};

        if (auto ip { IPv4::parse(host) })
            return TcpPin { TCPPeeraddr { Sockaddr4 { *ip, *p } } };
        return TcpPin { HostnamePort(std::string(host), *p) };
    }
    [[nodiscard]] std::string to_string() const
    {
        return visit([](auto& t) { return t.to_string(); });
    }
    TcpPin(std::string_view s)
        : TcpPin([&] {
            if (auto p { parse(s) }) {
                return *p;
            }
            throw std::runtime_error(fmt_lib::format("Cannot parse endpoint {}", s));
        }())
    {
    }
};

struct WSPeeraddr : public Sockaddr {
    using Sockaddr::Sockaddr;
    auto operator<=>(const WSPeeraddr&) const = default;
    std::string to_string() const;
    std::string to_string_with_protocol() const
    {
        return "ws://" + to_string();
    }
    WSPeeraddr(Sockaddr addr)
        : Sockaddr(std::move(addr))
    {
    }
    std::string_view type_str() const
    {
        return "WS";
    }
    // static WSSockaddr from_sql_id(int64_t id)
    // {
    //     return { TCPSockaddrBase::from_sql_id(id) };
    // }
    // static constexpr wrt::optional<WSSockaddr> parse(const std::string_view& sv)
    // {
    //     auto p { TCPSockaddrBase::parse(sv) };
    //     if (p) {
    //         return WSSockaddr(*p);
    //     }
    //     return {};
    // }
};
