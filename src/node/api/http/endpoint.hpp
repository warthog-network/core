#pragma once
#ifndef DISABLE_LIBUV
#define UWS_NO_ZLIB
#include "api/events/events.hpp"
#include "api/events/subscription_fwd.hpp"
#include "api/glaze/schema_aggregator.hpp"
#include "api/reply.hpp"
#include "api/types/shared.hpp"
#include "block/block.hpp"
#include "transport/helpers/tcp_sockaddr.hpp"
#include "uwebsockets/App.h"
#include <thread>
#include <variant>

struct ConfigParams;

class IndexGenerator {
public:
    void get(std::string_view s, std::string_view schemaName);
    void post(std::string_view s, std::string_view schemaName);
    void section(std::string s);
    APIReply result(bool isPublic) const;

private:
    void on_method(std::string_view method, std::string_view s, std::string_view schemaName);
    bool fresh { true };
    std::string inner;
};

class HTTPEndpoint {
    template <typename T>
    friend class RouterHook;

public:
    static constexpr bool SSL = false;
    using event_t = api::Event;
    static std::optional<HTTPEndpoint> make_public_endpoint(const ConfigParams&);
    HTTPEndpoint(TCPPeeraddr bind, bool isPublic = false);
    void start()
    {
        assert(!worker.joinable());
        worker = std::thread(&HTTPEndpoint::work, this);
    }
    ~HTTPEndpoint()
    {
        lc.loop->defer(std::bind(&HTTPEndpoint::shutdown, this));
        if (worker.joinable())
            worker.join();
    }
    void push_event(event_t e)
    {
        lc.loop->defer([this, e = std::move(e)]() mutable {
            on_event(std::move(e));
        });
    };

    void send_event(std::vector<subscription_ptr> subscribers, subscription::events::Event&&);

private:
    void dispatch(std::vector<subscription_ptr> subscribers, const std::string& serialized);
    void async_reply(uWS::HttpResponse<false>* res, APIReply r)
    {
        lc.loop->defer([this, res, r = std::move(r)] { reply_pending(res, std::move(r)); });
    }
    auto& router()
    {
        return app;
    }
    void insert_pending(uWS::HttpResponse<SSL>* res)
    {
        pendingRequests.insert(res);
        res->onAborted([this, res]() { on_aborted(res); });
    }
    void work();
    void shutdown();
    void on_event(event_t&& e);

    void reply_pending(uWS::HttpResponse<false>* res, const APIReply& r);
    static void write_cors_headers(uWS::HttpResponse<false>* res);
    static void reply(uWS::HttpResponse<false>* res, const APIReply& r);

    //////////////////////////////
    // handlers
    void on_aborted(uWS::HttpResponse<false>* res);
    void on_listen(us_listen_socket_t* ls);

    //////////////////////////////
    // variables
    IndexGenerator indexGenerator;
    SchemaAggregator schemaAggregator;
    std::set<uWS::HttpResponse<false>*> pendingRequests;
    TCPPeeraddr bind;
    bool isPublic;
    us_listen_socket_t* listen_socket = nullptr;
    const uWS::LoopCleaner lc;
    uWS::App app;
    bool bshutdown = false;
    std::thread worker;
};
#endif
