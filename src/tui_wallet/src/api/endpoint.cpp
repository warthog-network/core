#include "endpoint.hpp"
#include "block/chain/height.hpp"
#include "general/hex.hpp"
#include "httplib.hpp"
#include "log.hpp"
#include "nlohmann/json.hpp"
#include "parse.hpp"
#include <algorithm>
#include <iostream>
using namespace std;
using namespace nlohmann;

template <typename T>
T Endpoint::parse_get(const std::string& path) const
{
    return Parse(http_get(path));
}

std::string Endpoint::http_get(const std::string& path) const
{
    auto ref { log_request("GET " + host + ":" + std::to_string(port) + path) };
    httplib::Client cli(host, port);
    cli.set_read_timeout(10);
    auto res = cli.Get(path);
    { // set success
        auto p { ref.lock() };
        if (p) {
            p->set_success(res);
        }
    }
    if (res) {
        return { std::move(res->body) };
    }
    throw std::runtime_error("GET request failed");
}

nlohmann::json Endpoint::extract_data(const std::string& json) const
{
    try {
        std::string error;
        auto parsed = json::parse(json);
        auto iter = parsed.find("error");
        if (iter != parsed.end() && !iter->is_null()) {
            error = iter->get<string>();
        }
        auto code { parsed["code"].get<int32_t>() };
        if (code != 0 || !error.empty()) {
            if (error.empty()) {
                throw std::runtime_error(std::format("Error without details returned by API, code {}", code));
            }
            throw std::runtime_error(std::format("API error \"{}\", code {}", error, code));
        } else {
            auto j = parsed["data"];
            return j;
        }
    } catch (...) {
        throw std::runtime_error("API response is malformed.");
    }
}

json Endpoint::api_get(const std::string& path) const
{
    return extract_data(http_get(path));
}

json Endpoint::api_post(const std::string& path, std::span<const uint8_t> postdata) const
{
    return extract_data(http_post(path, postdata));
}

json Endpoint::api_post(const std::string& path, std::string_view s) const
{
    std::span<const uint8_t> sp(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    return api_post(path, sp);
}

std::string Endpoint::http_post(const std::string& path, std::span<const uint8_t> postdata) const
{
    auto ref { log_request("POST " + path) };
    httplib::Client cli(host, port);
    cli.set_read_timeout(10);
    auto res = cli.Post(path, (const char*)postdata.data(), postdata.size(), ""s);
    { // set success
        auto p { ref.lock() };
        if (p) {
            p->set_success(res);
        }
    }
    if (res) {
        return { std::move(res->body) };
    }
    throw std::runtime_error("POST request failed");
}

std::pair<PinHeight, PinHash> Endpoint::get_pin()
{
    auto data(api_get("/chain/head"));
    std::string h = data["pinHash"].get<std::string>();
    auto pinHash { Hash::parse_throw(h) };

    auto v { data["pinHeight"].get<int32_t>() };
    if (auto pinHeight = Height(v).pin_height())
        return make_pair(*pinHeight, PinHash(pinHash));
    throw std::runtime_error(std::format("Invalid pinHeight {}.", v));
}

namespace {
struct GetBalanceData {
    struct {
        struct Bal {
            uint64_t u64;
            uint8_t precision;
            FundsDecimal funds() const { return { Funds_uint64(u64), TokenPrecision(precision) }; };
        };
        Bal total;
        Bal locked;
    } balance;
};
}
api::FundsBalance Endpoint::get_balance(const std::string& account, api::TokenIdOrSpec token) const
{
    std::string url = "/account/" + account + "/balance/" + token.to_string();
    auto p { parse_get<GetBalanceData>(url) };
    return { .total { p.balance.total.funds() }, .locked { p.balance.locked.funds() } };
}

api::FundsBalance Endpoint::wart_balance(const std::string& account) const
{
    return get_balance(account, TokenId::WART);
}

namespace {

struct GetTokenList {
    std::vector<api_types::TokenListEntry> matches;
};
}

api_types::TokenList Endpoint::token_complete(std::string_view namePrefix, std::string_view hashPrefix) const
{
    api_types::TokenList res { std::string(namePrefix), std::string(hashPrefix) };
    if (!std::all_of(namePrefix.begin(), namePrefix.end(), [](char c) {
            return isalnum(c);
        }))
        return res; // return empty list
    std::string url { std::format("/token/complete?namePrefix={}&hashPrefix={}", namePrefix, hashPrefix) };
    auto l { parse_get<GetTokenList>(url) };
    res.entries = std::move(l.matches);
    return res;
}
auto Endpoint::send_transaction(const std::string& txjson) -> TxHash
{
    cout << "=====DEBUG INFO TRANSACTION JSON=====\n"
         << txjson << "\n"
         << "=====================================" << endl;

    std::string url = "/transaction/add";
    auto hex_str(api_post(url, txjson)
            .at("txHash")
            .get<std::string>());
    return TxHash { Hash { HexRef(hex_str) } };
}

std::runtime_error Endpoint::failed_msg() const
{
    return std::runtime_error { "API request to host " + host + " at port " + std::to_string(port) + " failed. Are you running the node with RPC endpoint enabled?" };
};
