#pragma once
#include "block/body/labels.hpp"
#include "general/base_elements_fwd.hpp"
#include "general/structured_reader_fwd.hpp"
namespace block {
namespace body {

template <typename... Ts>
struct Combined;
template <typename T>
struct HookMerkle;
template <typename... Ts>
struct SignedCombined;
template <StaticString tag, typename... Ts>
using TaggedSignedCombined = Tag<tag, SignedCombined<Ts...>>;

using Reward = HookMerkle<Combined<ToAccIdEl, WartEl>>;
struct WartTransfer;
struct AssetTransfer;
struct LiquidityTransfer;
struct AssetCreation;
struct Order;
struct Cancelation;
struct LiquidityDeposit;
struct LiquidityWithdrawal;
}
}
