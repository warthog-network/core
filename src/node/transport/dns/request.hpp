#pragma once
#include "general/result.hpp"
#include "transport/helpers/tcp_sockaddr.hpp"


struct DnsResolveResult{
    using Data = Result<std::vector<IPv4>>;
    Hostname host;
    Data result;
};

struct DnsResolveRequest {
    Hostname hostname;
    std::function<void(DnsResolveResult)> callback;
};
