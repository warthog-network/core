#include "tcp_util.hpp"
#include "general/byte_order.hpp"
#include "general/reader.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace {
std::optional<uint16_t> parse_port(const std::string_view& s)
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
            if (port > 6553)
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
}

IPv4::IPv4(Reader& r)
    : data(r.uint32())
{
}

IPv4::IPv4(const sockaddr_in& sin)
    : IPv4(ntoh32(uint32_t(sin.sin_addr.s_addr)))
{
}

bool IPv4::is_valid(bool allowLocalhost) const
{
    uint32_t ndata = hton32(data);
    uint8_t* pdata = reinterpret_cast<uint8_t*>(&ndata);
    return data != 0 // ignore 0.0.0.0
        && (allowLocalhost || pdata[0] != 127)
        && pdata[0] != 192 // ignore 192.xxx.xxx.xxx
        && !(pdata[0] == 169 && pdata[1] == 254); // ignore 192.xxx.xxx.xxx
}

EndpointAddress::EndpointAddress(std::string_view s)
    : EndpointAddress(
        [&] {
            auto ea { EndpointAddress::parse(s) };
            if (ea)
                return *ea;
            throw std::runtime_error("Cannot parse endpoint address \"" + std::string(s) + "\".");
        }()) {
    };

// modified version of libuv's
// static int inet_pton4(const char *src, unsigned char *dst) {
std::optional<IPv4> IPv4::parse(const std::string_view& s)
{
    uint32_t out;
    int saw_digit, index;
    std::array<uint8_t, 4> tmp;

    saw_digit = 0;
    index = 0;

    uint32_t nw = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            nw = nw * 10 + (c - '0');
            if ((saw_digit && nw == 0) || nw > 255)
                return {};
            tmp[index] = nw;
            saw_digit = 1;
        } else if (c == '.' && saw_digit) {
            if (++index >= 4)
                return {};
            saw_digit = 0;
            nw = 0;
        } else {
            return {};
        }
    }
    if (index != 3)
        return {};
    uint32_t be32_ipv4;
    memcpy(&be32_ipv4, tmp.data(), 4);
    out = ntoh32(be32_ipv4);
    return IPv4 { out };
}

std::string IPv4::to_string() const
{
    static const char fmt[] = "%u.%u.%u.%u";
    uint32_t ndata = hton32(data);
    uint8_t* src = reinterpret_cast<uint8_t*>(&ndata);
    char tmp[16];
    assert(snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]) > 0);
    return { tmp };
}

EndpointAddress::EndpointAddress(Reader& r)
    : ipv4(r)
    , port(r.uint16())
{
}

namespace {
std::optional<IPv4> resolve_hostname(const std::string& hostname)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4 only
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
    if (status != 0) {
        return {};
    }

    std::optional<IPv4> result;
    if (res != nullptr && res->ai_family == AF_INET) {
        struct sockaddr_in* ipv4_addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
        result = IPv4(*ipv4_addr);
    }

    freeaddrinfo(res);
    return result;
}
}

std::optional<EndpointAddress> EndpointAddress::parse(const std::string_view& s)
{
    size_t d1 = s.find(":");
    auto hoststr { s.substr(0, d1) };
    
    // First try to parse as IP address
    auto ip = IPv4::parse(hoststr);
    
    // If not a valid IP, try DNS resolution
    if (!ip) {
        ip = resolve_hostname(std::string(hoststr));
    }
    
    if (!ip)
        return {};
    if (d1 != std::string::npos) {
        size_t d2 = s.find("//", d1 + 1);
        auto portstr { s.substr(d1 + 1, d2 - d1 - 1) };
        auto port = parse_port(portstr);
        if (!port)
            return {};

        return EndpointAddress { ip.value(), port.value() };
    } else {
        return EndpointAddress { ip.value() };
    }
}

std::string EndpointAddress::to_string() const
{
    return ipv4.to_string() + ":" + std::to_string(port);
}

sockaddr_in EndpointAddress::sock_addr() const
{
    sockaddr_in out;
    memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = hton16(port);
#ifdef SIN6_LEN
    out.sin_len = sizeof(out);
#endif
    uint32_t ntmp = hton32(ipv4.data);

    static_assert(sizeof(struct in_addr) == 4);
    memcpy(&out.sin_addr.s_addr, &ntmp, 4);
    return out;
}
