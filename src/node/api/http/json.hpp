#pragma once
#include "api/events/subscription.hpp"
#include "api/interface.hpp"
#include "general/errors.hpp"
#include "nlohmann/json.hpp"
struct Head;
class Hash;
class TxHash;
class Header;
namespace jsonmsg {
using namespace nlohmann;

json to_json(Wart);
json to_json(const FundsDecimal&, bool prec = true);
json to_json(const Grid&);
json to_json(const Hash&);
json to_json(const PeerDB::BanEntry&);
json to_json(const Peeraddr&);
json to_json(const TCPPeeraddr&);
json to_json(const TxHash&);
json to_json(const api::Account&);
json to_json(const api::AccountHistory&);
json to_json(const api::AddressCount&);
json to_json(const api::AssetSearchResult&);
json to_json(const api::Block&);
json to_json(const api::BlockBinary&);
json to_json(const api::BlockSummary&);
json to_json(const api::ChainHead&);
json to_json(const api::FundsBalance&);
json to_json(const api::HashrateBlockChart&);
json to_json(const api::HashrateInfo&);
json to_json(const api::HashrateTimeChart&);
json to_json(const api::Head&);
json to_json(const api::HeaderInfo&);
json to_json(const api::JanushashNumber&);
json to_json(const api::LiquidityPool&, TokenDecimals baseDecimals, bool prec=true);
json to_json(const api::MempoolEntries&);
json to_json(const api::MempoolUpdate&);
json to_json(const api::MiningState&);
json to_json(const api::MarketOrders&);
json to_json(const api::MarketDetail&);
json to_json(const api::OrderDetail&);
json to_json(const api::ParsedPrice&);
json to_json(const api::Peerinfo&);
json to_json(const api::PeerinfoConnections&);
json to_json(const api::Richlist&, TokenDecimals p);
json to_json(const api::RichlistInfo&);
json to_json(const api::ThrottleState&);
json to_json(const api::ThrottledPeer&);
json to_json(const api::Token&);
json to_json(const api::TokenBalanceLookup&);
json to_json(const api::Transaction&);
json to_json(const api::TransactionMinfee&);
json to_json(const api::TransactionsByBlocks&);
json to_json(const api::TransmissionTimeseries&);
json to_json(const api::Wallet&);
json to_json(const api::WartBalance&);
json to_json(const api::WartBalanceLookup&);

json to_json(const OffenseEntry& e);
json to_json(const SignedSnapshot&);
json to_json(const TransactionId&);
json to_json(const AssetBasic&);
json to_json(const chainserver::TransactionIds&);
json to_json(const api::Round16Bit&);
json to_json(const api::Rollback&);
json to_json(const api::IPCounter& ipc);
json to_json(const api::NodeInfo& info);
json to_json(const api::AssetLookupTrace&);
json to_json(const api::OrderDetail&);

json to_json(const api::Candle&);
json to_json(const api::Trade&);
json to_json(const api::CandlesVector&);
json to_json(const api::TradesVector&);

json tx_to_json(const api::block::Reward&);
json tx_to_json(const api::block::WartTransfer&);
json tx_to_json(const api::block::TokenTransfer&);
json tx_to_json(const api::block::AssetCreation&);
json tx_to_json(const api::block::NewOrder&);
json tx_to_json(const api::block::Match&);
json tx_to_json(const api::block::LiquidityDeposit&);
json tx_to_json(const api::block::LiquidityWithdrawal&);
json tx_to_json(const api::block::TransactionCancelation&);
json tx_to_json(const api::block::OrderCancelation& tx);

inline json to_json(const json& j) { return j; }

template <typename T>
inline json to_json(const std::vector<T>& e, const auto& map)
{
    json j = json::array();
    for (auto& item : e) {
        j.push_back(to_json(map(item)));
    }
    return j;
}

template <typename T>
inline json to_json(const wrt::optional<T>& v)
{
    if (v) {
        return to_json(*v);
    } else {
        return nullptr;
    }
}

template <typename T>
inline json to_json(const std::vector<T>& e)
{
    return to_json(e, std::identity());
}

inline std::string status(Error e)
{
    nlohmann::json j;
    j["code"] = e.code;
    if (e.is_error()) {
        j["error"] = e.strerror();
    } else {
        j["error"] = nullptr;
    }
    return j.dump(1);
}

inline std::string status(const wrt::optional<Error>& e)
{
    if (e.has_value()) {
        return status(*e);
    } else {
        return status(Error::none);
    }
}

inline std::string serialize(const wrt::optional<Error>& e)
{
    if (e.has_value()) {
        return status(*e);
    } else {
        return status(Error::none);
    }
}

template <typename T>
inline json success_json(T&& t)
{
    return {
        { "code", 0 },
        { "data", std::move(t) }
    };
}

inline std::string serialize(const Result<json>& e)
{
    if (!e.has_value())
        return status(e.error());
    return success_json(*e).dump(1);
}

template <typename T>
std::string serialize(const Result<T>& e)
{
    if (!e.has_value())
        return status(e.error());
    return success_json(to_json(*e)).dump(1);
}

inline std::string serialize(const tl::unexpected<Error> e)
{
    return status(e.value());
}
std::string serialize(const api::Raw& r);

template <typename T>
inline std::string serialize(T&& e)
{
    return json {
        { "code", 0 },
        { "data", to_json(std::forward<T>(e)) }
    }.dump(1);
}

// inline std::string serialize(const std::vector<PeerDB::BanEntry>& banned)
// {
//     nlohmann::json j = nlohmann::json::array();
//     for (auto& item : banned) {
//         nlohmann::json elem;
//         elem["ip"] = item.ip.to_string().c_str();
//         elem["expires"] = item.banuntil;
//         elem["reason"] = item.offense.err_name();
//         j.push_back(elem);
//     }
//     return j.dump(1);
// }

// std::string serialize(const std::vector<api::Peerinfo>& banned);

std::string endpoints(const Eventloop&);
// std::string connect_timers(const Eventloop&);
std::string header_download(const Eventloop&);
}
