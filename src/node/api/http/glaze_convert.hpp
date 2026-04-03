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
TransactionDetails to_json(const api::TransactionDetails&);
TransactionSignedCommon from(const api::block::TransactionSignedData&);
CompactFee from(::CompactUInt);
TransactionMinfee from(const api::TransactionMinfee&);
ActionsByBlock from(const api::TransactionsByBlocks&);

// for transactions which do not differ between mined/unmined
reward::Transaction from(const block::Reward&);
wart_transfer::Transaction from(const block::WartTransfer&);
token_transfer::Transaction from(const block::TokenTransfer&);
match::Transaction from(const block::Match&);
TransmissionCharts::Element from(const rxtx::RangeAggregated&);
TransmissionCharts from(const api::TransmissionTimeseries&);
Wallet from(const api::Wallet& w);
WartBalanceResult from(const api::WartBalanceLookup&);
OffenseEntry from(const api::OffenseEntry&);
RoundedFeeResult from(const api::Round16Bit&);
RollbackResult from(const api::Rollback&);
std::vector<std::pair<std::string, size_t>> from(const api::IPCounter&);
NodeInfo from(const api::NodeInfo&);
Candle from(const api::Candle&);
Trade from(const api::Trade&);

template <typename T>
auto from(const ReversibleVector<T> v)
{
    using target_type = std::remove_cvref_t<decltype(from(std::declval<const T&>()))>;
    std::vector<target_type> out;
    v.foreach ([&](const T& t) { out.push_back(from(t)); });
    return out;
}

template <typename T>
auto from(const api::block::WithHistoryId<T>& tx);
template <typename T>
using target_type = std::remove_cvref_t<decltype(from(std::declval<const T&>()))>;

}

}
