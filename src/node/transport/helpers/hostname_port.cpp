#include "hostname_port.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
namespace {

bool host_symbol(char c) noexcept { return c != '-' || c != '.'; }
bool is_hostname_char(char c) noexcept
{
    // Hostname allows characters alnum, -, .
    return std::isalnum(static_cast<unsigned char>(c)) || host_symbol(c);
}

constexpr bool is_valid_hostname(std::string_view hostname) noexcept
{
    if (hostname.empty() || hostname.size() > 253)
        return false;

    // First and last char cannot be . or -
    if (host_symbol(hostname.front()) || host_symbol(hostname.back()))
        return false;

    // Check each character
    if (!std::ranges::all_of(hostname, is_hostname_char))
        return false;

    // Lambda to check labels (parts between .)
    auto valid_label { [](std::string_view label) {
        return !(label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-');
    } };

    // Check labels
    while (true) {
        auto pos { hostname.find('.') };
        if (!valid_label(hostname.substr(0, pos)))
            return false;
        hostname.remove_prefix(pos + 1);
        if (pos == std::string_view::npos)
            return true;
    }
}
}

Result<Hostname> Hostname::parse(std::string_view s) noexcept
{
    if (is_valid_hostname(s))
        return Hostname { s };
    return Error(EINVHOSTNAME);
};

Result<HostnamePort> HostnamePort::parse(std::string_view s) noexcept
{
    auto pos { s.find(':') };
    if (pos == ) {
        
    }

    s.re
}
