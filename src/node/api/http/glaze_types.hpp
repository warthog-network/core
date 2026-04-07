#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace api {
namespace glaze {
struct Timepoint {
    std::string UTC;
    uint32_t timestamp;
    struct glaze {
        static constexpr const char* name = "Timepoint";
    };
};
struct FundsDecimalNoDecimals {
    std::string str;
    uint64_t u64;
    struct glaze {
        static constexpr const char* name = "FundsDecimalNoDecimals";
    };
};
struct FundsDecimal {
    std::string str;
    uint64_t u64;
    int decimals;
    struct glaze {
        static constexpr const char* name = "FundsDecimal";
    };
};

struct Grid {
    std::vector<std::string> headers;
    struct glaze {
        static constexpr const char* name = "HeaderGrid";
    };
};
struct HashResult {
    std::string hash;
    struct glaze {
        static constexpr const char* name = "HashResult";
    };
    // struct glaze {
    //     using mimic = std::string;
    //     static constexpr auto value = &HashResult::hash;
    // };
};
struct BanEntry {
    std::string ip;
    uint32_t banuntil;
    std::string reason;
    struct glaze {
        static constexpr const char* name = "BanEntry";
    };
};

struct Account {
    std::string address;
    uint64_t accountId;
    struct glaze {
        static constexpr const char* name = "Account";
    };
};
struct Wart {
    std::string str;
    uint64_t E8;
    struct glaze {
        static constexpr const char* name = "Wart";
    };
};

struct TransactionSignedCommon {
    uint64_t originId;
    std::string originAddress;
    Wart fee;
    uint32_t nonceId;
    uint32_t pinHeight;
    // std::string signature;
    struct glaze {
        static constexpr const char* name = "TransactionSignedCommon";
    };
};

template <typename Data>
struct Transaction {
    Data data;
    std::string hash;
};
template <typename Data>
struct SignedTransaction {
    Data data;
    std::string hash;
    TransactionSignedCommon signedCommon;
};
template <typename Data, typename Processed>
struct SignedTransactionProcessed {
    Data data;
    Processed processed;
    std::string hash;
    TransactionSignedCommon signedCommon;
};
template <typename Data, typename Processed>
struct SignedTransactionMaybeProcessed {
    Data data;
    std::optional<Processed> processed;
    std::string hash;
    TransactionSignedCommon signedCommon;
};
template <typename Transaction>
struct WithHistoryId {
    Transaction transaction;
    uint64_t historyId;
};

struct PriceDetail {
    int precExponent10;
    int exponent2;
    int mantissa;
    std::string hex;
    double doubleAdjusted;
    double doubleRaw;
    struct glaze {
        static constexpr const char* name = "PriceDetail";
    };
};
struct BaseQuote {
    FundsDecimal base;
    Wart quote;
    struct glaze {
        static constexpr const char* name = "BaseQuote";
    };
};
struct AssetBasic {
    std::string hash;
    uint64_t id;
    std::string name;
    uint32_t decimals;
    struct glaze {
        static constexpr const char* name = "AssetBasic";
    };
};
struct AssetDetail {
    std::string hash;
    uint64_t id;
    std::string name;
    uint32_t decimals;
    uint32_t height;
    uint64_t ownerAccountId;
    FundsDecimal totalSupply;
    uint64_t groupId;
    std::optional<uint64_t> parentId;
    struct glaze {
        static constexpr const char* name = "AssetDetail";
    };
};

struct TransactionId {
    uint64_t accountId;
    uint32_t nonceId;
    uint32_t pinHeight;
    struct glaze {
        static constexpr const char* name = "TransactionId";
    };
};

namespace reward {
struct Data {
    std::string toAddress;
    Wart amount;
    struct glaze {
        static constexpr const char* name = "RewardData";
    };
};
using Transaction = Transaction<Data>;
using WithHistoryId = WithHistoryId<Transaction>;
}

namespace wart_transfer {
struct Data {
    std::string toAddress;
    Wart amount;
    struct glaze {
        static constexpr const char* name = "WartTransferData";
    };
};
using Transaction = SignedTransaction<Data>;
using WithHistoryId = WithHistoryId<Transaction>;
}

namespace token_transfer {
struct Data {
    std::string toAddress;
    FundsDecimal amount;
    AssetBasic asset;
    bool isLiquidity;
    std::string tokenSpec;
    struct glaze {
        static constexpr const char* name = "TokenTransferData";
    };
};
using Transaction = SignedTransaction<Data>;
using WithHistoryId = WithHistoryId<Transaction>;
}

namespace asset_creation {
struct Data {
    std::string name;
    FundsDecimal supply;
    struct glaze {
        static constexpr const char* name = "AssetCreationData";
    };
};
struct Processed {
    uint64_t assetId;
    struct glaze {
        static constexpr const char* name = "AssetCreationProcessed";
    };
};
using TransactionUnprocessed = SignedTransaction<Data>;
using TransactionProcessed = SignedTransactionProcessed<Data, Processed>;
using TransactionMaybeProcessed = SignedTransactionMaybeProcessed<Data, Processed>;
using WithHistoryId = WithHistoryId<TransactionProcessed>;
}

namespace new_order {
struct Data {
    AssetBasic baseAsset;
    FundsDecimal amount;
    PriceDetail limit;
    bool buy;
    struct glaze {
        static constexpr const char* name = "NewOrderProcessed";
    };
};
struct Processed {
    FundsDecimal remaining;
    struct glaze {
        static constexpr const char* name = "NewOrderProcessed";
    };
};
using TransactionUnprocessed = SignedTransaction<Data>;
using TransactionProcessed = SignedTransactionProcessed<Data, Processed>;
using TransactionMaybeProcessed = SignedTransactionMaybeProcessed<Data, Processed>;
using WithHistoryId = WithHistoryId<TransactionProcessed>;
}

namespace match {
struct Data {
    struct SwapEntry {
        BaseQuote swapped;
        uint64_t historyId;
        struct glaze {
            static constexpr const char* name = "SwapEntry";
        };
    };
    AssetBasic baseAsset;
    BaseQuote poolBefore;
    BaseQuote poolAfter;
    std::vector<SwapEntry> buySwaps;
    std::vector<SwapEntry> sellSwaps;
    struct glaze {
        static constexpr const char* name = "MatchData";
    };
};
using Transaction = Transaction<Data>;
using WithHistoryId = WithHistoryId<Transaction>;
}

namespace liquidity_deposit {
struct Data {
    AssetBasic baseAsset;
    BaseQuote deposited;
    struct glaze {
        static constexpr const char* name = "LiquidityDepositData";
    };
};
struct Processed {
    FundsDecimal sharesReceived;
    struct glaze {
        static constexpr const char* name = "LiquidityDepositProcessed";
    };
};
using TransactionUnprocessed = SignedTransaction<Data>;
using TransactionProcessed = SignedTransactionProcessed<Data, Processed>;
using TransactionMaybeProcessed = SignedTransactionMaybeProcessed<Data, Processed>;
using WithHistoryId = WithHistoryId<TransactionProcessed>;
}

namespace liquidity_withdrawal {
struct Data {
    AssetBasic baseAsset;
    FundsDecimal sharesRedeemed;
    struct glaze {
        static constexpr const char* name = "LiquidityWithdrawalData";
    };
};
struct Processed {
    BaseQuote received;
    struct glaze {
        static constexpr const char* name = "LiquidityWithdrawalProcessed";
    };
};
using TransactionUnprocessed = SignedTransaction<Data>;
using TransactionProcessed = SignedTransactionProcessed<Data, Processed>;
using TransactionMaybeProcessed = SignedTransactionMaybeProcessed<Data, Processed>;
using WithHistoryId = WithHistoryId<TransactionProcessed>;
}

namespace cancelation {
struct Data {
    TransactionId cancelTxid;
    struct glaze {
        static constexpr const char* name = "CancelationData";
    };
};
struct Processed {
    std::string canceledTxHash;
    struct glaze {
        static constexpr const char* name = "CancelationProcessed";
    };
};
using TransactionUnprocessed = SignedTransaction<Data>;
using TransactionProcessed = SignedTransactionProcessed<Data, Processed>;
using TransactionMaybeProcessed = SignedTransactionProcessed<Data, Processed>;
using WithHistoryId = WithHistoryId<TransactionProcessed>;
}

struct BlockActions {
    std::optional<reward::WithHistoryId> reward;
    std::vector<wart_transfer::WithHistoryId> wartTransfers;
    std::vector<token_transfer::WithHistoryId> tokenTransfers;
    std::vector<new_order::WithHistoryId> newOrders;
    std::vector<match::WithHistoryId> matches;
    std::vector<liquidity_deposit::WithHistoryId> liquidityDeposits;
    std::vector<liquidity_withdrawal::WithHistoryId> liquidityWithdrawals;
    std::vector<asset_creation::WithHistoryId> assetCreations;
    std::vector<cancelation::WithHistoryId> cancelations;
    struct glaze {
        static constexpr const char* name = "BlockActions";
    };
};

struct MempoolEntry {
    using Transaction = std::variant<
        wart_transfer::Transaction,
        token_transfer::Transaction,
        new_order::TransactionUnprocessed,
        liquidity_deposit::TransactionUnprocessed,
        liquidity_withdrawal::TransactionUnprocessed,
        asset_creation::TransactionUnprocessed,
        cancelation::TransactionUnprocessed>;

    Transaction transaction;
    std::string tag;
    struct glaze {
        static constexpr const char* name = "MempoolEntry";
    };
};
using MempoolEntries = std::vector<MempoolEntry>;

struct ActionsByBlock {
    struct BlockEntry {
        uint32_t height;
        uint32_t confirmations;
        BlockActions actions;
    };
    std::vector<BlockEntry> perBlock;
    uint64_t fromId;
    struct glaze {
        static constexpr const char* name = "ActionsByBlock";
    };
};

struct AddressCount {
    std::string address;
    uint64_t count;
    struct glaze {
        static constexpr const char* name = "AddressCount";
    };
};
struct AssetSearchResult {
    std::vector<AssetDetail> matches;
    std::string hashPrefix;
    std::string namePrefix;
    struct glaze {
        static constexpr const char* name = "AssetSearchResult";
    };
};
struct ProofOfWorkDetail {
    bool verusV2_2;
    std::string hashVerus;
    std::string hashSha256t;
    double floatVerus;
    double floatSha256t;
    struct glaze {
        static constexpr const char* name = "ProofOfWorkDetail";
    };
};

struct BlockHeader {
    std::string raw;
    Timepoint time;
    std::string target;
    std::string hash;
    ProofOfWorkDetail pow;
    std::string merkleroot;
    std::string nonce;
    std::string prevHash;
    std::string version;
    struct glaze {
        static constexpr const char* name = "BlockHeader";
    };
};
struct HeaderResult {
    BlockHeader header;
    uint32_t height;
    struct glaze {
        static constexpr const char* name = "HeaderResult";
    };
};
struct Block {
    BlockHeader header;
    BlockActions body;
    uint32_t confirmations;
    uint32_t height;
    struct glaze {
        static constexpr const char* name = "Block";
    };
};
struct BlockBinaryAnnotation {
    std::string tag;
    size_t beginOffset;
    size_t endOffset;
    std::vector<BlockBinaryAnnotation> children;
    struct glaze {
        static constexpr const char* name = "BlockBinaryAnnotation";
    };
};

struct BlockBinaryResult {
    std::string bytes;
    std::vector<BlockBinaryAnnotation> structure;
    struct glaze {
        static constexpr const char* name = "BlockBinary";
    };
};
struct ChainHead {
    std::string hash;
    uint32_t height;
    double difficulty;
    bool is_janushash;
    uint32_t pinHeight;
    double worksum;
    std::string worksumHex;
    std::string pinHash;
    uint64_t hashrate;
    struct glaze {
        static constexpr const char* name = "ChainHead";
    };
};
struct ChainHeadSynced {
    ChainHead chainHead;
    bool synced;
    struct glaze {
        static constexpr const char* name = "ChainHeadSynced";
    };
};
struct FundsBalance {
    FundsDecimal total;
    FundsDecimal locked;
    FundsDecimal mempool;
    struct glaze {
        static constexpr const char* name = "FundsBalance";
    };
};
struct WartBalance {
    Wart total;
    Wart locked;
    Wart mempool;
    struct glaze {
        static constexpr const char* name = "WartBalance";
    };
};

struct Uint32Range {
    uint32_t begin;
    uint32_t end;
    struct glaze {
        static constexpr const char* name = "Uint32Range";
    };
};
struct HashrateBlockChart {
    Uint32Range range;
    std::vector<double> data;
    struct glaze {
        static constexpr const char* name = "HashrateBlockChart";
    };
};
struct HashrateTimeChart {
    struct Entry {
        uint32_t timestamp;
        uint32_t height;
        uint64_t hashrate;
        struct glaze {
            static constexpr const char* name = "HashrateTimeChartEntry";
        };
    };
    Uint32Range range;
    std::vector<Entry> data;
    uint32_t interval;
    struct glaze {
        static constexpr const char* name = "HashrateTimeChart";
    };
};
struct HashrateResult {
    uint64_t lastNBlocksEstimate;
    size_t N;
    struct glaze {
        static constexpr const char* name = "HashrateResult";
    };
};

struct JanushashResult {
    double janushashNumber;
    struct glaze {
        static constexpr const char* name = "JanushashResult";
    };
};

struct SignedSnapshot{
    std::string hash;
    std::string signature;
    uint32_t priorityHeight;
    uint32_t priorityImportance;
};

struct LiquidityPool {
    FundsDecimal asset;
    Wart wart;
    FundsDecimal shares;
    struct glaze {
        static constexpr const char* name = "LiquidityPool";
    };
};
struct MempoolUpdateResult {
    size_t deleted;
    struct glaze {
        static constexpr const char* name = "MempoolUpdateResult";
    };
};
struct MiningState {
    bool synced;
    std::string header;
    double difficulty;
    std::string merklePrefix;
    std::string body;
    Wart blockReward;
    uint32_t height;
    bool testnet;
    struct glaze {
        static constexpr const char* name = "MiningState";
    };
};

struct SwapOrder {
    bool inMempool;
    std::string txHash;
    PriceDetail limit;
    FundsDecimal amount;
    FundsDecimal filled;
    struct glaze {
        static constexpr const char* name = "SwapOrder";
    };
};

struct MarketOrders {
    AssetBasic baseAsset;
    std::vector<SwapOrder> wartToAssetSwaps;
    std::vector<SwapOrder> assetToWartSwaps;
    struct glaze {
        static constexpr const char* name = "MarketOrders";
    };
};
struct ToPool {
    bool isQuote;
    FundsDecimal amount;
    struct glaze {
        static constexpr const char* name = "ToPool";
    };
};

struct MarketDetail {
    AssetBasic baseAsset;
    std::vector<SwapOrder> wartToAssetSwaps;
    std::vector<SwapOrder> assetToWartSwaps;
    LiquidityPool liquidityPool;
    struct Match {
        BaseQuote filled;
        ToPool toPool;
    };
    Match match;
    struct glaze {
        static constexpr const char* name = "MarketDetails";
    };
};
struct ParsedPrice {
    uint32_t decimals;
    PriceDetail floor;
    PriceDetail ceil;
    struct glaze {
        static constexpr const char* name = "ParsedPrice";
    };
};
struct Order {
    uint32_t confirmations; // 0 means it is in mempool
    std::optional<uint32_t> height;
    std::optional<uint64_t> historyId;
    std::string txHash;
    TransactionId txid;
    PriceDetail limit;
    FundsDecimal amount;
    FundsDecimal filled;
};

struct ThrottleState {
    struct State {
        uint32_t h0;
        uint32_t h1;
        size_t window;
    };
    int delay;
    State blockRequest;
    State headerRequest;
    struct glaze {
        static constexpr const char* name = "ThrottleState";
    };
};
struct PeerinfoConnection {
    Timepoint since;
    int port;
    std::optional<std::string> ip;
};
struct Peerinfo {
    struct LeaderPriority {
        struct Priority {
            int importance;
            uint32_t hegiht;
        };
        Priority ack;
        Priority theirs;
    };
    struct Chain {
        uint32_t length;
        uint32_t forkLower;
        uint32_t forkUpper;
        uint32_t descriptor;
        double worksum;
        std::string worksumHex;
        std::vector<std::string> grid;
    };
    PeerinfoConnection connection;
    ThrottleState throttle;
    Chain chain;
    LeaderPriority leaderPriority;
    struct glaze {
        static constexpr const char* name = "Peerinfo";
    };
};

struct RichlistEntry {
    std::string address;
    FundsDecimal balance;
    struct glaze {
        static constexpr const char* name = "RichlistEntry";
    };
};
struct Token {
    uint64_t id;
    std::string spec;
    std::string name;
    int decimals;
    struct glaze {
        static constexpr const char* name = "Token";
    };
};

struct RichlistResult {
    Token token;
    std::vector<RichlistEntry> richlist;
    struct glaze {
        static constexpr const char* name = "RichlistResult";
    };
};
struct Connection {
    std::string endpoint;
    uint64_t id;
    struct glaze {
        static constexpr const char* name = "Connection";
    };
};
struct ThrottledPeer {
    ThrottleState throttle;
    Connection connection;
    struct glaze {
        static constexpr const char* name = "ThrottledPeer";
    };
};
struct AssetLookupTrace {
    std::vector<AssetDetail> fails;
    std::optional<uint32_t> snapshotHeight;
    struct glaze {
        static constexpr const char* name = "AssetLookupTrace";
    };
};

struct TokenBalanceLookup {
    FundsBalance balance;
    Token token;
    std::optional<AssetLookupTrace> lookupTrace;
    std::optional<Account> account;
    struct glaze {
        static constexpr const char* name = "TokenBalanceLookup";
    };
};

struct TransactionDetails {
    using Variant = std::variant<
        reward::Transaction,
        wart_transfer::Transaction,
        token_transfer::Transaction,
        new_order::TransactionMaybeProcessed,
        match::Transaction,
        liquidity_deposit::TransactionMaybeProcessed,
        liquidity_withdrawal::TransactionMaybeProcessed,
        asset_creation::TransactionMaybeProcessed,
        cancelation::TransactionMaybeProcessed>;
    Variant transaction;
    std::string type;
    struct Mined {
        uint64_t historyId;
        struct Block {
            uint32_t hegiht;
            std::string hash;
            uint32_t timestamp;
        } block;
    };
    std::optional<Mined> mined;
    uint32_t confirmations;
    struct glaze {
        static constexpr const char* name = "TransactionDetails";
    };
};
struct CompactFee {
    std::string str;
    uint64_t E8;
    std::string bytes;
    struct glaze {
        static constexpr const char* name = "CompactFee";
    };
};
struct TransactionMinfeeResult {
    CompactFee minFee;
    struct glaze {
        static constexpr const char* name = "TransactionMinfeeResult";
    };
};
struct TransmissionChartsResult {
    struct Element {
        uint32_t begin;
        uint32_t end;
        size_t rx;
        size_t tx;
    };
    std::map<std::string, std::vector<Element>> byHost;
    struct glaze {
        static constexpr const char* name = "TransmissionChartsResult";
    };
};
struct Wallet {
    std::string privKey;
    std::string pubKey;
    std::string address;
    struct glaze {
        static constexpr const char* name = "Wallet";
    };
};
struct WartBalanceResult {
    WartBalance wart;
    std::optional<Account> account;
    struct glaze {
        static constexpr const char* name = "WartBalanceResult";
    };
};
struct OffenseEntry {
    std::string ip;
    Timepoint time;
    std::string offense;
    struct glaze {
        static constexpr const char* name = "OffenseEntry";
    };
};
struct RoundedFeeResult {
    Wart original;
    CompactFee rounded;
    struct glaze {
        static constexpr const char* name = "RoundedFeeResult";
    };
};
struct RollbackResult {
    uint32_t length;
    struct glaze {
        static constexpr const char* name = "RollbackResult";
    };
};
struct NodeVersion {
    std::string name;
    int major;
    int minor;
    int patch;
    std::string commit;
    struct glaze {
        static constexpr const char* name = "NodeVersion";
    };
};
struct NodeInfoResult {
    struct Uptime {
        Timepoint since;
        uint32_t seconds;
        std::string formatted;
    };
    size_t dbSize;
    std::string chainDBPath;
    std::string peersDBPath;
    std::string rxtxDBPath;
    NodeVersion version;
    Uptime uptime;
    struct glaze {
        static constexpr const char* name = "NodeInfoResult";
    };
};
struct HeaderInfo {
    uint32_t height;
    BlockHeader header;
};
struct HashrateInfo {
    size_t nBlocks;
    uint64_t estimate;
};
struct AccountHistory {
    uint64_t fromId;
    std::vector<Block> perBlock;
};

using Candle = std::tuple<uint32_t, uint32_t, double, double, double, double, double, double>;
using Trade = std::tuple<uint32_t, uint32_t, double, double>;

template <typename T>
struct Success {
    int code;
    T data;
};
template<>
struct Success<void> {
    int code;
};
struct Error {
    int code;
    std::string error;
};
template <typename T>
using Result = std::variant<Success<T>, Error>;

}
}
