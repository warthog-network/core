#pragma once

#include "SQLiteCpp/Database.h"
#include "SQLiteCpp/Transaction.h"
#include "api/types/all_fwd.hpp"
#include "api/types/asset_lookup_trace.hpp"
#include "block/block.hpp"
#include "block/block_fwd.hpp"
#include "block/body/container.hpp"
#include "block/chain/history/history.hpp"
#include "block/chain/offsts.hpp"
#include "block/chain/worksum.hpp"
#include "block/id.hpp"
#include "chainserver/db/state_ids.hpp"
#include "chainserver/transaction_ids.hpp"
#include "db/sqlite_fwd.hpp"
#include "defi/token/info.hpp"
#include "defi/uint64/pool.hpp"
#include "deletion_key.hpp"
#include "general/address_funds.hpp"
#include "general/filelock/filelock.hpp"
#include "general/timestamp.hpp"
#include "order_loader.hpp"
#include "types_fwd.hpp"

struct CreatorToken;
struct AccountToken;
class HeaderBatch;
class AssetName;
class CancelId;
struct SignedSnapshot;
class Headerchain;

struct RawUndo : public std::vector<uint8_t> {
};
struct Candle {
    Timestamp timestamp;
    Price_uint64 open;
    Price_uint64 high;
    Price_uint64 low;
    Price_uint64 close;
    Funds_uint64 quantity;
    Funds_uint64 volume;
};

struct BlockUndoData {
    Header header;
    BodyData body;
    RawUndo rawUndo;
};

struct Balance_uint64 {
private:
    constexpr Balance_uint64(Funds_uint64 total, Funds_uint64 locked)
        : total(std::move(total))
        , locked(std::move(locked))
    {
    }

public:
    Balance_uint64(Reader& r)
        : total(r)
        , locked(r)
    {
    }
    static constexpr Balance_uint64 from_total_locked(Funds_uint64 total, Funds_uint64 locked)
    {
        return { total, locked };
    }
    constexpr static size_t byte_size() { return 2 * Funds_uint64::byte_size(); }
    void serialize(RawSerializer auto&& s) const
    {
        s << total << locked;
    }

    auto free_assert() const { return diff_assert(total, locked); }
    static constexpr Balance_uint64 zero()
    {
        return { 0, 0 };
    }
    // data
    Funds_uint64 total;
    Funds_uint64 locked; // <= total
};

namespace chain_db {

using Statement = sqlite::Statement;
template <typename T>
class StateIdStatementsWrapper;

template <typename T, typename... R>
class StateIdStatementsWrapper<StateIdBase<T, R...>> {
    template <typename S>
    static Statement delete_from_stmt(SQLite::Database& db);

    template <typename S>
    using statement_wrapper = Statement;
    std::tuple<statement_wrapper<R>...> deleteStatements;

public:
    StateIdStatementsWrapper(SQLite::Database& db)
        : deleteStatements(delete_from_stmt<R>(db)...)
    {
    }
    void delete_from(const T& t)
    {
        std::apply([&](auto&... s) { (s.run(t), ...); }, deleteStatements);
    }
};

template <typename T>
using StateIdStatements = StateIdStatementsWrapper<typename T::parent_t>;

namespace type_info {
template <typename T>
struct TypeInfo {
    static_assert(false, "Type name not implemented");
};
template <>
struct TypeInfo<AssetDetail> {
    constexpr static std::string_view name = "Asset";
    static constexpr Error notfound_error(AssetId)
    {
        return Error(EASSETIDNOTFOUND);
    }
    static constexpr Error notfound_error(AssetHash)
    {
        return Error(EASSETHASHNOTFOUND);
    }
};
template <>
struct TypeInfo<Address> {
    constexpr static std::string_view name = "Address";
    static constexpr Error notfound_error(AccountId)
    {
        return Error(EACCIDNOTFOUND);
    }
};

template <>
struct TypeInfo<history::Entry> {
    constexpr static std::string_view name = "History";
};

}

class ChainDB {
private:
    friend class ChainDBTransaction;
    // ids to save additional information in tables
    static constexpr int64_t WORKSUMID = -1;
    static constexpr int64_t SIGNEDPINID = -2;

public:
    ChainDB(const std::string& path);
    [[nodiscard]] ChainDBTransaction transaction();
    template <typename T>
    void insert_guarded(const T& t, bool checkId = true)
    {
        if (checkId) {
            auto& stateId { cache.ids.corresponding_state(t.id) };
            stateId.if_unequal_throw(t.id);
            insert_unguarded(t);
            stateId++;
        } else {
            insert_unguarded(t);
        }
    }

private:
    void insert_unguarded(const AccountData&);
    void insert_unguarded(const TokenForkBalanceData&);
    void insert_unguarded(const AssetData&);
    void insert_unguarded(const BalanceData&);

public:
    // void delete_state32_from(StateId32 fromStateId);
    void delete_state64_from(StateId64 fromStateId);
    // void setStateBalance(AccountId accountId, Funds balance);
    void insert_consensus(NonzeroHeight height, BlockId blockId, HistoryId historyCursor, StateId64 stateId);

    std::tuple<std::vector<HeaderBatch>, HistoryHeights, State64Heights>
    get_consensus_headers() const;

    // Consensus Functions
    Worksum get_consensus_work() const;
    void set_consensus_work(const Worksum& ws);
    wrt::optional<SignedSnapshot> get_signed_snapshot() const;
    void set_signed_snapshot(const SignedSnapshot&);
    [[nodiscard]] std::vector<BlockId> consensus_block_ids(HeightRange) const;
    [[nodiscard]] wrt::optional<BlockId> consensus_block_id(Height) const;

    //////////////////
    // delete schedule functiosn
    [[nodiscard]] DeletionKey delete_consensus_from(NonzeroHeight height);

    void garbage_collect_blocks(DeletionKey);
    [[nodiscard]] DeletionKey schedule_protected_all();
    [[nodiscard]] DeletionKey schedule_protected_part(Headerchain hc, NonzeroHeight fromHeight);
    void protect_stage_assert_scheduled(BlockId id);

    //////////////////
    // Block functions
    // get
    [[nodiscard]] wrt::optional<BlockId> lookup_block_id(const HashView hash) const;
    [[nodiscard]] wrt::optional<NonzeroHeight> lookup_block_height(const HashView hash) const;
    [[nodiscard]] wrt::optional<BlockUndoData> get_block_undo(BlockId id) const;
    [[nodiscard]] wrt::optional<Block> get_block(BlockId id) const;
    [[nodiscard]] wrt::optional<std::pair<BlockId, Block>> get_block(HashView hash) const;
    [[nodiscard]] wrt::optional<BlockData> get_block_data(BlockId id) const;
    [[nodiscard]] wrt::optional<BodyData> get_block_body(HashView hash) const;
    // set
    std::pair<BlockId, bool> insert_protect(const Block&);
    void set_block_undo(BlockId id, const std::vector<uint8_t>& undo);

    /////////////////////
    // Order functions
    void prune_candles(Timestamp timestamp);
    void insert_candles_5m(TokenId, const Candle&);
    wrt::optional<Candle> select_candle_5m(TokenId, Timestamp);
    std::vector<Candle> select_candles_5m(TokenId, Timestamp from, Timestamp to);
    void insert_candles_1h(TokenId, const Candle&);
    wrt::optional<Candle> select_candle_1h(TokenId, Timestamp);
    std::vector<Candle> select_candles_1h(TokenId, Timestamp from, Timestamp to);

    /////////////////////
    // Order functions
    void insert(const chain_db::OrderData&);
    void update_order_fillstate(const chain_db::OrderFillstate&);
    void delete_order(const chain_db::OrderDelete&);
    [[nodiscard]] wrt::optional<chain_db::OrderData> select_order(TransactionId) const;

    [[nodiscard]] std::vector<std::pair<chain_db::OrderData,TxHash>> lookup_account_orders(AccountId) const;
    [[nodiscard]] std::vector<std::pair<chain_db::OrderData, TxHash>> lookup_account_orders_market(AccountId, AssetId) const;

    [[nodiscard]] OrderLoaderAscending base_order_loader_ascending(AssetId) const;
    [[nodiscard]] OrderLoaderDescending quote_order_loader_descending(AssetId) const;

    [[nodiscard]] OrderLoaderTxhashAscending base_order_loader_txhash_ascending(AssetId) const;
    [[nodiscard]] OrderLoaderTxhashDescending quote_order_loader_txhash_descending(AssetId) const;

    /////////////////////
    // Canceled functions

    void insert_canceled(CancelId cid, AccountId aid, PinHeight ph, NonceId nid);
    void delete_canceled(CancelId cid);

    /////////////////////
    // Account functions
    // get
    [[nodiscard]] wrt::optional<Address> lookup_address(AccountId id) const;

    template <typename T>
    static std::string_view type_string()
    {
        if constexpr (std::is_same_v<T, AssetDetail>) {
            return "Asset";
        } else if constexpr (std::is_same_v<T, Address>) {
            return "Address";
        } else {
            static_assert(false, "Not implemented");
        }
    }

    template <typename T>
    auto lookup(auto&& key) const
    {
        if constexpr (std::is_same_v<T, AssetDetail>) {
            return lookup_asset(key);
        } else if constexpr (std::is_same_v<T, Address>) {
            return lookup_address(key);
        } else if constexpr (std::is_same_v<T, history::Entry>) {
            return lookup_history(key);
        } else {
            static_assert(false, "Lookup not implemented");
        }
    }

    static std::runtime_error database_error_for_type(std::string_view typeName);
    template <typename T>
    T fetch_existing(auto&& key) const
    {
        auto p { lookup<T>(std::forward<decltype(key)>(key)) };
        if (!p) {
            throw database_error_for_type(type_info::TypeInfo<T>::name);
        }
        return *p;
    }

    template <typename T>
    T fetch_throw(auto&& key) const
    {
        auto p { lookup<T>(key) };
        if (!p) {
            Error e { type_info::TypeInfo<T>::notfound_error(key) };
            throw e;
        }
        return *p;
    }

    /////////////////////
    // Pool functions
    void insert_pool(const PoolData& pool);
    void delete_pool(AssetId assetId);
    [[nodiscard]] wrt::optional<PoolData> select_pool(AssetId assetId) const;
    void update_pool(const PoolData&);

    /////////////////////
    // Token fork balance functions

public:
    // bool fork_balance_exists(AccountToken, NonzeroHeight);
    wrt::optional<std::pair<NonzeroHeight, Funds_uint64>> get_balance_snapshot_after(TokenId tokenId, NonzeroHeight minHeight) const;

    /////////////////////
    // Token functions

public:
    [[nodiscard]] wrt::optional<NonzeroHeight> get_latest_fork_height(TokenId, Height);

    [[nodiscard]] wrt::optional<BalanceData> get_token_balance(BalanceId id) const;

private:
    [[nodiscard]] wrt::optional<std::pair<BalanceId, Balance_uint64>> get_balance(AccountId aid, TokenId tid) const;

public:
    ////////////////////
    // Asset functions
    [[nodiscard]] std::vector<AssetDetail> search_assets(const api::AssetSearchArgs&) const;
    [[nodiscard]] Result<AssetDetail> lookup_asset(AssetId) const;
    [[nodiscard]] Result<AssetDetail> lookup_asset(const AssetHash&) const;
    void set_balance(BalanceId, Balance_uint64 bl);
    std::vector<std::pair<TokenId, Funds_uint64>> get_tokens(AccountId, size_t limit);
    [[nodiscard]] api::Richlist lookup_richlist(TokenId, size_t limit) const;
    /////////////////////
    // Transactions functions
    [[nodiscard]] api::Richlist look(size_t N) const;

    ///////////////
    // Deleteschedule functions

    ///////////////
    // direct delete (without delete schedule)
    void delete_bad_block(HashView);

    // txids

    chainserver::TransactionIds fetch_tx_ids(Height) const;

    ///////////////
    // Badblocks functions
    // get
    std::vector<std::pair<Height, Header>>
    getBadblocks() const;
    // set
    void insert_bad_block(NonzeroHeight height, const HeaderView header);

    void insert_history_link(HistoryId parent, HistoryId link);
    HistoryId insertHistory(const HashView hash,
        const std::vector<uint8_t>& data);
    void delete_history_from(NonzeroHeight);
    wrt::optional<std::pair<history::HistoryVariant, HistoryId>> lookup_history(const HashView hash) const;

    [[nodiscard]] std::vector<std::pair<HistoryId, history::Entry>> lookup_history_range(HistoryId lower, HistoryId upper) const;
    [[nodiscard]] wrt::optional<history::Entry> lookup_history(HistoryId id) const;
    void insertAccountHistory(AccountId accountId, HistoryId historyId);
    HistoryId next_history_id() const
    {
        return cache.nextHistoryId;
    }
    const auto& id_incrementer() const { return cache.ids; }
    auto next_id() const { return cache.ids.next(); }
    StateId64 next_id64() const { return next_id(); }
    // StateId32 next_id32() const { return next_id(); }

    struct RecursiveTokenLookup {
        struct Match {
            BalanceId balanceId;
            bool forked;
        };

        Balance_uint64 balance;
        tl::optional<Match> match;
        RecursiveTokenLookup(Balance_uint64 balance)
            : balance(std::move(balance))
        {
        }
        RecursiveTokenLookup(Balance_uint64 balance, Match match)
            : balance(std::move(balance))
            , match(std::move(match))
        {
        }
    };
    [[nodiscard]] RecursiveTokenLookup get_token_balance_recursive(AccountId aid, TokenId tid, api::AssetLookupTrace* trace = nullptr) const;
    [[nodiscard]] Funds_uint64 get_free_balance(AccountToken at) const;

    //////////////////////////////
    // BELOW METHODS REQUIRED FOR INDEXING NODES
    [[nodiscard]] wrt::optional<AccountId> lookup_account(const AddressView address) const; // for indexing nodes
    std::vector<std::tuple<HistoryId, history::Entry>> lookup_history_100_desc(AccountId account_id, int64_t beforeId);
    size_t byte_size() const;

private:
    [[nodiscard]] bool schedule_exists(BlockId dk);
    [[nodiscard]] bool consensus_exists(Height h, BlockId dk);

private:
    Filelock fl;
    struct Database : public SQLite::Database {
        Database(const std::string& path);
    } db;
    struct Cache {
        StateIncrementer ids;
        HistoryId nextHistoryId;
        DeletionKey deletionKey;
        static Cache init(SQLite::Database& db);
    } cache;
    // StateIdStatements<StateId32> state32Statements;
    StateIdStatements<StateId64> state64Statements;
    Statement stmtBlockInsert;
    Statement stmtUndoSet;
    mutable Statement stmtBlockGetUndo;
    mutable Statement stmtBlockById;
    mutable Statement stmtBlockByHash;

    // Candles statements
    Statement stmtPruneCandles5m;
    Statement stmtInsertCandles5m;
    Statement stmtSelectCandles5m;
    Statement stmtPruneCandles1h;
    Statement stmtInsertCandles1h;
    Statement stmtSelectCandles1h;

    // Orders statements
    Statement stmtInsertBaseSellOrder;
    Statement stmtUpdateFillBaseSellOrder;
    Statement stmtDeleteBaseSellOrder;
    Statement stmtDeleteBaseSellOrderTxid;
    Statement stmtSellOrderDeleteFrom;
    Statement stmtInsertQuoteBuyOrder;
    Statement stmtUpdateFillQuoteBuyOrder;
    Statement stmtDeleteQuoteBuyOrder;
    Statement stmtDeleteQuoteBuyOrderTxid;
    Statement stmtBuyOrderDeleteFrom;
    mutable Statement stmtSelectBaseSellOrderAsc;
    mutable Statement stmtSelectQuoteBuyOrderDesc;
    mutable Statement stmtSelectBaseSellOrderTxhashAsc;
    mutable Statement stmtSelectQuoteBuyOrderTxhashDesc;

    mutable Statement stmtSelectQuoteBuyOrderAccount;
    mutable Statement stmtSelectQuoteBuyOrderAccountAsset;
    mutable Statement stmtSelectBaseSellOrderAccount;
    mutable Statement stmtSelectBaseSellOrderAccountAsset;

    mutable Statement stmtSelectBaseSell;
    mutable Statement stmtSelectQuoteBuy;

    // Canceled statements
    Statement stmtInsertCanceled;
    Statement stmtDeleteCanceled;

    // Pool statements
    Statement stmtInsertPool;
    Statement stmtDeletePool;
    mutable Statement stmtSelectPool;
    Statement stmtUpdatePool;

    // TokenForks statements
    Statement stmtTokenForkBalanceInsert;
    mutable Statement stmtTokenForkBalanceEntryExists;
    mutable Statement stmtTokenForkBalanceSelect;

    // Asset statements
    Statement stmtAssetInsert;
    mutable Statement stmtAssetSelectForkHeight;
    mutable Statement stmtAssetLookup;
    mutable Statement stmtAssetLookupByNameHash1;
    mutable Statement stmtAssetLookupByNameHash2;
    mutable Statement stmtAssetLookupByNameHash3;
    mutable Statement stmtAssetLookupByHash;
    mutable Statement stmtSelectBalanceId;

    // Balance statements
    Statement stmtTokenInsertBalance;
    mutable Statement stmtTokenSelectBalance;
    mutable Statement stmtAccountSelectAssets;
    Statement stmtTokenUpdateBalanceTotalById;
    mutable Statement stmtTokenSelectRichlist;

    // Consensus table functions
    mutable Statement stmtConsensusHeaders;
    Statement stmtConsensusInsert;
    // Statement stmtConsensusSet;
    Statement stmtConsensusSetProperty;
    mutable Statement stmtConsensusSelect;
    mutable Statement stmtConsensusSelectRange;
    mutable Statement stmtConsensusSelectHistory;
    mutable Statement stmtConsensusHead;
    Statement stmtConsensusDeleteFrom;

    Statement stmtScheduleExists;
    Statement stmtScheduleInsert;
    Statement stmtScheduleBlock;
    Statement stmtScheduleProtected;
    Statement stmtScheduleDelete;
    Statement stmtScheduleConsensus;
    Statement stmtDeleteGCBlocks;
    Statement stmtDeleteGCRefs;

    Statement stmtAccountsInsert;
    Statement stmtBadblockInsert;
    mutable Statement stmtBadblockGet;
    mutable Statement stmtAccountsLookup;
    Statement stmtHistoryLinkInsert;
    Statement stmtHistoryInsert;
    Statement stmtHistoryDeleteFrom;
    mutable Statement stmtHistoryLookup;
    mutable Statement stmtHistoryLookupRange;
    Statement stmtAccountHistoryInsert;
    Statement stmtAccountHistoryDeleteFrom;

    mutable Statement stmtBlockIdSelect;
    mutable Statement stmtBlockHeightSelect;
    Statement stmtBlockDelete;

    mutable Statement stmtAddressLookup;
    mutable Statement stmtHistoryById;
    mutable Statement stmtGetDBSize;
};
class ChainDBTransaction {
public:
    void commit()
    {
        tx.commit();
        commited = true;
    }
    ~ChainDBTransaction()
    {
        if (parent != nullptr && !commited) {
            parent->cache = c;
        }
    }
    ChainDBTransaction(const ChainDBTransaction&) = delete;
    ChainDBTransaction(ChainDBTransaction&& other)
        : parent(other.parent)
        , tx(std::move(other.tx))
        , c(std::move(other.c))
    {
        other.commited = true;
    }

private:
    friend ChainDB;
    ChainDBTransaction(ChainDB& parent)
        : parent(&parent)
        , tx(parent.db)
        , c(parent.cache)
    {
    }
    bool commited = false;
    ChainDB* parent;
    SQLite::Transaction tx;
    ChainDB::Cache c;
};
}
