#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace api {
namespace glaze {
struct FundsDecimalNoDecimals {
    std::string str;
    uint64_t u64;
};
struct FundsDecimal {
    std::string str;
    uint64_t u64;
    int decimals;
};

struct Grid {
    std::vector<std::string> headers;
};
struct HashResult {
    std::string hash;
};
struct BanEntry {
    std::string ip;
    uint32_t banuntil;
    std::string reason;
};

struct Account {
    std::string address;
    uint64_t accountId;
};
struct Wart {
    std::string str;
    uint64_t E8;
};

struct TransactionSignedCommon {
    uint64_t originId;
    std::string originAddress;
    Wart fee;
    uint32_t nonceId;
    uint32_t pinHeight;
    // std::string signature;
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

struct Price {
    int precExponent10;
    int exponent2;
    int mantissa;
    std::string hex;
    double doubleAdjusted;
    double doubleRaw;
};
struct BaseQuote {
    FundsDecimal base;
    Wart quote;
};
struct AssetBasic {
    std::string hash;
    uint64_t id;
    std::string name;
    uint32_t decimals;
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
};

struct TransactionId {
    uint64_t accountId;
    uint32_t nonceId;
    uint32_t pinHeight;
};

namespace reward {
struct Data {
    std::string toAddress;
    Wart amount;
};
using Transaction = Transaction<Data>;
using WithHistoryId = WithHistoryId<Transaction>;
}

namespace wart_transfer {
struct Data {
    std::string toAddress;
    Wart amount;
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
};
using Transaction = SignedTransaction<Data>;
using WithHistoryId = WithHistoryId<Transaction>;
}

namespace asset_creation {
struct Data {
    std::string name;
    FundsDecimal supply;
};
struct Processed {
    uint64_t assetId;
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
    Price limit;
    bool buy;
};
struct Processed {
    FundsDecimal filled;
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
    };
    AssetBasic baseAsset;
    BaseQuote poolBefore;
    BaseQuote poolAfter;
    std::vector<SwapEntry> buySwaps;
    std::vector<SwapEntry> sellSwaps;
};
using Transaction = Transaction<Data>;
using WithHistoryId = WithHistoryId<Transaction>;
}

namespace liquidity_deposit {
struct Data {
    AssetBasic baseAsset;
    BaseQuote deposited;
};
struct Processed {
    FundsDecimal sharesReceived;
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
};
struct Processed {
    BaseQuote received;
};
using TransactionUnprocessed = SignedTransaction<Data>;
using TransactionProcessed = SignedTransactionProcessed<Data, Processed>;
using TransactionMaybeProcessed = SignedTransactionMaybeProcessed<Data, Processed>;
using WithHistoryId = WithHistoryId<TransactionProcessed>;
}

namespace cancelation {
struct Data {
    TransactionId cancelTxid;
};
struct Processed {
    std::string canceledTxHash;
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
};

struct AddressCount {
    std::string address;
    uint64_t count;
};
struct AssetSearchResult {
    std::vector<AssetDetail> matches;
    std::string hashPrefix;
    std::string namePrefix;
};
struct POW {
    bool verusV2_2;
    std::string hashVerus;
    std::string hashSha256t;
    double floatVerus;
    double floatSha256t;
};

struct BlockHeader {
    std::string raw;
    uint32_t timestamp;
    std::string utc;
    std::string target;
    std::string hash;
    POW pow;
    std::string merkleroot;
    std::string nonce;
    std::string prevHash;
    std::string version;
};
struct HeaderInfo {
    BlockHeader header;
};
struct Block {
    BlockHeader header;
    BlockActions body;
    uint32_t confirmations;
    uint32_t height;
};
struct BlockBinaryAnnotation {
    std::string tag;
    std::string beginOffset;
    std::string endOffset;
    std::vector<BlockBinaryAnnotation> children;
};

struct BlockBinary {
    std::string bytes;
    std::vector<BlockBinaryAnnotation> structure;
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
};
struct ChainHeadSynced {
    ChainHead chainHead;
    bool synced;
};
struct FundsBalance {
    FundsDecimal total;
    FundsDecimal locked;
    FundsDecimal mempool;
};
struct WartBalance {
    Wart total;
    Wart locked;
    Wart mempool;
};

struct Uint32Range {
    uint32_t begin;
    uint32_t end;
};
struct HashrateBlockChart {
    Uint32Range range;
    std::vector<double> data;
};
struct HashrateTimeChart {
    struct Entry {
        uint32_t timestamp;
        uint32_t height;
        uint64_t hashrate;
    };
    Uint32Range range;
    std::vector<Entry> data;
    uint32_t interval;
};
struct HashrateInfo {
    uint64_t lastNBlocksEstimate;
    size_t N;
};

struct JanushashInfo {
    double janushashNumber;
};

struct LiquidityPool {
    FundsDecimal asset;
    Wart wart;
    FundsDecimal shares;
};
struct MempoolUpdate {
    size_t deleted;
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
};

struct SwapOrder {
    std::string txHash;
    Price limit;
    FundsDecimal amount;
    FundsDecimal filled;
};

struct MarketOrders {
    AssetBasic baseAsset;
    std::vector<SwapOrder> wartToAssetSwaps;
    std::vector<SwapOrder> assetToWartSwaps;
};
struct ToPool {
    bool isQuote;
    FundsDecimal amount;
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
};
struct ParsedPrice {
    uint32_t decimals;
    Price floor;
    Price ceil;
};

struct ThrottleState {
    struct State {
        uint32_t h1;
        uint32_t h2;
        size_t window;
    };
    int delay;
    State blockRequest;
    State headerRequest;
};
struct Timepoint {
    std::string UTC;
    uint32_t timestamp;
};
struct Peerinfo {
    struct Connection {
        Timepoint since;
        int port;
        std::optional<std::string> ip;
    };
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
    Connection connection;
    ThrottleState throttle;
    Chain chain;
    LeaderPriority leaderPriority;
};

struct RichlistEntry {
    std::string address;
    FundsDecimal balance;
};
struct Token {
    uint64_t id;
    std::string spec;
    std::string name;
    int decimals;
};

struct Richlist {
    Token token;
    std::vector<RichlistEntry> richlist;
};
struct Connection {
    std::string endpoint;
    uint64_t id;
};
struct ThrottledPeer {
    ThrottleState throttle;
    Connection connection;
};
struct AssetLookupTrace {
    std::vector<AssetDetail> fails;
    std::optional<uint32_t> snapshotHeight;
};

struct TokenBalanceLookup {
    FundsBalance balance;
    Token token;
    std::optional<AssetLookupTrace> lookupTrace;
    std::optional<Account> account;
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
};
struct CompactFee {
    std::string str;
    uint64_t E8;
    std::string bytes;
};
struct TransactionMinfee {
    CompactFee minFee;
};
struct TransmissionCharts {
    struct Element {
        uint32_t begin;
        uint32_t end;
        size_t rx;
        size_t tx;
    };
    std::map<std::string, std::vector<Element>> byHost;
};
struct Wallet {
    std::string privKey;
    std::string pubKey;
    std::string address;
};
struct WartBalanceResult {
    WartBalance wart;
    std::optional<Account> account;
};
struct OffenseEntry {
    std::string ip;
    uint32_t timestamp;
    std::string utc;
    std::string offense;
};
struct RoundedFeeResult {
    Wart original;
    CompactFee rounded;
};
struct RollbackResult {
    uint32_t length;
};
struct NodeVersion {
    std::string name;
    int major;
    int minor;
    int patch;
    std::string commit;
};
struct NodeInfo {
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
};
using Candle = std::tuple<uint32_t,uint32_t,double,double,double,double,double,double>;
using Trade = std::tuple<uint32_t,uint32_t,double,double>;

}
}
