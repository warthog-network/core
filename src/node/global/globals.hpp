#pragma once
#include "config/config.hpp"
#include "health/health.hpp"
#include "general/logger/timing_logger.hpp"
#include <memory>

class BatchRegistry;
class HTTPEndpoint;
class PeerServer;
class ChainServer;
class Eventloop;
class TCPConnectionManager;
class WSConnectionManager;
namespace spdlog {
class logger;
}
namespace uvw {
class loop;
}
namespace rxtx {
class Server;
}

struct Global {
#ifndef DISABLE_LIBUV
    TCPConnectionManager* conman;
    WSConnectionManager* wsconman;
    HTTPEndpoint* httpEndpoint;
#endif
    ChainServer* chainServer;
    rxtx::Server* rxtxServer;
    PeerServer* peerServer;
    Eventloop* core;
    BatchRegistry* batchRegistry;
    std::unique_ptr<Health> health;
    std::shared_ptr<spdlog::logger> connLogger;
    std::shared_ptr<spdlog::logger> communicationLogger;
    wrt::optional<logging::TimingLogger> timingLogger;
    std::shared_ptr<spdlog::logger> syncdebugLogger;
    wrt::optional<Config> conf;
};
extern std::atomic<bool> shutdownSignal;

const Global& global();
inline spdlog::logger& connection_log() { return *global().connLogger; }
inline auto& timing_log() { return global().timingLogger.value(); }
inline spdlog::logger& syncdebug_log() { return *global().syncdebugLogger; }
inline spdlog::logger& communication_log() { return *global().communicationLogger; }
const Config& config();
Health& health();
Config& set_config();
int init_config(int argc, char** argv);
void start_global_services();

#ifndef DISABLE_LIBUV
HTTPEndpoint& http_endpoint();
void global_init(BatchRegistry* pbr, rxtx::Server* ts, PeerServer* pps, ChainServer* pcs, TCPConnectionManager* pcm, WSConnectionManager* wcm, Eventloop* pel, HTTPEndpoint* httpEndpoint);
#else
void global_init(BatchRegistry* pbr, rxtx::Server* ts, PeerServer* pps, ChainServer* pcs, Eventloop* pel);
#endif

struct GlobalsSession {
    GlobalsSession();
    ~GlobalsSession();
};
