#pragma once

#include "accountid_or_address.hpp"
#include "api/types/input.hpp"
#include "asset_lookup_trace.hpp"
#include "block/body/labels.hpp"
#include "block/chain/history/index.hpp"
#include "block/chain/worksum.hpp"
#include "block/header/difficulty_declaration.hpp"
#include "block/header/header.hpp"
#include "communication/mining_task.hpp"
#include "crypto/address.hpp"
#include "defi/order_id.hpp"
#include "defi/token/asset.hpp"
#include "defi/types.hpp"
#include "defi/uint64/pool.hpp"
#include "defi/uint64/price.hpp"
#include "shared_fwd.hpp"
// #include "forward_declarations.hpp"
#include "general/funds.hpp"
#include "height_or_hash.hpp"
#include "transport/helpers/ip.hpp"
// #include "peerserver/db/offense_entry.hpp"
#include <chrono>
#include <cstdlib>
#include <vector>
namespace chainserver {
class DBCache;
}

namespace api {
using sc = std::chrono::steady_clock;
struct ChainHead {
    Worksum worksum;
    Target nextTarget;
    Hash hash;
    Height height;
    PinHash pinHash;
    PinHeight pinHeight;
    uint64_t hashrate;
};
struct HeaderInfo {
    NonzeroHeight height;
    Header header;
};

struct MiningState {
    ChainMiningTask miningTask;
    bool synced;
};

struct Head {
    ChainHead chainHead;
    bool synced;
};

struct Account {
    Address address;
    AccountId id;
};

struct WartBalance {
    Wart total { Wart::zero() };
    Wart locked { Wart::zero() };
    Wart free() const { return diff_assert(total, locked); }
};
struct WartBalanceLookup {
    wrt::optional<Account> account;
    WartBalance balance;
};

struct JanushashNumber {
    double d;
};

struct Rollback {
    static constexpr const char eventName[] = "rollback";
    Height length;
};

struct BlockSummary {
    Header header;
    NonzeroHeight height;
    uint32_t confirmations = 0;
    uint32_t nTransfers;
    Address miner;
    Wart transferred;
    Wart totalTxFee;
    Wart blockReward;
};
namespace block {
struct HistoryDataBase {
    TxHash txhash;
    wrt::optional<HistoryId> hid;
};

template <typename T>
struct WithHistoryBase : public HistoryDataBase, T {
    WithHistoryBase(TxHash txhash, HistoryId hid, T t)
        : HistoryDataBase(std::move(txhash), std::move(hid))
        , T(std::move(t))
    {
    }
};

struct SignedInfoData : public HistoryDataBase {
    SignedInfoData(TxHash txHash, wrt::optional<HistoryId> hid, AccountId originId, Address originAddress, Wart fee, NonceId nonceId, PinHeight pinHeight)
        : HistoryDataBase(std::move(txHash), std::move(hid))
        , originId(std::move(originId))
        , originAddress(std::move(originAddress))
        , fee(std::move(fee))
        , nonceId(std::move(nonceId))
        , pinHeight(std::move(pinHeight))
    {
    }
    AccountId originId;
    Address originAddress;
    Wart fee;
    NonceId nonceId;
    PinHeight pinHeight;
};

template <typename T>
struct WithSignedInfo : public SignedInfoData, T {
    WithSignedInfo(SignedInfoData i, T t)
        : SignedInfoData(std::move(i))
        , T(std::move(t))
    {
    }
};

struct RewardData {
    static constexpr const char* label = ::block::labels::reward;
    Address toAddress;
    Wart wart;
};

struct WartTransferData {
    static constexpr const char* label = ::block::labels::wartTransfer;
    Address toAddress;
    Wart amount;
};

struct TokenTransferData {
    static constexpr const char* label = ::block::labels::tokenTransfer;
    AssetBasic assetInfo;
    bool isLiquidity;
    Address toAddress;
    Funds_uint64 amount;
    FundsDecimal amount_decimal() const { return { amount, isLiquidity ? TokenPrecision::LIQUIDITY : assetInfo.precision }; }
};

struct NewOrderData {
    static constexpr const char* label = ::block::labels::limitSwap;
    AssetBasic assetInfo;
    Funds_uint64 amount;
    Price_uint64 limit;
    bool buy;

    FundsDecimal amount_decimal() const { return { amount, buy ? assetInfo.precision : Wart::precision }; }
};

struct MatchData {
    static constexpr const char* label = ::block::labels::match;
    using Swap = CombineElements<BaseEl, QuoteEl, ReferredHistoryIdEl>;
    AssetBasic assetInfo;
    defi::BaseQuote poolBefore;
    defi::BaseQuote poolAfter;
    std::vector<Swap> buySwaps;
    std::vector<Swap> sellSwaps;
};

struct AssetCreationData {
    static constexpr const char* label = ::block::labels::assetCreation;
    AssetName name;
    FundsDecimal supply;
    wrt::optional<AssetId> assetId;
};

struct CancelationData {
    static constexpr const char* label = ::block::labels::cancelation;
    TransactionId cancelTxid;
};

struct OrderCancelationData {
    static constexpr const char* label = ::block::labels::orderCancelation;
    TransactionId cancelTxid;
    bool buy;
    AssetBasic assetInfo;
    HistoryId historyId;
    Funds_uint64 remaining;
};

struct LiquidityDepositData {
    static constexpr const char* label = ::block::labels::liquidityDeposit;
    AssetBasic assetInfo;
    Funds_uint64 baseDeposited;
    Wart quoteDeposited;
    wrt::optional<Funds_uint64> sharesReceived;
};

struct LiquidityWithdrawalData {
    static constexpr const char* label = ::block::labels::liquidityWithdrawal;
    AssetBasic assetInfo;
    Funds_uint64 sharesRedeemed;
    wrt::optional<Funds_uint64> baseReceived;
    wrt::optional<Wart> quoteReceived;
};

struct Actions {
    wrt::optional<block::Reward> reward;
    std::vector<block::WartTransfer> wartTransfers;
    std::vector<block::TokenTransfer> tokenTransfers;
    std::vector<block::AssetCreation> assetCreations;
    std::vector<block::NewOrder> newOrders;
    std::vector<block::Match> matches;
    std::vector<block::LiquidityDeposit> liquidityDeposit;
    std::vector<block::LiquidityWithdrawal> liquidityWithdrawal;
    std::vector<block::TransactionCancelation> cancelations;
    std::vector<block::OrderCancelation> orderCancelations;
};
}

struct Block {
    static constexpr const char eventName[] = "blockAppend";
    Header header;
    NonzeroHeight height;
    uint32_t confirmations = 0;

public:
    block::Actions actions;

    Block(Header header,
        NonzeroHeight height, uint32_t confirmations,
        block::Actions actions)
        : header(header)
        , height(height)
        , confirmations(confirmations)
        , actions(std::move(actions))
    {
    }
    void set_reward(block::Reward r);
};

struct BlockBinary {
    std::vector<uint8_t> data;
    ParseAnnotations annotations;
};
struct AssetSearchArgs {
    std::string namePrefix;
    std::string hashPrefix;
};

struct Order {
    bool fromMempool; // true for mempool orders, false for chain orders
    // TxHash hash;
    Price_uint64 limit;
    Funds_uint64 amount;
    Funds_uint64 filled;
};
struct Orders {
    Orders(TokenPrecision basePrec):basePrec(basePrec){};
    TokenPrecision basePrec;
    std::vector<Order> buys;
    std::vector<Order> sells;
};

struct AssetSearchResult {
    AssetSearchResult(AssetSearchArgs args)
        : args(std::move(args))
    {
    }
    struct Entry {
        std::string name;
        AssetHash hash;
        NonzeroHeight height;
        TokenPrecision precision;
    };
    std::vector<Entry> entries;
    AssetSearchArgs args;
};

struct CompleteBlock : public Block {
    explicit CompleteBlock(Block b)
        : Block(std::move(b))
    {
        if (!actions.reward)
            throw std::runtime_error("API Block is incomplete.");
    }
    auto& reward() const { return *actions.reward; }
};

struct TemporalInfo {
    uint32_t confirmations;
    Height height { 0 };
    uint32_t timestamp = 0;
};

template <typename TxType>
struct Temporal : public TemporalInfo, public TxType {
};

struct AddressCount {
    Address address;
    int64_t count;
};

struct AccountHistory {
    Wart balance;
    Wart locked;
    HistoryId fromId;
    std::vector<api::Block> blocks_reversed;
};
struct TransactionsByBlocks {
    size_t count { 0 };
    HistoryId fromId;
    std::vector<api::Block> blocks_reversed;
};
struct TransactionMinfee {
    CompactUInt minfee;
};

struct Token {
    TokenId id;
    api::TokenSpec spec;
    std::string name;
    TokenPrecision assetPrecision;
    TokenPrecision token_precision() const
    {
        return spec.isLiquidity ? TokenPrecision::LIQUIDITY : assetPrecision;
    }
    static constexpr Token WART()
    {
        return {
            .id { TokenId::WART },
            .spec { api::TokenSpec::WART },
            .name { "WART" },
            .assetPrecision { TokenPrecision::WART }
        };
    }
};

struct FundsBalance {
    FundsDecimal total;
    FundsDecimal locked;
    FundsDecimal free() const { return FundsDecimal(diff_assert(total.funds, locked.funds), total.precision); }
    static FundsBalance zero()
    {
        return { FundsDecimal::zero(), FundsDecimal::zero() };
    }
};

struct TokenBalanceLookup {
    api::Token token;
    FundsBalance balance;
    wrt::optional<api::Account> account;
    wrt::optional<AssetLookupTrace> lookupTrace;
};

struct Richlist {
    std::vector<std::pair<Address, Funds_uint64>> entries;
};

struct RichlistInfo {
    Richlist richlist;
    Token token;
};

struct HashrateInfo {
    size_t nBlocks;
    uint64_t estimate;
};

struct HashrateChartRequest {
    Height begin;
    Height end;
};

struct HashrateBlockChart {
    HashrateChartRequest range;
    std::vector<double> chart;
};

struct HashrateTimeChart {
    uint32_t begin;
    uint32_t end;
    uint32_t interval;
    std::vector<std::tuple<uint32_t, Height, uint64_t>> chartReversed;
};

struct MempoolUpdate {
    size_t deletedTransactions;
};

struct ParsedPrice {
    TokenPrecision prec;
    Price_uint64 floor;
    Price_uint64 ceil;
};

struct Network {
    /* data */
};

struct Round16Bit {
    Wart original;
};

struct Wallet {
    PrivKey pk;
};

struct Raw {
    std::string s;
};

struct IPCounter {
    std::vector<std::pair<IP, size_t>> vector;
};

struct DBSize {
    size_t dbSize;
};

struct NodeInfo : public DBSize {
};
}
