#pragma once
#include "api/types/shared_fwd.hpp"
#include "chainserver/markethistory/api_types_fwd.hpp"
#include "wrt/variant_fwd.hpp"
namespace api {
struct AccountHistory;
struct AccountIdOrAddress;
struct AddressCount;
struct Account;
struct AssetLookupTrace;
struct Block;
struct BlockBinary;
struct AssetSearchArgs;
struct AssetSearchResult;
struct ChainHead;
struct CompleteBlock;
struct HashrateBlockChart;
struct HashrateChartRequest;
struct HashrateInfo;
struct HashrateTimeChart;
struct Head;
struct HeaderInfo;
struct HeightOrHash;
struct MempoolEntries;
struct MempoolUpdate;
struct MiningState;
struct Peerinfo;
struct ParsedPrice;
struct PeerinfoConnections;
struct Raw;
struct TokenBalanceLookup;
struct WartBalance;
struct LiquidityPool;
struct JanushashNumber;
template <typename Transaction>
struct MaybeMined;
template <typename Transaction>
struct Mined;

using MinedReward = Mined<block::Reward>;
using MaybeMinedWartTransfer = MaybeMined<block::WartTransfer>;
using MaybeMinedTokenTransfer = MaybeMined<block::TokenTransfer>;
using MaybeMinedAssetCreation = MaybeMined<block::AssetCreation>;
using MaybeMinedNewOrder = MaybeMined<block::NewOrder>;
using MinedMatch = Mined<block::Match>;
using MaybeMinedLiquidityDeposit = MaybeMined<block::LiquidityDeposit>;
using MaybeMinedLiquidityWithdrawal = MaybeMined<block::LiquidityWithdrawal>;
using MaybeMinedCancelation = MaybeMined<block::TransactionCancelation>;
using MaybeMinedOrderCancelation = MaybeMined<block::OrderCancelation>;

// this is returned for transaction lookup
using TransactionDetails = wrt::variant<
    MinedReward,
    MaybeMinedWartTransfer,
    MaybeMinedTokenTransfer,
    MaybeMinedAssetCreation,
    MaybeMinedNewOrder,
    MinedMatch,// <-- this is always mined, can only come from block, not from mempool
    MaybeMinedLiquidityDeposit,
    MaybeMinedLiquidityWithdrawal,
    MaybeMinedCancelation,
    MaybeMinedOrderCancelation>;

struct Richlist;
struct RichlistInfo;
struct Rollback;
struct Round16Bit;
struct TransactionsByBlocks;
struct TransactionMinfee;
struct Token;
struct Wallet;
struct DBSize;
struct NodeInfo;
struct IPCounter;
struct ThrottleState;
struct ThrottledPeer;
}
