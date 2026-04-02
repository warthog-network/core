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
Wart from(const ::Wart);
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

asset_creation::TransactionProcessed from_mined(const block::AssetCreation&);
new_order::TransactionProcessed from_mined(const block::NewOrder&);
liquidity_deposit::TransactionProcessed from_mined(const block::LiquidityDeposit&);
liquidity_withdrawal::TransactionProcessed from_mined(const api::block::WithHistoryId<block::LiquidityWithdrawal>&);
cancelation::TransactionProcessed from_mined(const block::TransactionCancelation);


template<typename T>
auto from(const api::block::WithHistoryId<T>& tx);
template <typename T>
using target_type = std::remove_cvref_t<decltype(from(std::declval<const T&>()))>;

}

}
