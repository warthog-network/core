#pragma once
#include "api/events/subscription.hpp"
#include "api/interface.hpp"
#include "general/errors.hpp"
#include "glaze/glaze.hpp"
#include "nlohmann/json.hpp"
struct Head;
class Hash;
class TxHash;
class Header;
namespace jsonmsg {
using namespace nlohmann;

json to_json(Wart);

json to_json(const FundsDecimal&, bool printDecimals = true);
json to_json(const Grid&);
json to_json(const Hash&);
json to_json(const PeerDB::BanEntry&);
json to_json(const Peeraddr&);
json to_json(const TCPPeeraddr&);
json to_json(const TxHash&);
json to_json(const api::Account&);
json to_json(const api::AccountHistory&);
json to_json(const api::AddressCount&);
json to_json(const AssetDetail& a);
json to_json(const api::AssetSearchResult&);
json to_json(const api::Block&);
json to_json(const api::BlockBinary&);
json to_json(const api::ChainHead&);
json to_json(const api::FundsBalance&);
json to_json(const api::HashrateBlockChart&);
json to_json(const api::HashrateInfo&);
json to_json(const api::HashrateTimeChart&);
json to_json(const api::Head&);
json to_json(const api::HeaderInfo&);
json to_json(const api::JanushashNumber&);
json to_json(const api::LiquidityPool&, TokenDecimals baseDecimals, bool prec = true);
json to_json(const api::MempoolEntries&);
json to_json(const api::MempoolUpdate&);
json to_json(const api::MiningState&);
json to_json(const api::MarketOrders&);
json to_json(const api::MarketDetail&);
json to_json(const api::ParsedPrice&);
json to_json(const api::Peerinfo&);
json to_json(const api::PeerinfoConnections&);
json to_json(const api::Richlist&, TokenDecimals p);
json to_json(const api::RichlistInfo&);
json to_json(const api::ThrottleState&);
json to_json(const api::ThrottledPeer&);
json to_json(const api::Token&);
json to_json(const api::TokenBalanceLookup&);
json to_json(const api::TransactionDetails&);
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

json to_json(const api::Candle&);
json to_json(const api::Trade&);
json to_json(const api::CandlesVector&);
json to_json(const api::TradesVector&);
json to_json(const api::block::TransactionSignedData&);
json to_json(const api::block::RewardData&);
json to_json(const api::block::WartTransferData&);
json to_json(const api::block::TokenTransferData&);
json to_json(const api::block::AssetCreationData&);
json to_json(const api::block::NewOrderData& tx);
json to_json(const api::block::MatchData&);
json to_json(const api::block::LiquidityDepositData&);
json to_json(const api::block::LiquidityWithdrawalData&);
json to_json(const api::block::CancelationData&);
json to_json(const api::block::OrderCancelationData& tx);
json to_json(const api::TransactionMinedData&);
inline json to_json(const Height& h) { return h.value(); }
inline json to_json(json j) { return j; }

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
json to_json(const api::block::IsTransaction<T>& tx)
{
    return {
        { "hash", serialize_hex(tx.hash) },
        { "data", to_json(tx.data) }
    };
}

template <typename TxType>
json to_json(const api::Mined<TxType>& tx)
{
    return {
        { "mined", to_json(tx.mined) },
        { "confirmations", tx.confirmations },
        { "transaction", to_json(tx.transaction) }
    };
}

template <typename TxType>
json to_json(const api::MaybeMined<TxType>& tx)
{
    return {
        { "mined", to_json(tx.mined) },
        { "confirmations", tx.confirmations },
        { "transaction", to_json(tx.transaction) }
    };
}

template <typename T>
json to_json(const api::block::IsSignedTransaction<T>& tx)
{
    auto j(to_json(*static_cast<const api::block::IsTransaction<T>*>(&tx)));
    j["signingData"] = to_json(tx.signedData);
    return j;
}

template <typename T>
json to_json(const api::block::WithHistoryId<T>& wh)
{
    return { { "transaction", to_json(wh.transaction) },
        { "historyId", wh.historyId.value() } };
}

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
inline json to_json(const std::vector<T>& e)
{
    return to_json(e, std::identity());
}

inline std::string serialize_error(const Error& e)
{
    struct ErrorStruct {
        int code;
        std::optional<std::string> error;
    };
    ErrorStruct out {
        e.code,
        e.is_error() ? std::optional<std::string> { e.strerror() } : std::nullopt
    };
    return glz::write_json(out).value();
}
// inline std::string serialize_error(Error e)
// {
//     nlohmann::json j;
//     j["code"] = e.code;
//     if (e.is_error()) {
//         j["error"] = e.strerror();
//     } else {
//         j["error"] = nullptr;
//     }
//     return j.dump(1);
// }

template <typename T>
inline json success_json(T&& t)
{
    return { { "code", 0 }, { "data", std::move(t) } };
}

inline json to_json(Error e)
{
    if (e.is_error()) {
        return { { "code", e.code }, { "error", e.strerror() } };
    } else {
        return { { "code", e.code }, { "data", nullptr } };
    }
}

template <typename T>
std::string serialize(const Result<T>& e)
{
    if (!e.has_value())
        return serialize_error(e.error());
    if constexpr (std::is_same_v<T,void>) {
        return success_json(nullptr).dump(1);
    }else{
        return success_json(to_json(*e)).dump(1);
    }
}

template <typename T>
inline std::string serialize(T&& e)
{
    return json {
        { "code", 0 },
        { "data", to_json(std::forward<T>(e)) }
    }.dump(1);
}

std::string endpoints(const Eventloop&);
std::string header_download(const Eventloop&);
}
