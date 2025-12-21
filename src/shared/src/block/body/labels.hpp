#pragma once
namespace block {
namespace labels {
#define LABEL_TX(VAR, LABEL) inline constexpr const char VAR[] = LABEL;
LABEL_TX(reward, "reward")
LABEL_TX(wartTransfer, "wartTransfer")
LABEL_TX(tokenTransfer, "tokenTransfer")
LABEL_TX(assetTransfer, "assetTransfer")
LABEL_TX(liquidityTransfer, "liquidityTransfer")
LABEL_TX(limitSwap, "limitSwap")
LABEL_TX(match, "match")
LABEL_TX(assetCreation, "assetCreation")
LABEL_TX(cancelation, "cancelation")
LABEL_TX(orderCancelation, "orderCancelation")
LABEL_TX(liquidityDeposit, "liquidityDeposit")
LABEL_TX(liquidityWithdrawal, "liquidityWithdrawal")
#undef LABEL_TX

}
}
