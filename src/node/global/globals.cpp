#include "globals.hpp"
#include "crypto/crypto.hpp"
#include "general/is_testnet.hpp"
#include "general/logger/log_memory.hpp"
#include "spdlog/sinks/callback_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"
#include "transport/tcp/conman.hpp"
#include <chrono>

// global variables
std::atomic<bool> shutdownSignal;

namespace {

std::string logdir()
{
    if (is_testnet()) {
        return "logs_testnet";
    } else {
        return "logs";
    }
}

[[nodiscard]] auto create_log(std::string_view name, size_t sizeMegabytes = 5, size_t nFiles = 3)
{
    size_t max_size = 1048576 * sizeMegabytes;
    using namespace std::string_literals;
    std::string filename { config().get_default_datadir() + logdir() + "/"s + std::string(name) + ".log"s };
    spdlog::info("Logging to {}", filename);
    return spdlog::rotating_logger_mt(std::string(name), filename, max_size, nFiles);
}

auto create_connection_logger()
{
    auto res { create_log("connections") };
    res->flush_on(spdlog::level::info);
    return res;
}
}

#ifdef DISABLE_LIBUV
#include "emscripten/proxying.h"
emscripten::ProxyingQueue globalProxyingQueue;
#endif

namespace {
Global globalinstance;
}

const Global& global()
{
    return globalinstance;
}

int init_config(int argc, char** argv)
{
    auto params { ConfigParams::from_args(argc, argv) };
    if (!params.has_value())
        return params.error();
    globalinstance.conf.emplace(std::move(params.value()));
    return 1;
};

const Config& config()
{
    return *globalinstance.conf;
}

Health& health(){
    return *globalinstance.health;
}

Config& set_config()
{
    return *globalinstance.conf;
}

#ifndef DISABLE_LIBUV
HTTPEndpoint& http_endpoint()
{
    return *globalinstance.httpEndpoint;
};

void global_init(BatchRegistry* pbr, rxtx::Server* ts, PeerServer* pps, ChainServer* pcs, TCPConnectionManager* pcm, WSConnectionManager* wcm, Eventloop* pel, HTTPEndpoint* httpEndpoint)
#else
void global_init(BatchRegistry* pbr, rxtx::Server* ts, PeerServer* pps, ChainServer* pcs, Eventloop* pel)
#endif
{
#ifndef DISABLE_LIBUV
    globalinstance.health = std::make_unique<Health>();
    globalinstance.conman = pcm;
    globalinstance.wsconman = wcm;
    globalinstance.httpEndpoint = httpEndpoint;
#endif
    globalinstance.batchRegistry = pbr;
    globalinstance.peerServer = pps;
    globalinstance.rxtxServer = ts;
    globalinstance.chainServer = pcs;
    globalinstance.core = pel;
    globalinstance.connLogger = create_connection_logger();
    globalinstance.syncdebugLogger = create_log("syncdebug");
    globalinstance.timingLogger.emplace(create_log("timing", 50, 10), create_log("longrunning", 50, 10));
    globalinstance.communicationLogger = create_log("communication", 50, 3);
};

namespace {
void initialize_logging()
{
    using namespace std::string_literals;
    using namespace std::chrono_literals;
    spdlog::flush_every(5s);
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg& msg) {
        logging::logMemory.add_entry(msg);
    });
    callback_sink->set_level(spdlog::level::debug);
    spdlog::default_logger()->sinks().push_back(callback_sink);
}

void initialize_srand()
{
    using namespace std::chrono;
    srand(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}

GlobalsSession::GlobalsSession()
{
    ECC_Start();
    initialize_logging();
    initialize_srand();
}
GlobalsSession::~GlobalsSession()
{
    ECC_Stop();
    spdlog::shutdown();
}
