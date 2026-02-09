#include "tcp_sockaddr.hpp"
#include <cassert>
#include <cstring>

std::string TCPPeeraddr::to_string() const
{
    return ip.to_string() + ":" + std::to_string(port);
}

std::string WSPeeraddr::to_string() const
{
    return ip.to_string() + ":" + std::to_string(port);
}
