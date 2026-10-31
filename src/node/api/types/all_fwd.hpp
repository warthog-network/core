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
struct HistoryIdOrHash;
struct MempoolEntries;
struct MempoolUpdate;
struct MiningState;
struct Peerinfo;
struct ParsedPrice;
struct PeerinfoConnections;
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
using MaybeMinedLimitSwap = MaybeMined<block::LimitSwap>;
using MinedMatch = Mined<block::Match>;
using MaybeMinedLiquidityDeposit = MaybeMined<block::LiquidityDeposit>;
using MaybeMinedLiquidityWithdrawal = MaybeMined<block::LiquidityWithdrawal>;
using MaybeMinedCancelation = MaybeMined<block::Cancelation>;

// this is returned for transaction lookup
using TransactionDetails = wrt::variant<
    MinedReward, // <-- always mined, not in mempool
    MaybeMinedWartTransfer,
    MaybeMinedTokenTransfer,
    MaybeMinedAssetCreation,
    MaybeMinedLimitSwap,
    MinedMatch, // <-- always mined, not in mempool
    MaybeMinedLiquidityDeposit,
    MaybeMinedLiquidityWithdrawal,
    MaybeMinedCancelation>;

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
struct NodeVersionPlaceholder{}; // Type to be populated with real version info in api::glaze::NodeVersion
struct IPCounter;
struct ThrottleState;
struct ThrottledPeer;
struct TCPConnectionSchedule;
struct WSConnectionSchedule;
using ConnectionSchedule = wrt::variant<TCPConnectionSchedule, WSConnectionSchedule>;
}
