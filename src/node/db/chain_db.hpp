#pragma once

#include "SQLiteCpp/SQLiteCpp.h"
#include "block/block.hpp"
#include "block/chain/offsts.hpp"
#include "block/id.hpp"
#include "chain/deletion_key.hpp"
#include "chainserver/transaction_ids.hpp"
#include "general/address_funds.hpp"
#include "general/filelock/filelock.hpp"
#include "api/types/forward_declarations.hpp"
class ChainDBTransaction;
class Batch;
struct SignedSnapshot;
class Headerchain;
struct RawBody : public std::vector<uint8_t> {
};
struct RawUndo : public std::vector<uint8_t> {
};

struct Column2 : public SQLite::Column {

    operator Height() const
    {
        int64_t h = getInt64();
        if (h < 0) {
            throw std::runtime_error("Database corrupted. Negative height h="
                + std::to_string(h) + " observed");
        }
        return Height(h);
    }
    template <size_t size>
    operator std::array<uint8_t, size>()
    {
        std::array<uint8_t, size> res;
        if (getBytes() != size)
            throw std::runtime_error(
                "Database corrupted, cannot load " + std::to_string(size) + " bytes");
        memcpy(res.data(), getBlob(), size);
        return res;
    }
    operator std::vector<uint8_t>()
    {
        std::vector<uint8_t> res(getBytes());
        memcpy(res.data(), getBlob(), getBytes());
        return res;
    }
    operator int64_t()
    {
        return getInt64();
    }
    operator HistoryId()
    {
        return HistoryId(int64_t(getInt64()));
    }
    operator AccountId()
    {
        return AccountId(int64_t(getInt64()));
    }
    operator IsUint64()
    {
        return IsUint64(int64_t(getInt64()));
    }
    operator BlockId()
    {
        return BlockId(getInt64());
    }
    operator Funds()
    {
        auto v{Funds::from_value(int64_t(getInt64()))};
        if (!v.has_value()) 
            throw std::runtime_error("Database corrupted, invalid funds");
        return *v;
    }
    operator uint64_t()
    {
        auto i { getInt64() };
        if (i < 0)
            throw std::runtime_error("Database corrupted, expected nonnegative entry");
        return (uint64_t)i;
    }
};

struct Statement2 : public SQLite::Statement {
    using SQLite::Statement::Statement;

    using SQLite::Statement::bind;
    Column2 getColumn(const int aIndex)
    {
        return { Statement::getColumn(aIndex) };
    }
    void bind(const int index, const Worksum& ws)
    {
        bind(index, ws.to_bytes());
    };
    void bind(const int index, const std::vector<uint8_t>& v)
    {
        SQLite::Statement::bind(index, v.data(), v.size());
    }
    template <size_t N>
    void bind(const int index, std::array<uint8_t, N> a)
    {
        SQLite::Statement::bind(index, a.data(), a.size());
    }
    template <size_t N>
    void bind(const int index, View<N> v)
    {
        SQLite::Statement::bind(index, v.data(), v.size());
    }
    void bind(const int index, Funds f)
    {
        SQLite::Statement::bind(index, (int64_t)f.E8());
    };
    void bind(const int index, uint64_t id)
    {
        assert(id< std::numeric_limits<uint64_t>::max());
        SQLite::Statement::bind(index, (int64_t)id);
    };
    void bind(const int index, IsUint64 id)
    {
        SQLite::Statement::bind(index, (int64_t)id.value());
    };
    void bind(const int index, BlockId id)
    {
        SQLite::Statement::bind(index, (int64_t)id.value());
    };
    void bind(const int index, Height id)
    {
        SQLite::Statement::bind(index, (int64_t)id.value());
    };
    template <size_t i>
    void recursive_bind()
    {
    }
    template <size_t i, typename T, typename... Types>
    void recursive_bind(T&& t, Types&&... types)
    {
        bind(i, std::forward<T>(t));
        recursive_bind<i + 1>(std::forward<Types>(types)...);
    }
    template <typename... Types>
    uint32_t run(Types&&... types)
    {
        recursive_bind<1>(std::forward<Types>(types)...);
        auto nchanged = exec();
        reset();
        assert(nchanged >=0);
        return nchanged;
    }

    // private:
    struct Row {
        template <typename T>
        T get(int index)
        {
            value_assert();
            return st.getColumn(index);
        }

        template <size_t N>
        std::array<uint8_t, N> get_array(int index)
        {
            value_assert();
            return st.getColumn(index);
        }
        std::vector<uint8_t> get_vector(int index)
        {
            value_assert();
            return st.getColumn(index);
        }

        template <typename T>
        operator std::optional<T>()
        {
            if (!hasValue)
                return {};
            return get<T>(0);
        }
        bool has_value() { return hasValue; }

    private:
        void value_assert()
        {
            if (!hasValue) {
                throw std::runtime_error(
                    "Database error: trying to access empty result.");
            }
        }
        friend struct Statement2;
        Row(Statement2& st)
            : st(st)
        {
            hasValue = st.executeStep();
        }
        Statement2& st;
        bool hasValue;
    };
    struct SingleResult : public Row {
        using Row::Row;
        ~SingleResult()
        {
            if (hasValue)
                assert(st.executeStep() == false);
            st.reset();
        }
    };

public:
    template <typename... Types>
    [[nodiscard]] SingleResult one(Types&&... types)
    {
        recursive_bind<1>(std::forward<Types>(types)...);
        return SingleResult { *this };
    }

    template <typename... Types, typename Lambda>
    void for_each(Lambda lambda, Types&&... types)
    {
        recursive_bind<1>(std::forward<Types>(types)...);
        while (true) {
            auto r { Row(*this) };
            if (!r.has_value())
                break;
            lambda(r);
        }
        reset();
    }
};

class ChainDB {
private:
    friend class ChainDBTransaction;
    // ids to save additional information in tables
    static constexpr int64_t WORKSUMID = -1;
    static constexpr int64_t SIGNEDPINID = -2;

public:
    ChainDB(const std::string& path);
    [[nodiscard]] ChainDBTransaction transaction();
    void set_balance(AccountId stateId, Funds newbalance)
    {
        stmtStateSetBalance.run(newbalance, stateId);
    };
    void insertStateEntry(const AddressView address, Funds balance,
        AccountId verifyNextStateId);

    void delete_state_from(AccountId fromAccountId);
    // void setStateBalance(AccountId accountId, Funds balance);
    void insert_consensus(NonzeroHeight height, BlockId blockId, HistoryId historyCursor, AccountId accountCursor);
    std::tuple<std::vector<Batch>, HistoryHeights, AccountHeights>
    getConsensusHeaders() const;

    // Consensus Functions
    Worksum get_consensus_work() const;
    void set_consensus_work(const Worksum& ws);
    std::optional<SignedSnapshot> get_signed_snapshot() const;
    void set_signed_snapshot(const SignedSnapshot&);
    [[nodiscard]] std::vector<BlockId> consensus_block_ids(Height begin, Height end) const;

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
    [[nodiscard]] std::optional<BlockId> lookup_block_id(const HashView hash) const;
    [[nodiscard]] std::optional<NonzeroHeight> lookup_block_height(const HashView hash) const;
    [[nodiscard]] std::optional<std::tuple<Header, RawBody, RawUndo>> get_block_undo(BlockId id) const;
    [[nodiscard]] std::optional<Block> get_block(BlockId id) const;
    [[nodiscard]] std::optional<std::pair<BlockId, Block>> get_block(HashView hash) const;
    // set
    std::pair<BlockId, bool> insert_protect(const Block&);
    void set_block_undo(BlockId id, const std::vector<uint8_t>& undo);

    /////////////////////
    // Account functions
    // get
    [[nodiscard]] std::optional<AddressFunds> lookup_account(AccountId id) const;
    [[nodiscard]] API::Richlist lookup_richlist(uint32_t N) const;
    [[nodiscard]] AddressFunds fetch_account(AccountId id) const;

    /////////////////////
    // Transactions functions
    [[nodiscard]] API::Richlist look(size_t N) const;

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

    AccountId next_state_id() const { return AccountId(cache.maxStateId + 1); };
    HistoryId insertHistory(const HashView hash,
        const std::vector<uint8_t>& data);
    void delete_history_from(NonzeroHeight);
    std::optional<std::pair<std::vector<uint8_t>, HistoryId>> lookup_history(const HashView hash);
    std::optional<std::pair<std::vector<uint8_t>, Hash>> lookup_history(HistoryId);

    std::vector<std::pair<Hash, std::vector<uint8_t>>>
    lookupHistoryRange(HistoryId lower, HistoryId upper);
    void insertAccountHistory(AccountId accountId, HistoryId historyId);
    HistoryId next_history_id() const { return cache.nextHistoryId; }

    //////////////////////////////
    // BELOW METHODS REQUIRED FOR INDEXING NODES
    std::optional<AccountFunds> lookup_address(const AddressView address) const; // for indexing nodes
    std::vector<std::tuple<HistoryId, Hash, std::vector<uint8_t>>> lookup_history_100_desc(AccountId account_id, int64_t beforeId);



private:
    [[nodiscard]] bool schedule_exists(BlockId dk);
    [[nodiscard]] bool consensus_exists(Height h, BlockId dk);

private:
    SQLite::Database db;
    Filelock fl;
    struct CreateTables {
        CreateTables(SQLite::Database& db)
        {
            db.exec("PRAGMA foreign_keys = ON");
            db.exec("CREATE TABLE IF NOT EXISTS `AccountHistory` (`account_id` "
                    "INTEGER, `history_id` INTEGER, PRIMARY "
                    "KEY(`account_id`,`history_id`)) "
                    "WITHOUT ROWID");

            db.exec("CREATE TABLE IF NOT EXISTS `Blocks` ( `height` INTEGER "
                    "NOT NULL, `header` BLOB NOT NULL, `body` BLOB NOT NULL, "
                    "`undo` BLOB DEFAULT null, `hash` BLOB NOT NULL UNIQUE )");
            db.exec("CREATE TABLE IF NOT EXISTS \"Consensus\" ( `height` INTEGER NOT "
                    "NULL, `block_id` INTEGER NOT NULL, `history_cursor` INTEGER NOT "
                    "NULL, `account_cursor` INTEGER NOT NULL, PRIMARY KEY(`height`) )");
            db.exec(" INSERT OR IGNORE INTO `Consensus` (`height`, "
                    "`block_id`,`history_cursor`, `account_cursor`) "
                    "VALUES "
                    "(-1,x'"
                    "0000000000000000000000000000000000000000000000000000000000"
                    "000000',0,0);");
            db.exec("CREATE TABLE IF NOT EXISTS `State` ( `address` BLOB NOT "
                    "NULL UNIQUE, "
                    "`balance` INTEGER NOT NULL DEFAULT 0 )");
            db.exec("CREATE TABLE IF NOT EXISTS `Badblocks` ( `height` INTEGER, "
                    "`header` "
                    "BLOB UNIQUE )");
            db.exec("CREATE TABLE IF NOT EXISTS`Deleteschedule` ( `block_id`	INTEGER NOT NULL, `deletion_key`	INTEGER, PRIMARY KEY(`block_id`))");

            // create indices
            db.exec("CREATE INDEX IF NOT EXISTS `deletion_key` ON `Deleteschedule` ( `deletion_key`)");
            db.exec("CREATE INDEX IF NOT EXISTS `account_history_index` ON "
                    "`AccountHistory` (`history_id` ASC)");
            db.exec("CREATE TABLE IF NOT EXISTS `History` ( `id` INTEGER NOT NULL, "
                    "`hash` BLOB NOT NULL, `data` BLOB NOT NULL, PRIMARY KEY(`id`))");
            db.exec("CREATE INDEX IF NOT EXISTS `history_index` ON "
                    "`History` (`hash` ASC)");
        }
    } createTables;
    struct Cache {
        AccountId maxStateId;
        HistoryId nextHistoryId;
        DeletionKey deletionKey;
        static Cache init(SQLite::Database& db);
    } cache;
    Statement2 stmtBlockInsert;
    Statement2 stmtUndoSet;
    mutable Statement2 stmtBlockGetUndo;
    mutable Statement2 stmtBlockById;
    mutable Statement2 stmtBlockByHash;

    // Consensus table functions
    mutable Statement2 stmtConsensusHeaders;
    Statement2 stmtConsensusInsert;
    // Statement2 stmtConsensusSet;
    Statement2 stmtConsensusSetProperty;
    mutable Statement2 stmtConsensusSelect;
    mutable Statement2 stmtConsensusSelectRange;
    mutable Statement2 stmtConsensusSelectHistory;
    mutable Statement2 stmtConsensusHead;
    Statement2 stmtConsensusDeleteFrom;

    Statement2 stmtScheduleExists;
    Statement2 stmtScheduleInsert;
    Statement2 stmtScheduleBlock;
    Statement2 stmtScheduleProtected;
    Statement2 stmtScheduleDelete2;
    Statement2 stmtScheduleConsensus;
    Statement2 stmtDeleteGCBlocks;
    Statement2 stmtDeleteGCRefs;

    Statement2 stmtStateInsert;
    Statement2 stmtStateDeleteFrom;
    Statement2 stmtStateSetBalance;
    Statement2 stmtBadblockInsert;
    mutable Statement2 stmtBadblockGet;
    mutable Statement2 stmtAccountLookup;
    mutable Statement2 stmtRichlistLookup;
    Statement2 stmtHistoryInsert;
    Statement2 stmtHistoryDeleteFrom;
    mutable Statement2 stmtHistoryLookupByHash;
    mutable Statement2 stmtHistoryLookupById;
    mutable Statement2 stmtHistoryLookupRange;
    Statement2 stmtAccountHistoryInsert;
    Statement2 stmtAccountHistoryDeleteFrom;

    mutable Statement2 stmtBlockIdSelect;
    mutable Statement2 stmtBlockHeightSelect;
    Statement2 stmtBlockDelete;

    mutable Statement2 stmtAddressLookup;
    mutable Statement2 stmtHistoryById;
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
    friend class ChainDB;
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
