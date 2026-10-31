#include "endpoint.hpp"
#include "api/http/parse.hpp"
#include "api/types/accountid_or_address.hpp"
#include "api/types/all.hpp"
#include "chainserver/transaction_ids.hpp"
#include "communication/mining_task.hpp"
#include "general/hex.hpp"
#include "json.hpp"
#include "spdlog/spdlog.h"
#include "version.hpp"
#include <charconv>
#include <iostream>
#include <type_traits>
using namespace std::placeholders;

namespace {

struct ParameterParser {
    std::string_view sv;
    template <typename T>
    requires std::is_integral_v<T>
    operator T()
    {
        T res {};
        auto result = std::from_chars(sv.data(), sv.end(), res);
        if (result.ec != std::errc {} || result.ptr != sv.end()) {
            throw Error(EINV_ARGS);
        }
        return res;
    }
    operator API::HistoryIdOrHash()
    {
        if (sv.length() == 64)
            return { Hash { *this } };
        return { HistoryId { static_cast<uint64_t>(*this) } };
    }
    operator API::HeightOrHash()
    {
        if (sv.length() == 64)
            return { Hash { *this } };
        return { Height { *this } };
    }
    operator API::AccountIdOrAddress()
    {
        if (sv.length() == 48)
            return { Address { *this } };
        return { AccountId { static_cast<uint64_t>(*this) } };
    }
    operator PrivKey()
    {
        return PrivKey(sv);
    }
    operator Funds()
    {
        return Funds::parse_throw(sv);
    }
    operator Page()
    {
        return static_cast<uint32_t>(*this);
    }
    operator Hash()
    {
        return hex_to_arr<32>(sv);
    }
    operator NonzeroHeight()
    {
        return Height(static_cast<uint32_t>(*this)).nonzero_throw(EBADHEIGHT);
    }
    operator Height()
    {
        return Height(static_cast<uint32_t>(*this));
    }
    operator std::string_view(){
        return sv;
    }
    operator Address()
    {
        return Address(sv);
    }
};
void send_json(uWS::HttpResponse<false>* res, const std::string& s)
{
    res->writeHeader("Content-type", "application/json; charset=utf-8");
    res->end(s, true);
}

void send_html(uWS::HttpResponse<false>* res, const std::string& s)
{
    res->writeHeader("Content-type", "text/html; charset=utf-8");
    res->end(s, true);
}
} // namespace

void IndexGenerator::get(std::string s)
{
    inner += "            <li>GET <a href=" + s + ">" + s + "</a></li>";
}
void IndexGenerator::post(std::string s)
{
    inner += "            <li>POST <a href=" + s + ">" + s + "</a></li>";
}
void IndexGenerator::section(std::string s)
{
    if (!fresh) {
        inner += "        </ul>";
    } else
        fresh = false;
    inner += R"(        <h2>)" + s + R"(</h2>
        <ul>)";
}
std::string IndexGenerator::result(bool isPublic) const
{
    return R"HTML(
<!doctype html>
<html>
    <head>
        <meta charset="utf-8" />
        <title>endpoint methods</title>
    </head>
    <body>
        <h1>)HTML"
        + std::string(isPublic ? "Public " : "") + "API for Warthog node version " CMDLINE_PARSER_VERSION + "</h1>\n"
        + inner
        +
        R"HTML(</ul>
    </body>
</html>)HTML";
}

void HTTPEndpoint::work()
{
    app.get("/", [&](uWS::HttpResponse<false>* res, uWS::HttpRequest*) {
        send_html(res, indexGenerator.result(isPublic));
    });

    indexGenerator.section("Transaction Endpoints");
    post("/transaction/add", parse_payment_create, put_mempool);
    get("/transaction/mempool", get_mempool);
    get_1("/transaction/lookup/:txid", lookup_tx);
    get("/transaction/latest", get_latest_transactions);
    get("/transaction/minfee", get_transaction_minfee);

    indexGenerator.section("Settings");
    get_1("/settings/mempool/minfee/:feeE8", set_minfee, true);

    indexGenerator.section("Chain Endpoints");
    get("/chain/head", get_block_head);
    get("/chain/grid", get_chain_grid, true);
    get_1("/chain/block/:id/hash", get_chain_hash);
    get_1("/chain/block/:id/header", get_chain_header);
    get_1("/chain/block/:id", get_chain_block);
    get_1("/chain/mine/:account", get_chain_mine);
    get_1("/chain/mine/:account/log", get_chain_mine);
    get("/chain/signed_snapshot", get_signed_snapshot, true);
    get("/chain/txcache", get_txcache);
    get_1("/chain/hashrate/:window", get_hashrate_n);
    get_3("/chain/hashrate/chart/:from/:to/:window", get_hashrate_chart, true);
    post("/chain/append", parse_mining_task, put_chain_append, true);

    indexGenerator.section("Account Endpoints");
    get_1("/account/:account/balance", get_account_balance);
    get_2("/account/:account/history/:beforeTxIndex", get_account_history);
    get("/account/richlist", get_account_richlist);

    indexGenerator.section("Peers Endpoints");
    get("/peers/ip_count", inspect_conman, jsonmsg::ip_counter);
    get("/peers/banned", get_banned_peers);
    get("/peers/unban", unban_peers, true);
    get_1("/peers/offenses/:page", get_offenses);
    get("/peers/connected", get_connected_peers2, true);
    get("/peers/connected/connection", get_connected_connection);
    get("/peers/endpoints", inspect_eventloop, jsonmsg::endpoints, true);
    get("/peers/connect_timers", inspect_eventloop, jsonmsg::connect_timers, true);

    indexGenerator.section("Tools Endpoints");
    get_1("/tools/encode16bit/from_e8/:feeE8", get_round16bit_e8);
    get_1("/tools/encode16bit/from_string/:string", get_round16bit_funds);
    get("/tools/version", get_version);
    get("/tools/wallet/new", get_wallet_new);
    get_1("/tools/wallet/from_privkey/:privkey", get_wallet_from_privkey);
    get_1("/tools/janushash_number/:headerhex", get_janushash_number);

    indexGenerator.section("Debug Endpoints");
    get("/debug/header_download", inspect_eventloop, jsonmsg::header_download, true);
    app.ws<int>("/ws/chain_delta", {
                                       .open = [](auto* ws) {
                                           ws->subscribe(API::Block::WEBSOCKET_EVENT);
                                           ws->subscribe(API::Rollback::WEBSOCKET_EVENT);
                                       },
                                   });
    app.listen(bind.ipv4.to_string(), bind.port, std::bind(&HTTPEndpoint::on_listen, this, _1));
    lc.loop->run();
}

std::optional<HTTPEndpoint> HTTPEndpoint::make_public_endpoint(const Config&)
{
    auto& pAPI { config().publicAPI };
    if (!pAPI)
        return {};
    return std::optional<HTTPEndpoint> { std::in_place, pAPI->bind, true };
};

HTTPEndpoint::HTTPEndpoint(EndpointAddress bind, bool isPublic)
    : bind(bind)
    , isPublic(isPublic)
    , app(lc.loop)
{
    spdlog::info("RPC {}endpoint is {}.", isPublic ? "public " : "", bind.to_string());
    t = std::thread(&HTTPEndpoint::work, this);
}

void HTTPEndpoint::get(std::string pattern, auto asyncfun, auto serializer, bool priv)
{
    if (priv && isPublic)
        return;
    indexGenerator.get(pattern);
    app.get(pattern,
        [this, asyncfun, serializer, pattern](auto* res, auto* req) {
            spdlog::debug("GET {}", req->getUrl());
            asyncfun(
                [this, res, serializer](auto& data) {
                    async_reply(res, serializer(data));
                });
            pendingRequests.insert(res);
            res->onAborted([this, res]() { on_aborted(res); });
        });
}

void HTTPEndpoint::get(std::string pattern, auto asyncfun, bool priv)
{
    if (priv && isPublic)
        return;
    indexGenerator.get(pattern);
    app.get(pattern,
        [this, asyncfun, pattern](auto* res, auto* req) {
            spdlog::debug("GET {}", req->getUrl());
            asyncfun(
                [this, res]<typename T>(T&& data) {
                    async_reply(res, jsonmsg::serialize(std::forward<T>(data)));
                });
            pendingRequests.insert(res);
            res->onAborted([this, res]() { on_aborted(res); });
        });
}

void HTTPEndpoint::get_1(std::string pattern, auto asyncfun, bool priv)
{
    if (priv && isPublic)
        return;
    indexGenerator.get(pattern);
    app.get(pattern,
        [this, asyncfun, pattern](auto* res, auto* req) {
            spdlog::debug("GET {}", req->getUrl());
            try {
                ParameterParser p1 { req->getParameter(0) };
                asyncfun(p1,
                    [this, res](auto& data) {
                        async_reply(res, jsonmsg::serialize(data));
                    });
                pendingRequests.insert(res);
                res->onAborted([this, res]() { on_aborted(res); });
            } catch (Error e) {
                send_json(res, jsonmsg::serialize(tl::make_unexpected(e.e)));
            }
        });
}
void HTTPEndpoint::get_2(std::string pattern, auto asyncfun, bool priv)
{
    if (priv && isPublic)
        return;
    indexGenerator.get(pattern);
    app.get(pattern,
        [this, asyncfun, pattern](auto* res, auto* req) {
            spdlog::debug("GET {}", req->getUrl());
            try {
                ParameterParser p1 { req->getParameter(0) };
                ParameterParser p2 { req->getParameter(1) };
                asyncfun(p1, p2,
                    [this, res](auto& data) {
                        async_reply(res, jsonmsg::serialize(data));
                    });
                pendingRequests.insert(res);
                res->onAborted([this, res]() { on_aborted(res); });
            } catch (Error e) {
                send_json(res, jsonmsg::serialize(tl::make_unexpected(e.e)));
            }
        });
}
void HTTPEndpoint::get_3(std::string pattern, auto asyncfun, bool priv)
{
    if (priv && isPublic)
        return;
    indexGenerator.get(pattern);
    app.get(pattern,
        [this, asyncfun, pattern](auto* res, auto* req) {
            spdlog::debug("GET {}", req->getUrl());
            try {
                ParameterParser p1 { req->getParameter(0) };
                ParameterParser p2 { req->getParameter(1) };
                ParameterParser p3 { req->getParameter(2) };
                asyncfun(p1, p2, p3,
                    [this, res](auto& data) {
                        async_reply(res, jsonmsg::serialize(data));
                    });
                pendingRequests.insert(res);
                res->onAborted([this, res]() { on_aborted(res); });
            } catch (Error e) {
                send_json(res, jsonmsg::serialize(tl::make_unexpected(e.e)));
            }
        });
}

void HTTPEndpoint::post(std::string pattern, auto parser, auto asyncfun, bool priv)
{
    if (priv && isPublic)
        return;
    indexGenerator.post(pattern);
    app.post(pattern,
        [this, pattern, parser = std::move(parser), asyncfun = std::move(asyncfun)](auto* res, uWS::HttpRequest* req) {
            spdlog::debug("POST {}", req->getUrl());
            std::vector<uint8_t> body;

            pendingRequests.insert(res);
            res->onData(
                [this, asyncfun = std::move(asyncfun), parser = std::move(parser), res, body = std::move(body)](std::string_view data, bool last) mutable {
                    body.insert(body.end(), data.begin(), data.end());
                    if (last) {
                        try {
                            asyncfun(parser(body),
                                [this, res](auto& data) {
                                    async_reply(res, jsonmsg::serialize(data));
                                });
                        } catch (Error e) {
                            auto ser = jsonmsg::serialize(tl::make_unexpected(e.e));
                            async_reply(res, ser);
                        }
                    }
                });
            res->onAborted([this, res]() { on_aborted(res); });
        });
}

void HTTPEndpoint::shutdown()
{
    bshutdown = true;
    if (listen_socket != nullptr) {
        us_listen_socket_close(0, listen_socket);
        listen_socket = nullptr;
    }
}

void HTTPEndpoint::on_event(WebsocketEvent&& e)
{
    std::visit([&](auto&& e) {
        handle_event(std::move(e));
    },
        std::move(e));
}

void HTTPEndpoint::handle_event(const API::Block& b)
{
    auto txt { nlohmann::json {
        { "type", "blockAppend" },
        { "data", jsonmsg::to_json(b) } }
                   .dump() };
    app.publish(b.WEBSOCKET_EVENT, txt, uWS::OpCode::TEXT);
}

void HTTPEndpoint::handle_event(const API::Rollback& r)
{
    auto txt { nlohmann::json {
        { "type", "rollback" },
        { "data", jsonmsg::to_json(r) } }
                   .dump() };
    app.publish(r.WEBSOCKET_EVENT, txt, uWS::OpCode::TEXT);
}
void HTTPEndpoint::send_reply(uWS::HttpResponse<false>* res, const std::string& s)
{
    auto iter = pendingRequests.find(res);
    if (iter != pendingRequests.end()) {
        send_json(res, s);
        pendingRequests.erase(iter);
    }
}

void HTTPEndpoint::on_aborted(uWS::HttpResponse<false>* res)
{
    pendingRequests.erase(res);
}

void HTTPEndpoint::on_listen(us_listen_socket_t* ls)
{
    listen_socket = ls;
    if (listen_socket) {
        if (bshutdown) {
            us_listen_socket_close(0, listen_socket);
        }
    } else
        throw std::runtime_error("Cannot listen on " + bind.to_string());
}
