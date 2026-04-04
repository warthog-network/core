#pragma once
namespace api {
namespace block {

struct RewardData;
struct WartTransferData;
struct TokenTransferData;
struct AssetCreationData;
struct NewOrderData;
struct LiquidityDepositData;
struct LiquidityWithdrawalData;
struct CancelationData;
struct OrderCancelationData;
struct MatchData;

template <typename T>
struct IsTransaction;
template <typename T>
struct IsSignedTransaction;
template <typename T>
struct WithHistoryId;

using Reward = IsTransaction<RewardData>;
using WartTransfer = IsSignedTransaction<WartTransferData>;
using TokenTransfer = IsSignedTransaction<TokenTransferData>;
using AssetCreation = IsSignedTransaction<AssetCreationData>;
using NewOrder = IsSignedTransaction<NewOrderData>;
using LiquidityDeposit = IsSignedTransaction<LiquidityDepositData>;
using LiquidityWithdrawal = IsSignedTransaction<LiquidityWithdrawalData>;
using Cancelation = IsSignedTransaction<CancelationData>;
using Match = IsTransaction<MatchData>;

}
}
