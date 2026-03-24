#pragma once
#include "api/http/json.hpp"
#include "general/static_string.hpp"
#include "tools/try_parse.hpp"
#include "types/opt_param.hpp"
// #include "general/funds.hpp"
#include "api/http/parse.hpp"
#include "api/types/accountid_or_address.hpp"
#include "api/types/input.hpp"
#include "api/types/shared.hpp"
#include "chainserver/transaction_ids.hpp"
#include "communication/mining_task.hpp"
#include "communication/rxtx_server/rxtx_server.hpp"
#include "http/json.hpp"
#include "spdlog/spdlog.h"
#include "uwebsockets/HttpParser.h"
#include <string>

namespace {

template <typename T>
struct ArgCount {
    static_assert(false, "Can only count args of function pointers");
};
template <typename R, typename... Types>
struct ArgCount<R (*)(Types...)> {
    static constexpr size_t value = sizeof...(Types);
};

template <typename T>
constexpr auto count_fnptr_args { ArgCount<T>::value };

template <typename R, typename... Types>
static constexpr size_t getArgumentCount(R (*)(Types...))
{
    return sizeof...(Types);
}

struct ParameterParser {
    std::string_view sv;
    template <typename T>
    requires std::is_integral_v<T>
    operator T()
    {
        if (auto p { try_parse<T>(sv) })
            return *p;
        throw Error(EINV_ARGS);
    }

    template <typename T>
    operator OptParam<T>()
    {
        if (sv.length() == 0) {
            return OptParam<T>({});
        }
        return OptParam<T>(std::optional<T>(T(*this)));
    }
    operator api::HeightOrHash()
    {
        if (sv.length() == 64)
            return { Hash { *this } };
        return { Height { *this } };
    }
    operator api::AccountIdOrAddress()
    {
        if (sv.length() == 48)
            return { Address { *this } };
        return { AccountId { static_cast<uint64_t>(*this) } };
    }
    operator HistoryId()
    {
        return HistoryId(static_cast<uint64_t>(*this));
    }
    operator TokenDecimals()
    {
        return static_cast<uint64_t>(*this);
    }
    operator PrivKey()
    {
        return PrivKey(sv);
    }
    operator api::AssetIdOrHash()
    {
        if (sv.length() == 64)
            return { AssetHash(*this) };
        return { AssetId(*this) };
    }
    operator api::TokenIdOrSpec()
    {
        if (sv.length() >= 64)
            return { api::TokenSpec::parse_throw(*this) };
        return { TokenId(*this) };
    }
    operator ParsedFunds()
    {
        return ParsedFunds(sv);
    }
    operator Wart()
    {
        return Wart::parse_throw(sv);
    }
    operator Page()
    {
        return static_cast<uint32_t>(*this);
    }
    operator std::string()
    {
        return std::string(sv);
    }
    operator Hash()
    {
        return Hash(HexRef(sv));
    }
    operator TxHash()
    {
        return TxHash(static_cast<Hash>(*this));
    }
    operator NonzeroHeight()
    {
        return Height(static_cast<uint32_t>(*this)).nonzero_throw(EBADHEIGHT);
    }
    operator Height()
    {
        return Height(static_cast<uint32_t>(*this));
    }
    operator std::string_view()
    {
        return sv;
    }
    operator Address()
    {
        return Address(sv);
    }
};

struct UrlArgsCount {
protected:
    struct GetIndex {
        bool labeled;
        size_t i;
    };

public:
    size_t UnlabeledArgs { 0 };
    size_t TotalArgs { 0 };
    constexpr GetIndex get_index(size_t i) const
    {
        if (i < UnlabeledArgs)
            return { .labeled = false, .i = i };
        if (i < TotalArgs)
            return { .labeled = true, .i = i - UnlabeledArgs };
        throw std::runtime_error("Out of bounds");
    }
};

struct UrlArgs : public UrlArgsCount {
    static constexpr size_t MaxLabels { 5 };
    using Labels = std::array<std::string_view, MaxLabels>;
    Labels labels;
    std::string_view pattern;

    template <GetIndex gi>
    std::string_view get(auto& req) const
    {
        if constexpr (gi.labeled)
            return req->getQuery(labels[gi.i]);
        return req->getParameter(gi.i);
    }

    constexpr UrlArgs(std::string_view patternWithQuery)
    {
        bool enteredParam { false };
        size_t n { 0 };
        for (; n < patternWithQuery.size(); ++n) {
            auto c { patternWithQuery[n] };
            if (c == '?')
                break;
            if (c == ':') {
                UnlabeledArgs += !enteredParam;
                TotalArgs += 1;
                enteredParam = true;
            } else if (c == '/')
                enteredParam = false;
        }
        pattern = patternWithQuery.substr(0, n);
        if (n == patternWithQuery.size()) // no query string
            return;

        auto s { patternWithQuery.substr(n + 1) };

        for (size_t i = 0; i < MaxLabels; ++i) {
            if (s.empty())
                return;
            n = s.find('=');
            if (n == s.npos)
                throw std::runtime_error("Cannot parse query.");
            TotalArgs += 1;
            labels[i] = { s.begin(), s.begin() + n };

            n = s.find('&', n + 1);
            if (n == s.npos)
                s = {};
            else
                s = s.substr(n + 1);
        }
        if (!s.empty())
            throw std::runtime_error("Too many query arguments.");
    }
    constexpr size_t sum_sizes() const
    {
        size_t N { 0 };
        for (auto& s : labels) {
            N += s.size();
        }
        return N;
    }
};
}

template <UrlArgsCount urlArgs>
struct UrlArgsRetriever {
    template <size_t I>
    requires(I < urlArgs.TotalArgs)
    constexpr static std::string_view get()
    {
        return {};
    }
};

template <typename T>
class RouterHook {
    T& t;

public:
    RouterHook(T& t)
        : t(t) { };
    struct Options {
        bool priv = true;
        bool hidden = true;
    };

    template <StaticString s>
    void GET_INTERNAL(Options opts, auto asyncfun, auto serializer)
    {
        constexpr UrlArgs args { s.value };
        auto& t { this->t };
        if (opts.priv && t.isPublic)
            return;
        if (!opts.hidden)
            t.indexGenerator.get(std::string(s.value));
        constexpr size_t ARGC = count_fnptr_args<std::remove_cvref_t<decltype(asyncfun)>>;
        t.router().get(std::string(args.pattern),
            [&t, asyncfun = std::forward<decltype(asyncfun)>(asyncfun), serializer, args](auto* res, auto* req) {
                constexpr UrlArgsCount argsCount { UrlArgs { s.value } };
                spdlog::debug("GET {}", req->getUrl());
                try {
                    static_assert(ARGC > 0); // last argument is for callback

                    [&]<size_t... Ids>(std::index_sequence<Ids...>) {
                        asyncfun(ParameterParser(args.get<argsCount.get_index(Ids)>(req))...,
                            [&t, res, serializer](auto& data) {
                                t.async_reply(res, serializer(data));
                            });
                    }(std::make_index_sequence<ARGC - 1>());
                    t.insert_pending(res);
                } catch (Error e) {
                    t.reply_json(res, jsonmsg::serialize(tl::make_unexpected(e)));
                }
            });
    }

    template <StaticString s>
    void GET_INTERNAL(Options opts, auto asyncfun)
    {
        constexpr UrlArgs args { s.value };
        auto& t { this->t };
        if (opts.priv && t.isPublic)
            return;
        if (!opts.hidden)
            t.indexGenerator.get(std::string(s.value));
        constexpr size_t ARGC = count_fnptr_args<std::remove_cvref_t<decltype(asyncfun)>>;
        t.router().get(std::string(args.pattern),
            [&t, asyncfun = std::forward<decltype(asyncfun)>(asyncfun), args](auto* res, auto* req) {
                constexpr UrlArgsCount argsCount { UrlArgs { s.value } };
                spdlog::debug("GET {}", req->getUrl());
                try {
                    static_assert(ARGC > 0); // last argument is for callback

                    [&]<size_t... Ids>(std::index_sequence<Ids...>) {
                        asyncfun(ParameterParser(args.get<argsCount.get_index(Ids)>(req))...,
                            [&t, res](auto& data) {
                                t.async_reply(res, jsonmsg::serialize(data));
                            });
                    }(std::make_index_sequence<ARGC - 1>());
                    t.insert_pending(res);
                } catch (Error e) {
                    t.reply_json(res, jsonmsg::serialize(tl::make_unexpected(e)));
                }
            });
    }
    template <StaticString s, typename... Ts>
    void GET_PUB(Ts&&... ts)
    {
        Options opts {
            .priv = false,
            .hidden = false,
        };
        GET_INTERNAL<s>(opts, std::forward<Ts>(ts)...);
    }
    // template <typename... Ts>
    // void GET_PUB(Ts&&... ts)
    // {
    //     Options opts {
    //         .priv = false,
    //         .hidden = false,
    //     };
    //     GET_INTERNAL(opts, std::forward<Ts>(ts)...);
    // }
    template <StaticString s, typename... Ts>
    void GET_PUB_HIDDEN(Ts&&... ts)
    {
        Options opts {
            .priv = false,
            .hidden = true,
        };
        GET_INTERNAL<s>(opts, std::forward<Ts>(ts)...);
    }
    template <StaticString s, typename... Ts>
    void GET_PRIV(Ts&&... ts)
    {
        Options opts {
            .priv = true,
            .hidden = false,
        };
        GET_INTERNAL<s>(opts, std::forward<Ts>(ts)...);
    }

    void POST_INTERNAL(bool priv, std::string pattern, auto parser, auto asyncfun)
    {
        auto& t { this->t };
        if (priv && t.isPublic)
            return;
        t.indexGenerator.post(pattern);
        t.router().post(pattern,
            [&t, parser = std::move(parser), asyncfun = std::move(asyncfun)](auto* res, auto* req) {
                spdlog::debug("POST {}", req->getUrl());
                std::vector<uint8_t> body;

                res->onData(
                    [&t, asyncfun = std::move(asyncfun), parser = std::move(parser), res, body = std::move(body)](std::string_view data, bool last) mutable {
                        body.insert(body.end(), data.begin(), data.end());
                        if (last) {
                            try {
                                asyncfun(parser(body),
                                    [&t, res](auto& data) {
                                        t.async_reply(res, jsonmsg::serialize(data));
                                    });
                            } catch (Error e) {
                                auto ser = jsonmsg::serialize(tl::make_unexpected(e));
                                t.async_reply(res, ser);
                            }
                        }
                    });
                t.insert_pending(res);
            });
    }
    template <StaticString s, typename... Ts>
    void POST_PUB(Ts&&... ts)
    {
        POST_INTERNAL(false, s.value, std::forward<Ts>(ts)...);
    }
    template <StaticString s, typename... Ts>
    void POST_PRIV(Ts&&... ts)
    {
        POST_INTERNAL(true, s.value, std::forward<Ts>(ts)...);
    }
    void SECTION(std::string name)
    {
        t.indexGenerator.section(std::move(name));
    }
    void hook_endpoints()
    {
        using namespace chainserver;
        using namespace market_history;
        SECTION("Transaction Endpoints");
        POST_PUB<"/transaction/add">(parse_transaction_create, api_call<PutMempool>);
        GET_PUB<"/transaction/mempool">(api_call<GetMempool>);
        GET_PUB<"/transaction/lookup/:txid">(api_call<LookupTxHash>);
        GET_PUB<"/transaction/latest">(get_latest_transactions);
        GET_PUB<"/transaction/minfee">(get_transaction_minfee);

        SECTION("Settings Endpoints");
        GET_PRIV<"/settings/mempool/minfee/:feeE8">(set_minfee);

        SECTION("Chain Endpoints");
        GET_PUB<"/chain/head">(get_block_head);
        GET_PRIV<"/chain/grid">(api_call<GetGrid>);
        GET_PUB<"/chain/block/:id/hash">(api_call<GetBlockHash>);
        GET_PUB<"/chain/block/:id/header">(api_call<GetHeader>);
        GET_PUB<"/chain/block/:id/binary">(api_call<GetBlockBinary>);
        GET_PUB<"/chain/block/:id">(api_call<GetBlock>);
        GET_PUB<"/chain/mine/:account">(get_chain_mine);
        GET_PUB<"/chain/txcache">(api_call<GetTxcache>);
        GET_PUB<"/chain/hashrate/:window">(get_hashrate_n);
        GET_PRIV<"/chain/signed_snapshot">(get_signed_snapshot);
        GET_PRIV<"/chain/hashrate/chart/block/:from/:to/:window">(get_hashrate_block_chart);
        GET_PRIV<"/chain/hashrate/chart/time/:from/:to/:interval">(get_hashrate_time_chart);
        POST_PRIV<"/chain/append">(parse_block_worker, put_chain_append);

        SECTION("Token Endpoints");
        // GET_PUB_HIDDEN<"/token/complete/">(api_call<ListTokens>);
        GET_PUB<"/token/complete?namePrefix=...&hashPrefix=...">(api_call<CompleteToken>);

        SECTION("Market Endpoints");
        GET_PUB<"/market/:market">(api_call<MarketDetail>);
        GET_PUB<"/market/:market/order/:historyId">(api_call<OrderDetail>);
        //
        SECTION("Account Endpoints");
        GET_PUB<"/account/:account/mempool">(api_call<GetAccountMempool>);
        GET_PUB<"/account/:account/open_orders">(api_call<GetAccountOrders>);
        GET_PUB<"/account/:account/open_orders/:asset">(api_call<GetAccountOrdersAsset>);
        GET_PUB<"/account/:account/balance/:token">(api_call<GetTokenBalance>);
        GET_PUB<"/account/:account/wart_balance">(api_call<GetWartBalance>);
        GET_PUB<"/account/:account/history/:beforeTxIndex">(api_call<GetAccountHistory>);
        GET_PUB<"/account/richlist/:token">(get_token_richlist);

        SECTION("Peers Endpoints");
        GET_PUB<"/peers/ip_count">(get_ip_count);
        GET_PUB<"/peers/banned">(get_banned_peers);
        GET_PUB<"/peers/offenses/:page">(get_offenses);
        GET_PUB<"/peers/connected/connection">(get_connected_connection);
        GET_PUB<"/peers/connection_schedule">(get_connection_schedule);
        GET_PRIV<"/peers/unban">(unban_peers);
        GET_PRIV<"/peers/connected">(get_connected_peers2);
        GET_PRIV<"/peers/disconnect/:id">(disconnect_peer);
        GET_PRIV<"/peers/throttled">(get_throttled_peers);
        GET_PRIV<"/peers/transmission_hours">(get_transmission_hours);
        GET_PRIV<"/peers/transmission_minutes">(get_transmission_minutes);
        // GET_PRIV<t,"/peers/endpoints">( inspect_eventloop, jsonmsg::endpoints);
        // GET_PRIV<t,"/peers/connect_timers">( inspect_eventloop, jsonmsg::connect_timers);

        SECTION("Chart Endpoint");
        GET_PUB<"/chart/candles/:asset/:interval?from=...&to=...&n=...">(api_call<GetCandles>);
        GET_PUB<"/chart/trades/:asset?from=...&to=...&n=...">(api_call<GetTrades>);

        SECTION("Tools Endpoints");
        GET_PUB<"/tools/encode16bit/from_e8/:feeE8">(get_round16bit_e8);
        GET_PUB<"/tools/encode16bit/from_string/:string">(get_round16bit_funds);
        GET_PUB<"/tools/parse_price/:price/:decimals">(parse_price);
        GET_PUB<"/tools/info">(get_info);
        GET_PRIV<"/tools/wallet/new">(get_wallet_new);
        GET_PUB<"/tools/wallet/from_privkey/:privkey">(get_wallet_from_privkey);
        GET_PUB<"/tools/janushash_number/:headerhex">(get_janushash_number);
        GET_PUB<"/tools/sample_verified_peers/:number">(sample_verified_peers);

        SECTION("Debug Endpoints");
        GET_PRIV<"/debug/header_download">(inspect_eventloop, jsonmsg::header_download);
        GET_PRIV<"/loadtest/block_request/:conn_id">(loadtest_block);
        GET_PRIV<"/loadtest/header_request/:conn_id">(loadtest_header);
        GET_PRIV<"/loadtest/disable/:conn_id">(loadtest_disable);
        GET_PRIV<"/debug/fakemine">(api_call<FakeMineToZero>);
        GET_PRIV<"/debug/rollback">(api_call<Rollback>);
        GET_PRIV<"/debug/fakemine/:address">(api_call<FakeMine>);
    }
};

template <typename T>
void hook_endpoints(T&& t)
{
    RouterHook<std::remove_reference_t<T>>(std::forward<T>(t)).hook_endpoints();
}
