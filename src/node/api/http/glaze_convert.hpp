#pragma once
// #include "api/events/subscription.hpp"
// #include "api/interface.hpp"
#include "api/types/all.hpp"
#include "block/header/batch.hpp"
#include "glaze_types.hpp"
#include "peerserver/db/peer_db.hpp"
namespace api {
namespace glaze {

Grid from(const ::Grid&);
HashResult from(const ::Hash&);

std::string from(const Address&);
BanEntry from(const ::PeerDB::BanEntry&);
Account from(const ::api::Account&);
AssetBasic from(const ::AssetBasic&);
ThrottledPeer from(const ::api::ThrottledPeer&);
Token from(const ::api::Token&);
TokenBalanceLookup from(const ::api::TokenBalanceLookup&);
AssetDetail from(const ::AssetDetail&);
uint64_t from(const IsUint64& i);
uint32_t from(const IsUint32& i);
uint32_t from(const TokenDecimals& d);
TransactionId from(const ::TransactionId);
AssetLookupTrace from(const ::api::AssetLookupTrace& a);
TransactionDetails from(const api::TransactionDetails&);
TransactionSignedCommon from(const api::block::TransactionSignedData&);
CompactFee from(::CompactUInt);
TransactionMinfeeResult from(const api::TransactionMinfee&);
ActionsByBlock from(const api::TransactionsByBlocks&);

// for transactions which do not differ between mined/unmined
reward::Transaction from(const block::Reward&);
wart_transfer::Transaction from(const block::WartTransfer&);
token_transfer::Transaction from(const block::TokenTransfer&);
match::Transaction from(const block::Match&);
TransmissionChartsResult::Element from(const rxtx::RangeAggregated&);
TransmissionChartsResult from(const api::TransmissionTimeseries&);
Wallet from(const api::Wallet& w);
WartBalanceResult from(const api::WartBalanceLookup&);
OffenseEntry from(const api::OffenseEntry&);
RoundedFeeResult from(const api::Round16Bit&);
RollbackResult from(const api::Rollback&);
std::vector<std::pair<std::string, size_t>> from(const api::IPCounter&);
NodeInfoResult from(const api::NodeInfo&);
Candle from(const api::Candle&);
Trade from(const api::Trade&);
Error from(::Error e);

ChainHead from(const api::ChainHead&);
ChainHeadSynced from(const api::Head& h);
MempoolUpdateResult from(const api::MempoolUpdate& u);
MempoolEntry from(const api::MempoolEntry& e);
MempoolEntries from(const ::api::MempoolEntries&);
HeaderResult from(const ::api::HeaderInfo&);

BlockBinaryAnnotation from(const ParseAnnotation&);
BlockBinaryResult from(const api::BlockBinary&);
Block from(const api::Block&);
MiningState from(const api::MiningState&);
std::vector<TransactionId> from(const chainserver::TransactionIds&);
HashrateInfo from(const api::HashrateInfo&);
AssetSearchResult from(const api::AssetSearchResult&);
MarketDetail from(const api::MarketDetail&);
MarketOrders from(const api::MarketOrders&);
AccountHistory from(const api::AccountHistory&);
RichlistResult from(const api::RichlistInfo&);
OffenseEntry from(const api::OffenseEntry&);
Peerinfo from(const api::Peerinfo&);
std::string from(const IP&);
ThrottleState from(const api::ThrottleState&);
HashrateBlockChart from(const api::HashrateBlockChart&);
HashrateTimeChart from(const api::HashrateTimeChart&);
ParsedPrice from(const api::ParsedPrice&);
double from(const api::JanushashNumber&);
std::string from(const TCPPeeraddr&);
SignedSnapshot from(const ::SignedSnapshot&);
BlockActions from(const api::block::Actions&);
std::vector<PeerinfoConnection> from(const api::PeerinfoConnections&);

template <typename T>
auto from(const api::block::WithHistoryId<T>& tx);

template <typename T>
auto from(const ReversibleVector<T> v);

template <typename T>
auto from(const std::vector<T>& v);

template <typename T>
auto from(const std::vector<T>& v)
{
    using target_type = std::remove_cvref_t<decltype(from(std::declval<const T&>()))>;
    std::vector<target_type> out;
    for (auto& e : v) {
        out.push_back(from(e));
    }
    return out;
}

template <typename T>
auto from(const ReversibleVector<T> v)
{
    using target_type = std::remove_cvref_t<decltype(from(std::declval<const T&>()))>;
    std::vector<target_type> out;
    v.foreach ([&](const T& t) { out.push_back(from(t)); });
    return out;
}

template <typename T>
auto from(const wrt::optional<T>& o)
{
    using target_type = std::remove_cvref_t<decltype(from(std::declval<const T&>()))>;
    std::optional<target_type> out;
    if (o)
        out = from(*o);
    return out;
}

template <typename T>
auto from(const ::Result<T>& res)
{
    if constexpr (std::is_same_v<T, void>) {
        using out_type = Result<void>;
        if (res.has_value()) {
            return out_type { Success<void> { .code = 0 } };
        } else {
            return out_type { from(res.error()) };
        }
    } else {
        using target_type = std::remove_cvref_t<decltype(from(std::declval<const T&>()))>;
        using out_type = Result<target_type>;
        if (res.has_value()) {

            return out_type { Success<target_type> { .code = 0, .data = from(res.value()) } };
        } else {
            return out_type { from(res.error()) };
        }
    }
}

// auto from(const ::Result<void>& res){
//
// }

// template<typename ...Ts>
// auto from(const wrt::variant<Ts...>& var){
//     using out_type = std::variant<std::remove_cvref_t<decltype(from(std::declval<const Ts&>()))>...>;
//     if (res.has_value()) {
//         return out_type{Success<target_type>{.code = 0, .data = from(res.value())}};
//     }else{
//         return out_type{from(res.error())};
//     }
// }

template <typename T>
concept convertible = requires {
    api::glaze::from(std::declval<T>());
};
}

}
