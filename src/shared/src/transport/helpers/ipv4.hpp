#pragma once

#include "general/byte_order.hpp"
#include "ip_type.hpp"
#include "wrt/optional.hpp"
#include <compare>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
class Reader;
class Writer;
#ifndef DISABLE_LIBUV
struct sockaddr_in;
#endif

struct IPv4 {
    IPv4(Reader& r);
#ifndef DISABLE_LIBUV
    IPv4(const sockaddr_in& sin);
#endif
    constexpr IPv4(uint32_t data = 0)
        : data(data)
    {
    }
    constexpr static auto type() { return IpType::v4; }
    friend Writer& operator<<(Writer&, const IPv4&);
    static constexpr size_t byte_size() { return sizeof(data); }
    bool is_valid() const;
    bool is_loopback() const;
    bool is_rfc1918() const; // IPv4 private networks (10.0.0.0/8, 192.168.0.0/16, 172.16.0.0/12)
    bool is_rfc2544() const; // IPv4 inter-network communications (198.18.0.0/15)
    bool is_rfc6598() const; // IPv4 ISP-level NAT (100.64.0.0/10)
    bool is_rfc5737() const; // IPv4 documentation addresses (192.0.2.0/24, 198.51.100.0/24, 203.0.113.0/24)
    bool is_rfc3927() const; // IPv4 autoconfig (169.254.0.0/16)
    bool is_routable() const;
    [[nodiscard]] static constexpr size_t bytes_size() { return 4; }
    auto operator<=>(const IPv4& rhs) const = default;
    [[nodiscard]] static constexpr wrt::optional<IPv4> parse(const std::string_view&);
    uint8_t at0() const { return data >> 24; }
    uint8_t at1() const { return 0xFF & (data >> 16); }
    uint8_t at2() const { return 0xFF & (data >> 8); }
    uint8_t at3() const { return 0xFF & data; }
    constexpr IPv4(const std::string_view& s)
        : IPv4(
              [&] {
                  auto ea { parse(s) };
                  if (ea)
                      return *ea;
                  throw std::runtime_error("Cannot parse ip address \"" + std::string(s) + "\".");
              }()) {};
    uint32_t data;
    std::string to_string() const;
};

constexpr wrt::optional<IPv4> IPv4::parse(const std::string_view& s)
{
    uint32_t tmp { 0 };
    int saw_digit = 0;
    int index = 0;

    uint32_t nw = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            nw = nw * 10 + (c - '0');
            if ((saw_digit && nw == 0) || nw > 255)
                return {};
            saw_digit = 1;
        } else if (c == '.' && saw_digit) {
            tmp += uint32_t(nw) << 8 * (3 - index);
            if (++index >= 4)
                return {};
            saw_digit = 0;
            nw = 0;
        } else {
            return {};
        }
    }
    if (index != 3 || !saw_digit)
        return {};
    tmp += uint32_t(nw) << 8 * (3 - index);
    return IPv4(tmp);
}
